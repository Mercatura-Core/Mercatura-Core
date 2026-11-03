// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "durable_wallet.h"
#include <crypto/hmac_sha3_384.h>
#include <crypto/sha256.h>
#include <util/translation.h>
#include <limits>
#include <sqlite3.h>
namespace mercaweave::durable_research {
namespace {
constexpr std::string_view KEY{"mercaweave/wo07/state"};
constexpr size_t MAX_STATE{16*1024*1024};
struct Bounded {
    Bytes bytes;
    template<class S> void Serialize(S& s) const { ::Serialize(s,bytes); }
    template<class S> void Unserialize(S& s) { const auto n=ReadCompactSize(s);Require(n<=MAX_STATE,Code::Metadata,"research database record bound");bytes.resize(n);s.read(std::as_writable_bytes(std::span(bytes))); }
};
Bytes Blob(Reader& r) {const auto n=ReadCompactSize(r);Require(n<=MAX_STATE&&n<=r.remaining(),Code::Metadata,"research blob bound");Bytes b(n);r.read(std::as_writable_bytes(std::span(b)));return b;}
H48 Mac(const wallet::CKeyingMaterial& seed, const Bytes& bytes)
{
    static constexpr std::string_view domain{"Mercatura/MercaWeaveResearchJournal/v1"};
    H48 out;CHMAC_SHA3_384{seed.data(),seed.size()}.Write(std::span{reinterpret_cast<const unsigned char*>(domain.data()),domain.size()}).Write(bytes).Finalize(out);return out;
}
struct Envelope {Bytes payload,backup;H48 mac;};
Envelope Decode(const Bytes& sealed, const B32& expected)
{
    Reader outer{sealed};Envelope e;e.payload=Blob(outer);outer>>e.mac;Require(!outer.remaining(),Code::Metadata,"research record trailing bytes");
    Reader r{e.payload};uint32_t version;B32 genesis;r>>version>>genesis;Require(version==1,Code::Version,"research schema");Require(genesis==expected,Code::Network,"research database genesis");e.backup=Blob(r);return e;
}
bool Active(const Entry& entry){return entry.status==Status::Reserved||entry.status==Status::SimulatedAccepted;}
B32 Body(const reward_research::Proposal& proposal)
{
    Writer w;WriteCompactSize(w,proposal.transactions.size());for(const auto& funded:proposal.transactions){Require(bool(funded.psbt.tx),Code::Metadata,"missing proposal body");w<<funded.psbt.tx->GetHash();}
    B32 result;CSHA256{}.Write(w.bytes.data(),w.bytes.size()).Finalize(result.data());return result;
}
std::filesystem::path CheckedRoot()
{
    const auto root=std::filesystem::absolute(Store::Root()).lexically_normal();
    std::filesystem::path path;
    for(const auto& part:root){path/=part;Require(!std::filesystem::is_symlink(std::filesystem::symlink_status(path)),Code::Metadata,"research root symlink");}
    return root;
}
void Name(std::string_view name){Require(!name.empty()&&name.size()<100&&std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';}),Code::Metadata,"isolated database name");}
}
std::filesystem::path Store::Root(){return MERCAWEAVE_DURABLE_STORAGE_ROOT;}
Store::Store(std::string_view name,B32 genesis):m_genesis(genesis)
{
    Name(name);const auto root=CheckedRoot();
    const auto dir=root/std::string(name);Require(!std::filesystem::is_symlink(std::filesystem::symlink_status(dir)),Code::Metadata,"research database symlink");
    Require(!std::filesystem::is_symlink(std::filesystem::symlink_status(dir/"wallet.dat")),Code::Metadata,"research file symlink");
    std::filesystem::create_directories(dir);std::filesystem::permissions(root,std::filesystem::perms::owner_all);std::filesystem::permissions(dir,std::filesystem::perms::owner_all);
    m_database=std::make_unique<wallet::SQLiteDatabase>(dir,dir/"wallet.dat",wallet::DatabaseOptions{});
    sqlite3_limit(m_database->m_db,SQLITE_LIMIT_LENGTH,MAX_STATE+1024);
    std::filesystem::permissions(dir/"wallet.dat",std::filesystem::perms::owner_read|std::filesystem::perms::owner_write);
    bilingual_str error;Require(m_database->Verify(error),Code::Metadata,"native SQLite verification");
}
Bytes Store::Encode(const Wallet& wallet,const std::vector<Entry>& entries,uint64_t next)const
{
    Require(entries.size()<=MAX_ENTRIES,Code::Metadata,"journal entry bound");Bytes result;
    const auto seal=[&](const wallet::CKeyingMaterial& seed){
        Writer payload;payload<<uint32_t{1}<<m_genesis<<wallet.Backup()<<next;WriteCompactSize(payload,entries.size());
        for(const auto& entry:entries){payload<<entry.id<<entry.body<<uint8_t(entry.status);WriteCompactSize(payload,entry.inputs.size());for(const auto& point:entry.inputs)payload<<point;}
        Writer sealed;sealed<<payload.bytes<<Mac(seed,payload.bytes);Require(sealed.bytes.size()<=MAX_STATE,Code::Metadata,"journal state bound");result=std::move(sealed.bytes);return true;
    };
    if(wallet.IsWatchOnly()){
        Require(entries.empty()&&next==1,Code::WatchOnly,"watch database cannot contain reservations");
        seal(wallet::CKeyingMaterial{}); // public integrity only; never ownership
    }else Require(wallet.WithMercaturaPQMasterSeed(seal),Code::Locked,"checkpoint requires explicit unlocked seed owner");
    return result;
}
void Store::Commit(const Bytes& sealed)
{
    auto batch=m_database->MakeBatch();Require(batch->TxnBegin(),Code::Metadata,"native database transaction begin");
    Require(batch->Write(std::string(KEY),Bounded{sealed})&&batch->TxnCommit(),Code::Metadata,"native atomic research checkpoint");
}
std::unique_ptr<Store> Store::Create(std::string_view name,std::unique_ptr<Wallet> wallet)
{
    Name(name);CheckedRoot();Require(bool(wallet),Code::Metadata,"missing wallet");Require(!std::filesystem::exists(Root()/std::string(name)),Code::Metadata,"refuse existing database");
    std::filesystem::create_directories(Root());
    Require(std::filesystem::create_directory(Root()/std::string(name)),Code::Metadata,"research directory already claimed");
    auto store=std::unique_ptr<Store>(new Store(name,wallet->Genesis()));store->m_wallet=std::move(wallet);
    auto sealed=store->Encode(*store->m_wallet,{},1);store->Commit(sealed);store->m_sealed=std::move(sealed);store->m_authenticated=true;return store;
}
std::unique_ptr<Store> Store::Load(std::string_view name,B32 genesis)
{
    Name(name);CheckedRoot();Require(std::filesystem::is_regular_file(Root()/std::string(name)/"wallet.dat"),Code::Metadata,"research database absent");
    auto store=std::unique_ptr<Store>(new Store(name,genesis));Bounded sealed;
    auto batch=store->m_database->MakeBatch();Require(batch->Read(std::string(KEY),sealed),Code::Metadata,"missing research state");
    auto envelope=Decode(sealed.bytes,genesis);store->m_wallet=Wallet::Restore(envelope.backup,genesis);store->m_sealed=std::move(sealed.bytes);
    if(store->m_wallet->IsWatchOnly()){
        Require(Mac(wallet::CKeyingMaterial{},envelope.payload)==envelope.mac,Code::Metadata,"watch public record integrity");
        Reader r{envelope.payload};uint32_t version;B32 network;uint64_t next;r>>version>>network;Blob(r);r>>next;
        Require(next==1&&ReadCompactSize(r)==0&&!r.remaining(),Code::Metadata,"watch database has ownership journal");store->m_authenticated=true;
    }
    return store;
}
bool Store::Unlock(const SecureString& pass)
{
    std::lock_guard lock{m_mutex};if(!m_wallet->Unlock(pass))return false;
    try {
        auto e=Decode(m_sealed,m_genesis);H48 mac{};
        Require(m_wallet->WithMercaturaPQMasterSeed([&](const wallet::CKeyingMaterial& seed){mac=Mac(seed,e.payload);return true;}),Code::Locked,"journal authentication seed");
        unsigned char diff{0};for(size_t i=0;i<mac.size();++i)diff|=mac[i]^e.mac[i];Require(diff==0,Code::Metadata,"research journal authentication");
        Reader r{e.payload};uint32_t version;B32 genesis;r>>version>>genesis;Blob(r);uint64_t next;r>>next;const auto count=ReadCompactSize(r);Require(next>0&&count<=MAX_ENTRIES,Code::Metadata,"journal bounds");
        std::vector<Entry> entries;std::set<uint64_t> ids;std::set<B32> bodies;std::set<COutPoint> reserved;
        for(size_t i=0;i<count;++i){Entry entry;uint8_t status;r>>entry.id>>entry.body>>status;Require(entry.id>0&&entry.id<next&&status<=uint8_t(Status::Conflicted)&&ids.insert(entry.id).second&&bodies.insert(entry.body).second,Code::Metadata,"journal identity");entry.status=Status(status);auto n=ReadCompactSize(r);Require(n>0&&n<=MAX_INPUTS,Code::Metadata,"reservation input bound");for(size_t j=0;j<n;++j){COutPoint point;r>>point;Require(entry.inputs.insert(point).second,Code::Metadata,"duplicate reserved input");if(Active(entry))Require(reserved.insert(point).second,Code::Metadata,"overlapping reservations");}entries.push_back(std::move(entry));}
        Require(!r.remaining(),Code::Metadata,"journal trailing payload");m_entries=std::move(entries);m_next=next;m_authenticated=true;return true;
    }catch(...){m_wallet->Lock();m_authenticated=false;m_entries.clear();return false;}
}
void Store::Checkpoint(){std::lock_guard lock{m_mutex};Require(m_authenticated,Code::Metadata,"journal not authenticated");auto bytes=Encode(*m_wallet,m_entries,m_next);Commit(bytes);m_sealed=std::move(bytes);}
Entry Store::Reserve(const reward_research::Proposal& proposal)
{
    std::lock_guard lock{m_mutex};Require(m_authenticated&&!m_wallet->IsLocked(),Code::Locked,"reserved checkpoint requires unlocked owner");
    Require(!proposal.transactions.empty()&&proposal.inputs.size()>0&&proposal.inputs.size()<=MAX_INPUTS,Code::Metadata,"proposal bounds");
    const auto body=Body(proposal);for(const auto& entry:m_entries)if(entry.body==body)return entry;
    Require(m_entries.size()<MAX_ENTRIES&&m_next<UINT64_MAX,Code::Metadata,"journal exhausted");
    std::set<COutPoint> actual;for(const auto& transaction:proposal.transactions)for(const auto& in:transaction.psbt.tx->vin)Require(actual.insert(in.prevout).second,Code::Conflict,"duplicate proposal spend");
    Require(actual==proposal.inputs,Code::Metadata,"proposal reservation/body mismatch");
    for(const auto& entry:m_entries)if(Active(entry))for(const auto& point:proposal.inputs)Require(!entry.inputs.contains(point),Code::Conflict,"durably reserved input");
    auto entries=m_entries;entries.push_back({m_next,body,Status::Reserved,proposal.inputs});auto bytes=Encode(*m_wallet,entries,m_next+1);Commit(bytes);m_entries=std::move(entries);++m_next;m_sealed=std::move(bytes);return m_entries.back();
}
void Store::Mark(uint64_t id,Status status)
{
    std::lock_guard lock{m_mutex};Require(m_authenticated,Code::Metadata,"journal not authenticated");Require(status!=Status::Reserved,Code::Metadata,"cannot resurrect reservation");
    auto entries=m_entries;auto entry=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.id==id;});Require(entry!=entries.end()&&Active(*entry),Code::Conflict,"missing or terminal journal entry");entry->status=status;
    auto bytes=Encode(*m_wallet,entries,m_next);Commit(bytes);m_entries=std::move(entries);m_sealed=std::move(bytes);
}
void Store::Reconcile(const integration::ViewPtr& view)
{
    std::lock_guard lock{m_mutex};Require(m_authenticated&&view,Code::Metadata,"reconcile ownership");auto entries=m_entries;
    for(auto& entry:entries)if(Active(entry))for(const auto& point:entry.inputs)if(!view->CoinAt(point)){entry.status=Status::Conflicted;break;}
    auto bytes=Encode(*m_wallet,entries,m_next);Commit(bytes);m_entries=std::move(entries);m_sealed=std::move(bytes);
}
std::vector<Entry> Store::Entries()const{std::lock_guard lock{m_mutex};Require(m_authenticated&&!m_wallet->IsLocked(),Code::Locked,"journal unavailable while locked");return m_entries;}
bool Store::Backup(const std::filesystem::path& path)const
{
    std::lock_guard lock{m_mutex};CheckedRoot();Require(path.parent_path()==Root()&&path.filename().string().ends_with(".backup")&&!std::filesystem::exists(path),Code::Metadata,"isolated backup path");
    const bool success=m_database->Backup(path.string());if(success)std::filesystem::permissions(path,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write);return success;
}
}
