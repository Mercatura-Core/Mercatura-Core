// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/job.h>

#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <crypto/common.h>
#include <hash.h>
#include <streams.h>
#include <util/strencodings.h>

#include <algorithm>
#include <stdexcept>

namespace pool {
namespace {
std::vector<unsigned char> StrictHex(const std::string& hex)
{
    if (hex.size() % 2 || (!hex.empty() && !IsHex(hex))) throw std::invalid_argument("invalid hexadecimal bytes");
    return ParseHex(hex);
}
void PopulateTransactions(CBlock& block, const UniValue& t)
{
    block.vtx.emplace_back();
    if (!t["transactions"].isArray()) throw std::runtime_error("invalid template transactions");
    for (const auto& entry : t["transactions"].getValues()) {
        auto data = StrictHex(entry["data"].get_str());
        SpanReader reader{data};
        CMutableTransaction tx;
        reader >> TX_WITH_WITNESS(tx);
        if (!reader.empty()) throw std::runtime_error("trailing template transaction bytes");
        auto ref = MakeTransactionRef(std::move(tx));
        if (ref->GetHash().GetHex() != entry["txid"].get_str() || ref->GetWitnessHash().GetHex() != entry["hash"].get_str()) throw std::runtime_error("template transaction identity mismatch");
        block.vtx.push_back(std::move(ref));
    }
}
CScript WitnessCommitment(const CBlock& b)
{
    const uint256 reserved;
    const auto root = BlockWitnessMerkleRoot(b);
    const auto hash = Hash(root, reserved);
    std::vector<unsigned char> bytes{0x6a, 0x24, 0xaa, 0x21, 0xa9, 0xed};
    bytes.insert(bytes.end(), hash.begin(), hash.end());
    return CScript{bytes.begin(), bytes.end()};
}
} // namespace
template <typename T>
std::string SerializeHex(const T& object)
{
    std::vector<unsigned char> bytes;
    VectorWriter{bytes, 0, object};
    return HexStr(bytes);
}
std::string BlockHex(const CBlock& b) { return SerializeHex(TX_WITH_WITNESS(b)); }
std::string HeaderHex(const CBlockHeader& h) { return SerializeHex(h); }
CBlock ParseBlock(const std::string& hex)
{
    auto data = StrictHex(hex);
    SpanReader reader{data};
    CBlock b;
    reader >> TX_WITH_WITNESS(b);
    if (!reader.empty()) throw std::invalid_argument("trailing block data");
    return b;
}
CScript PqScript(const std::string& hex)
{
    auto bytes = StrictHex(hex);
    CScript script{bytes.begin(), bytes.end()};
    int version = 0;
    std::vector<unsigned char> program;
    if (!script.IsWitnessProgram(version, program) || version != 2 || program.size() != 32 || bytes.size() != 34) throw std::invalid_argument("native witness-v2 PQ script required");
    return script;
}
arith_uint256 ParseTarget(const std::string& hex)
{
    auto value = uint256::FromHex(hex);
    if (!value) throw std::invalid_argument("target must be 32 big-endian bytes");
    return UintToArith256(*value);
}
arith_uint256 TemplateTarget(const UniValue& t)
{
    const auto bits = t["bits"].get_str();
    if (bits.size() != 8 || !IsHex(bits)) throw std::runtime_error("invalid template nBits");
    const auto compact = static_cast<uint32_t>(Number::Parse(bits, true).Uint64());
    bool negative = false, overflow = false;
    arith_uint256 target;
    target.SetCompact(compact, &negative, &overflow);
    if (negative || overflow || target == 0 || target != ParseTarget(t["target"].get_str())) throw std::runtime_error("invalid template target");
    return target;
}
CScript CoinbaseScript(int height, uint64_t session, uint64_t job)
{
    if (height <= 0) throw std::invalid_argument("invalid coinbase height");
    CScript script;
    script << height;
    script << std::vector<unsigned char>{MARKER.begin(), MARKER.end()};
    std::vector<unsigned char> extra(16);
    WriteLE64(extra.data(), session);
    WriteLE64(extra.data() + 8, job);
    script << extra;
    if (script.size() < 2 || script.size() > 100) throw std::runtime_error("coinbase script size invalid");
    return script;
}
bool HasPoolMarker(const CScript& script, int height)
{
    if (height <= 0 || script.size() > 100) return false;
    CScript prefix;
    prefix << height;
    if (script.size() < prefix.size() || !std::equal(prefix.begin(), prefix.end(), script.begin())) return false;
    auto pc = script.begin() + prefix.size();
    opcodetype op;
    std::vector<unsigned char> value;
    if (!script.GetOp(pc, op, value) || op != static_cast<opcodetype>(MARKER.size()) || std::string{value.begin(), value.end()} != MARKER) return false;
    if (!script.GetOp(pc, op, value) || op != static_cast<opcodetype>(16) || value.size() != 16 || pc != script.end()) return false;
    return true;
}
size_t RequiredBlockBytes(const UniValue& t, size_t count)
{
    CBlock b;
    PopulateTransactions(b, t);
    CMutableTransaction cb;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CoinbaseScript(t["height"].getInt<int>(), 1, 1);
    cb.vin[0].scriptWitness.stack.emplace_back(32, 0);
    std::vector<unsigned char> program(32, 0);
    CScript pq;
    pq << OP_2 << program;
    for (size_t i = 0; i < count; ++i)
        cb.vout.emplace_back(0, pq);
    cb.vout.emplace_back(0, WitnessCommitment(b));
    b.vtx[0] = MakeTransactionRef(std::move(cb));
    return GetSerializeSize(TX_WITH_WITNESS(b));
}
Job BuildJob(const UniValue& t, const Window& w, int64_t cutoff, const Number& window_limit,
             const std::string& network, const std::string& genesis, int64_t session, int64_t serial,
             const std::string& target, int64_t now, int64_t lifetime, bool warmup)
{
    if (w.total.Zero()) throw std::runtime_error("empty PPLNS window: shared work unavailable");
    if (session <= 0 || serial <= 0 || lifetime <= 0) throw std::invalid_argument("invalid job namespace/lifetime");
    if (!t["coinbaseaux"].isObject() || !t["coinbaseaux"].empty()) throw std::runtime_error("unsupported nonempty coinbaseaux");
    const auto reward = t["coinbasevalue"].getInt<int64_t>();
    if (!MoneyRange(reward) || reward == 0) throw std::runtime_error("invalid template reward");
    Job j;
    j.session = session;
    j.serial = serial;
    j.issued = now;
    j.expires = now + lifetime;
    j.share_target = ParseTarget(target);
    j.network_target = TemplateTarget(t);
    // Assigned target must admit every network solution. Target zero is valid
    // mathematically, but would be an unusable operational target here.
    if (j.share_target < j.network_target) throw std::runtime_error("share target is harder than network target");
    const auto template_id = Digest("MCA-POOL/1/template", t.write());
    const auto seed = Digest("MCA-PPLNS/1/seed", network + ":" + genesis + ":" + t["previousblockhash"].get_str() + ":" + template_id + ":" + std::to_string(cutoff) + ":" + window_limit.Decimal() + ":" + std::to_string(reward));
    auto allocations = Allocate(reward, w, seed);
    if (allocations.size() > 1000) throw std::runtime_error("pool recipient launch limit exceeded");
    j.manifest.setObject();
    j.manifest.pushKV("accounting_version", ACCOUNTING_VERSION);
    j.manifest.pushKV("network", network);
    j.manifest.pushKV("genesis", genesis);
    j.manifest.pushKV("parent", t["previousblockhash"]);
    j.manifest.pushKV("height", t["height"]);
    j.manifest.pushKV("template_id", template_id);
    j.manifest.pushKV("template", t);
    j.manifest.pushKV("reward", reward);
    j.manifest.pushKV("cutoff", cutoff);
    j.manifest.pushKV("reference_work", WorkScore(Number::Parse(j.network_target.GetHex(), true)).Decimal());
    j.manifest.pushKV("window_limit", window_limit.Decimal());
    j.manifest.pushKV("total_work", w.total.Decimal());
    j.manifest.pushKV("oldest_sequence", w.oldest);
    j.manifest.pushKV("oldest_used", w.oldest_used.Decimal());
    j.manifest.pushKV("rounding", "largest-remainder/domain-sha256d-v1");
    j.manifest.pushKV("rounding_seed", seed);
    j.manifest.pushKV("allocations", AllocationsJson(allocations));
    j.manifest.pushKV("pool_name", "Mercatura Pool");
    j.manifest.pushKV("marker", std::string{MARKER});
    j.manifest.pushKV("fee_base_units", 0);
    j.manifest.pushKV("warmup", warmup);
    j.snapshot_manifest = j.manifest;
    j.snapshot = Digest("MCA-POOL/1/snapshot", j.snapshot_manifest.write());
    CBlock& b = j.block;
    b.nVersion = t["version"].getInt<int32_t>();
    const auto parent = uint256::FromHex(t["previousblockhash"].get_str());
    if (!parent) throw std::runtime_error("invalid template parent");
    b.hashPrevBlock = *parent;
    b.nBits = static_cast<uint32_t>(Number::Parse(t["bits"].get_str(), true).Uint64());
    b.nTime = std::max(t["curtime"].getInt<uint32_t>(), t["mintime"].getInt<uint32_t>());
    b.nNonce = 0;
    PopulateTransactions(b, t);
    CMutableTransaction cb;
    cb.version = 2;
    cb.nLockTime = 0;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    cb.vin[0].scriptSig = CoinbaseScript(t["height"].getInt<int>(), session, serial);
    cb.vin[0].scriptWitness.stack.emplace_back(32, 0);
    for (const auto& allocation : allocations)
        if (allocation.amount > 0) cb.vout.emplace_back(allocation.amount, PqScript(allocation.script));
    auto commitment = WitnessCommitment(b);
    if (HexStr(commitment) != t["default_witness_commitment"].get_str()) throw std::runtime_error("witness commitment mismatch");
    cb.vout.emplace_back(0, std::move(commitment));
    if (CTransaction{cb}.GetValueOut() != reward) throw std::logic_error("coinbase conservation failure");
    b.vtx[0] = MakeTransactionRef(std::move(cb));
    bool mutation = false;
    b.hashMerkleRoot = BlockMerkleRoot(b, &mutation);
    if (mutation) throw std::runtime_error("mutated template merkle tree");
    const auto bytes = GetSerializeSize(TX_WITH_WITNESS(b));
    if (bytes > t["sizelimit"].getInt<uint64_t>() || bytes > GetMaxBlockCapacityBytes(t["height"].getInt<int>())) throw std::runtime_error("multi-recipient block exceeds template byte budget");
    j.manifest.pushKV("snapshot_id", j.snapshot);
    j.manifest.pushKV("coinbase_txid", b.vtx[0]->GetHash().GetHex());
    j.manifest.pushKV("serialized_bytes", static_cast<uint64_t>(bytes));
    j.id = Digest("MCA-POOL/1/job", j.snapshot + ":" + std::to_string(session) + ":" + std::to_string(serial) + ":" + HeaderHex(b) + ":" + j.share_target.GetHex() + ":" + std::to_string(now));
    return j;
}
UniValue Job::Message() const
{
    UniValue o{UniValue::VOBJ};
    o.pushKV("job_id", id);
    o.pushKV("snapshot_id", snapshot);
    o.pushKV("warmup", manifest["warmup"]);
    o.pushKV("fee_base_units", 0);
    o.pushKV("header", HeaderHex(block));
    o.pushKV("coinbase", SerializeHex(TX_WITH_WITNESS(*block.vtx[0])));
    o.pushKV("share_target", share_target.GetHex());
    o.pushKV("network_target", network_target.GetHex());
    o.pushKV("expires", expires);
    o.pushKV("parent", block.hashPrevBlock.GetHex());
    o.pushKV("extranonce_namespace", std::to_string(session));
    UniValue path{UniValue::VARR};
    for (const auto& h : TransactionMerklePath(block, 0))
        path.push_back(h.GetHex());
    o.pushKV("merkle_path", path);
    return o;
}
} // namespace pool
