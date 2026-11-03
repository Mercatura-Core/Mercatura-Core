// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "research_wallet.h"
#include <bech32.h>
#include <crypto/hmac_sha3_384.h>
#include <crypto/mercatura_pqderive.h>
#include <crypto/mercatura_pqkey.h>
#include <random.h>
#include <streams.h>
#include <support/cleanse.h>
#include <util/strencodings.h>
#include <fstream>
#include <limits>

namespace mercaweave::wallet_research {
namespace {
using Branch=std::tuple<uint32_t,uint8_t,uint8_t>;
constexpr size_t SNAPSHOT_LIMIT=16*1024*1024;
struct Wipe { void* p;size_t n;~Wipe(){memory_cleanse(p,n);} };
Bytes ReadBytes(Reader& r,size_t maximum)
{
    auto n=ReadCompactSize(r);Require(n<=maximum&&n<=r.remaining(),Code::Metadata,"bounded-wallet-field");
    Bytes b(n);r.read(std::as_writable_bytes(std::span(b)));return b;
}
void ValidLocation(const Location& p)
{
    Require(p.purpose<=2&&(p.witness==2||p.witness==3),Code::Metadata,"invalid-location");
    Require(p.witness==3?p.address<Wallet::ROTATION:(p.purpose<2&&p.address==0),Code::Metadata,"invalid-address-index-or-native-branch");
}
CScript NativeOutput(const PublicKey& pk)
{
    MercaturaPQKeyCommitment c;Require(ComputeMercaturaPQKeyCommitmentV1(c,pk),Code::Ownership,"native-commitment");
    return CScript{}<<OP_2<<Bytes(c.begin(),c.end());
}
std::filesystem::path SafePath(const std::filesystem::path& path)
{
    auto root=std::filesystem::weakly_canonical(Wallet::StorageRoot());
    auto p=std::filesystem::weakly_canonical(path);
    auto rel=p.lexically_relative(root);
    Require(!rel.empty()&&!rel.is_absolute()&&*rel.begin()!="..",Code::Metadata,"research-storage-path-only");
    return p;
}
}
void Require(bool b,Code c,const char* m){if(!b)throw WalletError(c,m);}
bool SecureRandom(B32& out){GetStrongRandBytes(out);return true;}
std::pair<Cursor,Cursor> NextCursor(Cursor c,uint8_t version)
{
    Require(version==2||version==3,Code::Version,"witness-version");
    if(version==2){Require(c.next==0&&c.group<UINT32_MAX,Code::Index,"native-index-exhausted");auto next=c;++next.group;return {c,next};}
    Require(c.next<=Wallet::ROTATION,Code::Index,"address-index-corrupt");
    if(c.next==Wallet::ROTATION){Require(c.group<UINT32_MAX,Code::Index,"group-index-exhausted");++c.group;c.next=0;}
    auto next=c;++next.next;return {c,next};
}
Wallet::Wallet(B32 g):m_genesis(g){}
Wallet::~Wallet(){Lock();}
Bytes Wallet::Metadata()const
{
    Writer w;w<<SCHEME<<DERIVATION<<m_genesis<<uint8_t(m_watch)<<m_lookahead;
    WriteCompactSize(w,m_cursors.size());
    for(auto [branch,c]:m_cursors){auto [account,purpose,version]=branch;w<<account<<purpose<<version<<c.group<<c.next;}
    WriteCompactSize(w,m_addresses.size());for(const auto& a:m_addresses)w<<a;
    return std::move(w.bytes);
}
H48 Wallet::Mac(const wallet::CKeyingMaterial& seed)const
{
    const auto data=Metadata();H48 result{};
    // Research snapshot authentication, not a MercaWeave signing/hash domain.
    constexpr std::string_view tag="MercaWeave/ResearchWalletMetadata/v1";
    CHMAC_SHA3_384 h(seed.data(),seed.size());
    h.Write(std::span<const unsigned char>{reinterpret_cast<const unsigned char*>(tag.data()),tag.size()}).Write(data).Finalize(result);return result;
}
void Wallet::Authenticate(){Require(m_seed.size()==32,Code::Locked,"locked-wallet");m_mac=Mac(m_seed);m_verified=true;}
std::unique_ptr<Wallet> Wallet::Create(const wallet::CKeyingMaterial& seed,B32 genesis,const SecureString& password)
{
    Require(seed.size()==32,Code::Metadata,"existing-pq-master-seed-must-be-32-bytes");
    auto w=std::unique_ptr<Wallet>(new Wallet(genesis));
    wallet::CKeyingMaterial master(32);GetStrongRandBytes(master);
    w->m_master.vchSalt.resize(wallet::WALLET_CRYPTO_SALT_SIZE);GetStrongRandBytes(w->m_master.vchSalt);
    wallet::CCrypter crypter;
    Require(crypter.SetKeyFromPassphrase(password,w->m_master.vchSalt,w->m_master.nDeriveIterations,0)&&crypter.Encrypt(master,w->m_master.vchCryptedKey),Code::Metadata,"wallet-master-encryption");
    Require(wallet::ResearchEncryptPQSeed(master,seed,w->m_encrypted),Code::Metadata,"native-pq-seed-encryption");
    w->m_mac=w->Mac(seed);w->m_verified=true;return w; // Created encrypted and locked.
}
bool Wallet::Unlock(const SecureString& password)
{
    std::lock_guard lock(m_mutex);Lock();if(m_watch)return false;
    wallet::CKeyingMaterial master,seed;wallet::CCrypter crypter;
    if(!crypter.SetKeyFromPassphrase(password,m_master.vchSalt,m_master.nDeriveIterations,m_master.nDerivationMethod)||
       !crypter.Decrypt(m_master.vchCryptedKey,master)||!wallet::ResearchDecryptPQSeed(master,m_encrypted,seed)||!TimingResistantEqual(Mac(seed),m_mac))return false;
    m_seed=std::move(seed);
    try {for(const auto& a:m_addresses)Require(DeriveRecord(a.location)==a,Code::Ownership,"recovery-mismatch");}
    catch(...){Lock();return false;}
    m_verified=true;return true;
}
void Wallet::Lock(){std::lock_guard lock(m_mutex);if(!m_seed.empty())memory_cleanse(m_seed.data(),m_seed.size());m_seed.clear();}
bool Wallet::IsLocked()const{std::lock_guard lock(m_mutex);return m_seed.size()!=32;}
bool Wallet::WithMercaturaPQMasterSeed(const std::function<bool(const wallet::CKeyingMaterial&)>& cb)const
{std::lock_guard lock(m_mutex);return !m_watch&&m_seed.size()==32&&cb(m_seed);}
Address Wallet::DeriveRecord(Location p)const
{
    ValidLocation(p);Require(!m_watch&&m_seed.size()==32,Code::Locked,"locked-wallet");
    B32 master{},seed{};Wipe wm{master.data(),master.size()},ws{seed.data(),seed.size()};std::copy(m_seed.begin(),m_seed.end(),master.begin());
    Address a;a.location=p;
    if(p.witness==3){seed=Derive(master,m_genesis,p.account,p.purpose,p.group);a.nonce=Derive(master,m_genesis,p.account,p.purpose,p.group,p.address);}
    else {auto derived=DeriveMercaturaPQChildSeed(std::span<const unsigned char,32>{m_seed.data(),32},uint256{std::span<const unsigned char>{m_genesis}},p.account,p.purpose,p.group);
        Require(derived.has_value(),Code::Metadata,"native-derivation");seed=*derived;memory_cleanse(derived->data(),derived->size());}
    KeyPair key(seed);a.pk=key.pk;a.script=p.witness==3?Output(m_genesis,a.pk,a.nonce):NativeOutput(a.pk);return a;
}
std::unique_ptr<KeyPair> Wallet::Key(const Address& address)const
{
    std::lock_guard lock(m_mutex);Require(!m_watch,Code::WatchOnly,"watch-only-wallet");Require(m_seed.size()==32,Code::Locked,"locked-wallet");
    auto it=m_index.find(address.script);Require(it!=m_index.end()&&m_addresses[it->second]==address,Code::Ownership,"unmanaged-address");
    auto expected=DeriveRecord(address.location);Require(expected==address,Code::Ownership,"foreign-wallet-group");
    B32 master{},seed{};Wipe wm{master.data(),32},ws{seed.data(),32};std::copy(m_seed.begin(),m_seed.end(),master.begin());
    if(address.location.witness==3)seed=Derive(master,m_genesis,address.location.account,address.location.purpose,address.location.group);
    else {auto native=DeriveMercaturaPQChildSeed(std::span<const unsigned char,32>{m_seed.data(),32},uint256{std::span<const unsigned char>{m_genesis}},address.location.account,address.location.purpose,address.location.group);
        Require(native.has_value(),Code::Ownership,"native-key");seed=*native;memory_cleanse(native->data(),32);}
    return std::make_unique<KeyPair>(seed);
}
Address Wallet::Issue(uint32_t account,uint8_t purpose,uint8_t version)
{
    std::lock_guard lock(m_mutex);Require(!m_watch,Code::WatchOnly,"watch-only-wallet");Require(m_seed.size()==32,Code::Locked,"locked-wallet");
    Require(purpose<=2&&(version==3||(version==2&&purpose<2)),Code::Metadata,"unsupported-wallet-branch");
    Require(m_addresses.size()<MAX_RECORDS,Code::Index,"research-record-limit");
    Branch branch{account,purpose,version};auto it=m_cursors.find(branch);
    Require(it!=m_cursors.end()||m_cursors.size()<MAX_RECORDS,Code::Index,"research-cursor-limit");
    auto [used,next]=NextCursor(it==m_cursors.end()?Cursor{}:it->second,version);
    auto a=DeriveRecord({account,purpose,used.group,used.next,version});
    Require(!m_index.contains(a.script),Code::Metadata,"duplicate-issued-address");
    m_index.emplace(a.script,m_addresses.size());m_addresses.push_back(a);m_cursors[branch]=next;Authenticate();return a;
}
void Wallet::Rotate(uint32_t account,uint8_t purpose)
{
    std::lock_guard lock(m_mutex);Require(m_seed.size()==32&&!m_watch,Code::Locked,"locked-wallet");Require(purpose<=2,Code::Metadata,"purpose");
    Branch branch{account,purpose,3};Require(m_cursors.contains(branch)||m_cursors.size()<MAX_RECORDS,Code::Index,"research-cursor-limit");
    auto& c=m_cursors[branch];Require(c.group<UINT32_MAX,Code::Index,"group-index-exhausted");++c.group;c.next=0;Authenticate();
}
std::vector<Address> Wallet::Records()const{std::lock_guard lock(m_mutex);Require(m_verified,Code::Metadata,"unverified-restored-metadata");return m_addresses;}
std::optional<Address> Wallet::Find(const CScript& script)const
{
    std::lock_guard lock(m_mutex);Require(m_verified,Code::Metadata,"unverified-restored-metadata");auto it=m_index.find(script);
    return it==m_index.end()?std::nullopt:std::optional<Address>{m_addresses[it->second]};
}
void Wallet::Reindex()
{
    m_index.clear();std::set<Location> paths;
    for(size_t i=0;i<m_addresses.size();++i){const auto& a=m_addresses[i];ValidLocation(a.location);
        Require(paths.insert(a.location).second&&m_index.emplace(a.script,i).second,Code::Metadata,"duplicate-metadata");
        Require(a.script==(a.location.witness==3?Output(m_genesis,a.pk,a.nonce):NativeOutput(a.pk)),Code::Ownership,"public-record-commitment");}
}
void Wallet::RecoverIssued(std::span<const Address> records)
{
    std::lock_guard lock(m_mutex);Require(!m_watch&&m_seed.size()==32,Code::Locked,"locked-wallet");
    Require(records.size()<=MAX_RECORDS,Code::Metadata,"recovery-record-limit");
    for(const auto& a:records)Require(DeriveRecord(a.location)==a,Code::Ownership,"wrong-seed-or-recovery-location");
    auto old=m_addresses;auto cursors=m_cursors;
    try {m_addresses.assign(records.begin(),records.end());Reindex();m_cursors.clear();
        for(const auto& a:m_addresses){auto p=a.location;auto& c=m_cursors[{p.account,p.purpose,p.witness}];
            if(p.witness==2){Require(p.group<UINT32_MAX,Code::Index,"recovery-index");c.group=std::max(c.group,p.group+1);}
            else if(p.group>c.group||(p.group==c.group&&p.address>=c.next))c={p.group,p.address+1};}
        Authenticate();
    }catch(...){m_addresses=std::move(old);m_cursors=std::move(cursors);Reindex();throw;}
}
std::unique_ptr<Wallet> Wallet::Watch(B32 genesis,std::span<const Address> records)
{
    Require(records.size()<=MAX_RECORDS,Code::Metadata,"watch-record-limit");auto w=std::unique_ptr<Wallet>(new Wallet(genesis));
    w->m_watch=true;w->m_addresses.assign(records.begin(),records.end());w->Reindex();
    w->m_mac=PQH384("MercaWeave/ResearchWatchMetadata/v1",w->Metadata());w->m_verified=true;return w;
}
void Wallet::ParseMetadata(std::span<const unsigned char> bytes)
{
    Reader r{bytes};uint32_t scheme,derivation;B32 genesis;uint8_t watch;
    r>>scheme>>derivation>>genesis>>watch>>m_lookahead;
    Require(scheme==SCHEME&&derivation==DERIVATION,Code::Version,"wallet-version");Require(genesis==m_genesis,Code::Network,"wrong-wallet-network");
    Require(watch<=1&&m_lookahead>0&&m_lookahead<=1024,Code::Metadata,"recovery-state");m_watch=watch;
    auto count=ReadCompactSize(r);Require(count<=MAX_RECORDS&&count<=r.remaining()/14,Code::Metadata,"cursor-count");
    for(uint64_t i=0;i<count;++i){uint32_t account;uint8_t purpose,version;Cursor c;r>>account>>purpose>>version>>c.group>>c.next;
        Require(purpose<=2&&(version==3||(version==2&&purpose<2))&&c.next<=(version==3?64:0),Code::Metadata,"bad-cursor");
        Require(m_cursors.emplace(Branch{account,purpose,version},c).second,Code::Metadata,"duplicate-cursor");}
    count=ReadCompactSize(r);Require(count<=MAX_RECORDS&&count<=r.remaining()/2000,Code::Metadata,"record-count");
    for(uint64_t i=0;i<count;++i){Address a;r>>a.location>>a.pk>>a.nonce;auto script=ReadBytes(r,40);a.script=CScript(script.begin(),script.end());m_addresses.push_back(a);}
    Require(r.remaining()==0,Code::Metadata,"trailing-wallet-metadata");Reindex();
    if(!m_watch)for(const auto& a:m_addresses){auto p=a.location;auto it=m_cursors.find({p.account,p.purpose,p.witness});
        Require(it!=m_cursors.end()&&(p.witness==2?it->second.group>p.group:(it->second.group>p.group||(it->second.group==p.group&&it->second.next>p.address))),Code::Metadata,"missing-issued-range");}
    Require(Metadata()==Bytes(bytes.begin(),bytes.end()),Code::Metadata,"noncanonical-wallet-metadata");
}
Bytes Wallet::Backup()const
{
    std::lock_guard lock(m_mutex);Writer w;w<<Bytes{'M','W','R','W',1}<<m_master<<m_encrypted<<Metadata()<<m_mac;return std::move(w.bytes);
}
std::unique_ptr<Wallet> Wallet::Restore(std::span<const unsigned char> bytes,B32 genesis)
{
    Require(bytes.size()<=SNAPSHOT_LIMIT,Code::Metadata,"wallet-snapshot-limit");Reader r{bytes};
    Require(ReadBytes(r,5)==Bytes({'M','W','R','W',1}),Code::Version,"research-wallet-magic");
    auto w=std::unique_ptr<Wallet>(new Wallet(genesis));
    w->m_master.vchCryptedKey=ReadBytes(r,64);w->m_master.vchSalt=ReadBytes(r,8);
    r>>w->m_master.nDerivationMethod>>w->m_master.nDeriveIterations;w->m_master.vchOtherDerivationParameters=ReadBytes(r,0);
    w->m_encrypted.ciphertext=ReadBytes(r,64);r>>w->m_encrypted.seed_check;
    auto metadata=ReadBytes(r,SNAPSHOT_LIMIT);r>>w->m_mac;Require(r.remaining()==0,Code::Metadata,"trailing-wallet-snapshot");w->ParseMetadata(metadata);
    if(w->m_watch){Require(w->m_master.vchCryptedKey.empty()&&w->m_encrypted.ciphertext.empty()&&TimingResistantEqual(w->m_mac,PQH384("MercaWeave/ResearchWatchMetadata/v1",w->Metadata())),Code::Metadata,"watch-wallet-envelope-or-checksum");w->m_verified=true;}
    else Require(w->m_master.vchCryptedKey.size()==48&&w->m_master.vchSalt.size()==8&&w->m_master.nDerivationMethod==0&&w->m_master.nDeriveIterations>=25000&&w->m_master.nDeriveIterations<=1000000&&w->m_encrypted.ciphertext.size()==48,Code::Metadata,"seed-envelope");
    return w;
}
std::filesystem::path Wallet::StorageRoot(){return MERCAWEAVE_RESEARCH_STORAGE_ROOT;}
void Wallet::Save(const std::filesystem::path& path)const
{
    std::filesystem::create_directories(StorageRoot());std::filesystem::permissions(StorageRoot(),std::filesystem::perms::owner_all);
    auto p=SafePath(path);auto temp=p;temp+=".tmp";Require(!std::filesystem::is_symlink(temp),Code::Metadata,"snapshot-symlink");
    auto bytes=Backup();{std::ofstream stream(temp,std::ios::binary|std::ios::trunc);Require(bool(stream),Code::Metadata,"snapshot-open");stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());Require(bool(stream),Code::Metadata,"snapshot-write");}
    std::filesystem::permissions(temp,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write);std::filesystem::rename(temp,p);
}
std::unique_ptr<Wallet> Wallet::Load(const std::filesystem::path& path,B32 genesis)
{
    auto p=SafePath(path);Require(std::filesystem::file_size(p)<=SNAPSHOT_LIMIT,Code::Metadata,"snapshot-size");
    std::ifstream stream(p,std::ios::binary);Bytes bytes((std::istreambuf_iterator<char>(stream)),{});Require(bool(stream)||stream.eof(),Code::Metadata,"snapshot-read");return Restore(bytes,genesis);
}
std::string Wallet::EncodeResearchAddress(const Address& a,std::string_view hrp)
{
    Require(hrp=="mca"||hrp=="tmca"||hrp=="tb"||hrp=="mcrt",Code::Network,"unsupported-native-address-hrp");
    int version;Bytes program;Require(a.script.IsWitnessProgram(version,program)&&(version==2||version==3)&&program.size()==32,Code::Ownership,"native-address-program");
    std::vector<unsigned char> data{static_cast<unsigned char>(version)};
    ConvertBits<8,5,true>([&](unsigned char c){data.push_back(c);},program.begin(),program.end());
    return bech32::Encode(bech32::Encoding::BECH32M,std::string(hrp),data);
}
} // namespace mercaweave::wallet_research
