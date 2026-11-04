// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <wallet/pqgroup.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <chainparams.h>
#include <crypto/hmac_sha3_384.h>
#include <script/pqgroup_psbt.h>
#include <support/cleanse.h>
#include <util/strencodings.h>

namespace wallet {
namespace {
using namespace mercaweave;
struct Wipe { void* data; size_t size; ~Wipe() { memory_cleanse(data,size); } };
B32 Network()
{
    B32 value;
    const auto& hash = Params().GetConsensus().hashGenesisBlock;
    std::copy(hash.begin(),hash.end(),value.begin()); return value;
}
PQGroupRecord DeriveRecord(std::span<const unsigned char> seed, B32 genesis,
                           uint32_t account, uint8_t purpose, uint32_t group, uint32_t address)
{
    if (seed.size() != 32) throw Failure(Error::Context);
    B32 master; std::copy(seed.begin(),seed.end(),master.begin()); Wipe clean_master{master.data(),master.size()};
    auto child = Derive(master,genesis,account,purpose,group); Wipe clean_child{child.data(),child.size()};
    mercaweave::KeyPair key(child);
    return {account,purpose,group,address,key.pk,Derive(master,genesis,account,purpose,group,address)};
}
}
bool PQGroupRecords::StructurallyValid() const
{
    if (version != 1 || derivation != 1 || issued.size() > MAX_RECORDS) return false;
    std::map<std::pair<uint32_t,uint8_t>,uint32_t> next;
    std::set<CScript> scripts;
    for (const auto& record : issued) {
        if (record.purpose > 2 || record.address >= 64) return false;
        auto& sequence = next[{record.account,record.purpose}];
        if (record.group != sequence / 64 || record.address != sequence % 64 ||
            !scripts.insert(record.Script(genesis)).second) return false;
        ++sequence;
    }
    return true;
}
mercaweave::H48 PQGroupRecords::Authenticate(std::span<const unsigned char> seed) const
{
    static constexpr std::string_view DOMAIN{"Mercatura/PQGroupCWalletRecords/v1"};
    mercaweave::Writer metadata;
    metadata << version << derivation << genesis << issued;
    mercaweave::H48 result;
    CHMAC_SHA3_384(seed.data(),seed.size())
        .Write({reinterpret_cast<const unsigned char*>(DOMAIN.data()),DOMAIN.size()})
        .Write(metadata.bytes).Finalize(result);
    return result;
}
bool CWallet::AuthenticatePQGroups(const CKeyingMaterial& seed)
{
    AssertLockHeld(cs_wallet);
    if (!m_pq_group_records) return true;
    m_pq_group_verified = false; m_pq_group_index.clear();
    const auto& records = *m_pq_group_records;
    if (!m_group_execution || records.genesis != m_group_execution->Genesis() ||
        records.genesis != Network() || !records.StructurallyValid() || seed.size() != 32 ||
        !TimingResistantEqual(records.Authenticate(seed),records.mac)) return false;
    try {
        for (size_t i = 0; i < records.issued.size(); ++i) {
            const auto& record = records.issued[i];
            if (DeriveRecord(seed,records.genesis,record.account,record.purpose,record.group,record.address) != record)
                return false;
            m_pq_group_index.emplace(record.Script(records.genesis),i);
        }
    } catch (const mercaweave::Failure&) { m_pq_group_index.clear(); return false; }
    m_pq_group_verified = true;
    MarkDirty();
    return true;
}
bool CWallet::LoadPQGroupRecords(const PQGroupRecords& records)
{
    AssertLockHeld(cs_wallet);
    if (!m_group_execution || m_pq_group_records || !records.StructurallyValid() ||
        records.genesis != m_group_execution->Genesis() || records.genesis != Network() ||
        !HasMercaturaPQState()) return false;
    m_pq_group_records = records;
    // The record contains issued ranges, not a trustworthy creation timestamp.
    // A conservative birthday preserves rescan recovery of every known range.
    MaybeUpdateBirthTime(0);
    // Encrypted reload exposes no provisional ownership until native unlock
    // authenticates every record. Once authenticated, locking retains the
    // public ownership index without retaining any private key material.
    return m_mercatura_pq_master_seed.empty() || AuthenticatePQGroups(m_mercatura_pq_master_seed);
}
std::optional<PQGroupRecord> CWallet::IssuePQGroupAddress(uint32_t account, uint8_t purpose)
{
    LOCK(cs_wallet);
    if (!m_group_execution || m_group_execution->Genesis() != Network() || purpose > 2 ||
        IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS) || m_mercatura_pq_master_seed.size() != 32) return {};
    if (m_pq_group_records && !m_pq_group_verified) return {};
    PQGroupRecords staged = m_pq_group_records.value_or(PQGroupRecords{});
    staged.genesis = Network();
    if (staged.issued.size() >= PQGroupRecords::MAX_RECORDS) return {};
    uint32_t sequence{0};
    for (const auto& record : staged.issued) if (record.account == account && record.purpose == purpose) ++sequence;
    auto record = DeriveRecord(m_mercatura_pq_master_seed,staged.genesis,account,purpose,sequence / 64,sequence % 64);
    staged.issued.push_back(record);
    staged.mac = staged.Authenticate(m_mercatura_pq_master_seed);
    // Allocate the complete public index before committing durable metadata.
    // A failed allocation must not leave a committed issued record without its
    // in-memory ownership entry, or consume an address while returning failure.
    auto staged_index = m_pq_group_index;
    if (!staged_index.emplace(record.Script(staged.genesis),staged.issued.size()-1).second) return {};
    WalletBatch batch(GetDatabase());
    if (!batch.TxnBegin()) return {};
    if (!batch.WritePQGroupRecords(staged) || !batch.TxnCommit()) { batch.TxnAbort(); return {}; }
    m_pq_group_records = std::move(staged);
    m_pq_group_index.swap(staged_index);
    m_pq_group_verified = true;
    MaybeUpdateBirthTime(GetTime());
    return record;
}
std::optional<PQGroupRecord> CWallet::GetPQGroupRecord(const CScript& script) const
{
    LOCK(cs_wallet);
    if (!m_group_execution || !m_pq_group_verified || !m_pq_group_records ||
        m_group_execution->Genesis() != m_pq_group_records->genesis) return {};
    const auto found = m_pq_group_index.find(script);
    return found == m_pq_group_index.end() ? std::nullopt : std::optional{m_pq_group_records->issued[found->second]};
}
std::vector<CScript> CWallet::GetPQGroupScripts() const
{
    LOCK(cs_wallet);
    std::vector<CScript> result;
    if (m_group_execution && m_pq_group_verified && m_pq_group_records &&
        m_group_execution->Genesis() == m_pq_group_records->genesis)
        for (const auto& [script,index] : m_pq_group_index) result.push_back(script);
    return result;
}
std::optional<mercaweave::Sizes> CWallet::EstimatePQGroupTransaction(
    const CTransaction& tx, const std::vector<CTxOut>& spent) const
{
    LOCK(cs_wallet);
    if (!m_group_execution || tx.vin.size() != spent.size()) return {};
    CMutableTransaction model(tx);
    std::map<mercaweave::PublicKey,uint32_t> anchors;
    bool grouped{false};
    for (uint32_t i = 0; i < spent.size(); ++i) {
        int version; mercaweave::Bytes program;
        if (!spent[i].scriptPubKey.IsWitnessProgram(version,program) || program.size() != 32) return {};
        model.vin[i].scriptSig.clear();
        if (version == 3) {
            const auto record = GetPQGroupRecord(spent[i].scriptPubKey);
            if (!record) return {};
            grouped = true;
            const auto [anchor,first] = anchors.emplace(record->pk,i);
            if (first) model.vin[i].scriptWitness.stack = {mercaweave::Bytes(3309),
                mercaweave::Bytes(record->pk.begin(),record->pk.end()),mercaweave::Bytes(record->nonce.begin(),record->nonce.end())};
            else { mercaweave::Writer ref; ref << anchor->second;
                model.vin[i].scriptWitness.stack = {mercaweave::Bytes(record->nonce.begin(),record->nonce.end()),ref.bytes}; }
        } else if (version == 2) model.vin[i].scriptWitness.stack = {mercaweave::Bytes(3309),mercaweave::Bytes(1952)};
        else return {};
    }
    if (!grouped) return {};
    try { mercaweave::Reconstruct(model,spent); return mercaweave::Measure(model); }
    catch (const mercaweave::Failure&) { return {}; }
}
} // namespace wallet
