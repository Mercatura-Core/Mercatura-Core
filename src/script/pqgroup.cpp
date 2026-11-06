// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <script/pqgroup.h>

#include <crypto/hkdf_sha3_384.h>
#include <crypto/sha256.h>
#include <support/cleanse.h>
#include <streams.h>

#include <cstring>
#include <map>
#include <set>

namespace mercaweave {
namespace {
void Require(bool ok, Error e) { if (!ok) throw Failure(e); }
std::span<const unsigned char> AsBytes(std::string_view s)
{
    return {reinterpret_cast<const unsigned char*>(s.data()), s.size()};
}
template<size_t N> std::array<unsigned char, N> Fixed(const Bytes& b)
{
    Require(b.size() == N, Error::Witness);
    std::array<unsigned char, N> out;
    std::copy(b.begin(), b.end(), out.begin());
    return out;
}
struct Wipe { void* p; size_t n; ~Wipe() { memory_cleanse(p, n); } };
bool IsV3(const CScript& script)
{
    int version;
    Bytes program;
    return script.IsWitnessProgram(version, program) && version == 3;
}
void Basic(const CMutableTransaction& tx, const std::vector<CTxOut>& spent)
{
    Require(spent.size() == tx.vin.size(), Error::Context);
    Require(!tx.vin.empty() && !tx.vout.empty(), Error::Format);
    Require(tx.vin.size() <= UINT32_MAX, Error::Format);
    // The offline decoder bounds its own wire buffer. Normal validation uses
    // the chain's existing transaction, byte, witness and sigop budgets; a
    // standalone harness buffer ceiling must not become a consensus rule.
    std::set<COutPoint> seen;
    CAmount in{0}, out{0};
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        Require(Consensus::WitnessStackViolation(tx.vin[i].scriptWitness.stack) == nullptr, Error::Witness);
        Require(!tx.vin[i].prevout.IsNull(), Error::Format);
        Require(seen.insert(tx.vin[i].prevout).second, Error::DuplicateOutpoint);
        Require(MoneyRange(spent[i].nValue) && spent[i].nValue <= MAX_MONEY - in, Error::Amount);
        in += spent[i].nValue;
    }
    for (const auto& o : tx.vout) {
        Require(MoneyRange(o.nValue) && o.nValue <= MAX_MONEY - out, Error::Amount);
        out += o.nValue;
    }
    Require(out <= in, Error::Amount);
}
} // namespace

void Writer::write(std::span<const std::byte> src)
{
    Require(bytes.size() <= MAX_SIZE && src.size() <= MAX_SIZE - bytes.size(), Error::Encoding);
    if (src.empty()) return;
    // Stream source must not alias this writer's storage, as for Serialize.
    const auto old_size = bytes.size();
    bytes.resize(old_size + src.size());
    std::memcpy(bytes.data() + old_size, src.data(), src.size());
}
void Reader::read(std::span<std::byte> dst)
{
    const auto pos = m_pos;
    skip(dst.size());
    if (!dst.empty()) std::memcpy(dst.data(), m_bytes.data() + pos, dst.size());
}
void Reader::skip(size_t n)
{
    Require(n <= remaining(), Error::Encoding);
    m_pos += n;
}
Bytes Wire(const CMutableTransaction& tx, bool witness)
{
    Bytes bytes;
    VectorWriter writer{bytes, 0};
    if (witness) writer << TX_WITH_WITNESS(tx);
    else writer << TX_NO_WITNESS(tx);
    return bytes;
}
CScriptWitness DecodeWitness(std::span<const unsigned char> bytes)
{
    try {
        Reader r{bytes};
        const auto count = ReadCompactSize(r);
        Require(count == 2 || count == 3, Error::Witness);
        CScriptWitness w;
        for (size_t i = 0; i < count; ++i) {
            const auto size = ReadCompactSize(r);
            const size_t expected = count == 3 ? (i == 0 ? 3309 : i == 1 ? 1952 : 32) : (i == 0 ? 32 : 4);
            Require(size == expected && size <= r.remaining(), Error::Witness);
            Bytes item(size);
            r.read(std::as_writable_bytes(std::span{item}));
            w.stack.push_back(std::move(item));
        }
        Require(r.remaining() == 0, Error::Encoding);
        return w;
    } catch (const std::ios_base::failure&) { throw Failure(Error::Encoding); }
}
CMutableTransaction DecodeTransaction(std::span<const unsigned char> bytes)
{
    // This remains the existing 32 MiB in-memory research-harness budget.
    // Never feed attacker counts directly to a vector-allocating decoder.
    Require(bytes.size() <= MAX_SIZE, Error::Encoding);
    try {
        Reader r{bytes};
        r.skip(4);
        auto inputs = ReadCompactSize(r);
        bool witness{false};
        if (inputs == 0) {
            uint8_t flags;
            r >> flags;
            Require(flags == 1, Error::Encoding);
            witness = true;
            inputs = ReadCompactSize(r);
        }
        Require(inputs > 0 && inputs <= r.remaining() / 41, Error::Encoding);
        for (uint64_t i = 0; i < inputs; ++i) {
            r.skip(36);
            r.skip(ReadCompactSize(r));
            r.skip(4);
        }
        const auto outputs = ReadCompactSize(r);
        Require(outputs <= r.remaining() / 9, Error::Encoding);
        for (uint64_t i = 0; i < outputs; ++i) {
            r.skip(8);
            r.skip(ReadCompactSize(r));
        }
        if (witness) {
            bool any{false};
            for (uint64_t i = 0; i < inputs; ++i) {
                const auto count = ReadCompactSize(r);
                Require(count <= Consensus::MAX_WITNESS_STACK_ITEMS && count <= r.remaining(), Error::Encoding);
                any |= count != 0;
                for (uint64_t item = 0; item < count; ++item) {
                    const auto size = ReadCompactSize(r);
                    Require(size <= Consensus::MAX_WITNESS_ITEM_BYTES, Error::Encoding);
                    r.skip(size);
                }
            }
            Require(any, Error::Encoding);
        }
        r.skip(4);
        Require(r.remaining() == 0, Error::Encoding);
        Reader decode{bytes};
        CMutableTransaction tx;
        decode >> TX_WITH_WITNESS(tx);
        Require(decode.remaining() == 0, Error::Encoding);
        return tx;
    } catch (const std::ios_base::failure&) { throw Failure(Error::Encoding); }
}
B32 Commitment(const B32& genesis, std::span<const unsigned char> pk, const B32& nonce)
{
    Require(pk.size() == 1952, Error::Witness);
    constexpr std::string_view tag{"Mercatura/PQGroupKey/v1"};
    B32 th, out;
    auto t = AsBytes(tag);
    CSHA256().Write(t.data(), t.size()).Finalize(th.data());
    CSHA256().Write(th.data(), th.size()).Write(th.data(), th.size())
        .Write(genesis.data(), genesis.size()).Write(pk.data(), pk.size())
        .Write(nonce.data(), nonce.size()).Finalize(out.data());
    return out;
}
CScript Output(const B32& genesis, std::span<const unsigned char> pk, const B32& nonce)
{
    const auto c = Commitment(genesis, pk, nonce);
    CScript s;
    s << OP_3 << Bytes(c.begin(), c.end());
    return s;
}
Mapping Reconstruct(const CMutableTransaction& tx, const std::vector<CTxOut>& spent)
{
    Basic(tx, spent);
    Mapping map;
    std::vector<bool> v3(tx.vin.size(), false);
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        const auto& s = spent[i].scriptPubKey;
        int version;
        Bytes program;
        if (s.IsWitnessProgram(version, program) && version == 3) {
            Require(program.size() == 32, Error::Program);
            v3[i] = true;
            Require(++map.inputs <= MAX_INPUTS, Error::InputsLimit);
        } else if (s.IsPayToScriptHash()) {
            // Detect a v3 redeem program from scriptSig, never from witness shape.
            auto it = tx.vin[i].scriptSig.begin();
            opcodetype op{OP_INVALIDOPCODE};
            Bytes pushed;
            bool complete{true};
            while (it != tx.vin[i].scriptSig.end()) {
                if (!tx.vin[i].scriptSig.GetOp(it, op, pushed)) { complete = false; break; }
            }
            // Only the final pushed item is the redeem script. An earlier
            // ordinary argument may happen to contain witness-program bytes.
            if (complete && op <= OP_PUSHDATA4) {
                CScript redeem(pushed.begin(), pushed.end());
                Require(!IsV3(redeem), Error::Wrapped);
            }
        }
    }
    std::map<uint32_t, size_t> anchors;
    std::set<PublicKey> keys;
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        if (!v3[i]) continue;
        const auto idx = static_cast<uint32_t>(i);
        Require(tx.vin[i].scriptSig.empty(), Error::ScriptSig);
        const auto& w = tx.vin[i].scriptWitness.stack;
        if (w.size() == 3) {
            Require(w[0].size() == 3309 && w[1].size() == 1952 && w[2].size() == 32, Error::Witness);
            auto pk = Fixed<1952>(w[1]);
            Require(keys.insert(pk).second, Error::DuplicateKey);
            Require(map.groups.size() < MAX_GROUPS, Error::GroupsLimit);
            anchors.emplace(idx, map.groups.size());
            map.groups.push_back({idx, pk, {{idx, Fixed<32>(w[2])}}});
        } else {
            Require(w.size() == 2 && w[0].size() == 32 && w[1].size() == 4, Error::Witness);
            const uint32_t a = ReadLE32(w[1].data());
            Require(a < idx, Error::Reference);
            const auto found = anchors.find(a);
            Require(found != anchors.end(), Error::Reference);
            auto& group = map.groups[found->second];
            Require(group.members.size() < MAX_MEMBERS, Error::MembersLimit);
            group.members.push_back({idx, Fixed<32>(w[0])});
        }
    }
    return map;
}
void CheckOwnership(const Mapping& map, const std::vector<CTxOut>& spent, const B32& genesis, Statistics& stats)
{
    for (const auto& group : map.groups) for (const auto& member : group.members) {
        Require(member.index < spent.size(), Error::Context);
        ++stats.ownership_checks;
        Require(Output(genesis, group.pk, member.nonce) == spent[member.index].scriptPubKey, Error::Ownership);
    }
}
Bytes Manifest(const Mapping& map)
{
    Require(map.inputs <= MAX_INPUTS, Error::InputsLimit);
    Require(map.groups.size() <= MAX_GROUPS, Error::GroupsLimit);
    Writer w;
    WriteCompactSize(w, map.groups.size());
    std::set<uint32_t> members;
    std::set<PublicKey> keys;
    std::optional<uint32_t> last_anchor;
    for (const auto& group : map.groups) {
        Require(!last_anchor || *last_anchor < group.anchor, Error::Reference);
        Require(keys.insert(group.pk).second, Error::DuplicateKey);
        Require(!group.members.empty() && group.members.front().index == group.anchor, Error::Reference);
        Require(group.members.size() <= MAX_MEMBERS, Error::MembersLimit);
        last_anchor = group.anchor;
        w << group.anchor << group.pk;
        WriteCompactSize(w, group.members.size());
        std::optional<uint32_t> last;
        for (const auto& m : group.members) {
            Require(!last || *last < m.index, Error::Reference);
            Require(members.insert(m.index).second, Error::Reference);
            last = m.index;
            w << m.index << m.nonce;
        }
    }
    Require(members.size() == map.inputs, Error::Context);
    return std::move(w.bytes);
}
Hashes BodyHashes(const CMutableTransaction& tx, const std::vector<CTxOut>& spent, const B32& genesis)
{
    Require(spent.size() == tx.vin.size(), Error::Context);
    MercaturaPQHashWriter prev{"Mercatura/PQGroupAuth/v1/Prevouts"};
    MercaturaPQHashWriter amount{"Mercatura/PQGroupAuth/v1/Amounts"};
    MercaturaPQHashWriter script{"Mercatura/PQGroupAuth/v1/ScriptPubKeys"};
    MercaturaPQHashWriter seq{"Mercatura/PQGroupAuth/v1/Sequences"};
    MercaturaPQHashWriter out{"Mercatura/PQGroupAuth/v1/Outputs"};
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        prev << tx.vin[i].prevout;
        amount << spent[i].nValue;
        script << spent[i].scriptPubKey;
        seq << tx.vin[i].nSequence;
    }
    for (const auto& o : tx.vout) out << o;
    Hashes h{prev.GetHash(), amount.GetHash(), script.GetHash(), seq.GetHash(), out.GetHash(), {}, {}};
    MercaturaPQHashWriter body{"Mercatura/PQGroupAuth/v1/Tx"};
    body << tx.version << tx.nLockTime << uint64_t(tx.vin.size()) << uint64_t(tx.vout.size())
         << genesis << h.prev << h.amount << h.script << h.sequence << h.output;
    h.tx = body.GetHash();
    return h;
}
Hashes TransactionHashes(const CMutableTransaction& tx, const std::vector<CTxOut>& spent,
                         const B32& genesis, const Mapping& map)
{
    auto h = BodyHashes(tx, spent, genesis);
    h.manifest = PQH384("Mercatura/PQGroupAuth/v1/Manifest", Manifest(map));
    return h;
}
H48 Digest(const B32& genesis, const Hashes& h, const Group& group)
{
    MercaturaPQHashWriter w{"Mercatura/PQGroupAuth/v1/Final"};
    w << uint8_t{1} << genesis << h.tx << h.manifest << group.anchor << group.pk;
    return w.GetHash();
}
KeyPair::KeyPair(const B32& seed)
{
    if (!mercatura_mldsa65_keypair_from_seed(pk.data(), pk.size(), sk.data(), sk.size(), seed.data(), seed.size())) {
        memory_cleanse(sk.data(), sk.size());
        throw Failure(Error::Keygen);
    }
}
KeyPair::~KeyPair() { memory_cleanse(sk.data(), sk.size()); }
Signature SignDigest(const H48& digest, const KeyPair& key, const B32& randomness, std::string_view context)
{
    Signature sig;
    const auto c = AsBytes(context);
    Require(mercatura_mldsa65_sign(sig.data(), sig.size(), digest.data(), digest.size(), c.data(), c.size(),
                                 randomness.data(), randomness.size(), key.sk.data(), key.sk.size()), Error::Signing);
    return sig;
}
bool VerifyDigest(const H48& digest, std::span<const unsigned char> pk,
                  std::span<const unsigned char> sig, std::string_view context)
{
    const auto c = AsBytes(context);
    return mercatura_mldsa65_verify(sig.data(), sig.size(), digest.data(), digest.size(), c.data(), c.size(), pk.data(), pk.size());
}
void Sign(CMutableTransaction& tx, const std::vector<CTxOut>& spent, const B32& genesis,
          const std::vector<std::reference_wrapper<const KeyPair>>& keys, const RandomSource& random)
{
    const auto map = Reconstruct(tx, spent);
    Statistics stats;
    CheckOwnership(map, spent, genesis, stats);
    Require(keys.size() == map.groups.size(), Error::Context);
    const auto h = TransactionHashes(tx, spent, genesis, map);
    std::vector<Signature> signatures;
    for (size_t i = 0; i < map.groups.size(); ++i) {
        Require(keys[i].get().pk == map.groups[i].pk, Error::Ownership);
        B32 randomness{};
        Wipe wipe{randomness.data(), randomness.size()};
        Require(random && random(randomness), Error::Randomness);
        signatures.push_back(SignDigest(Digest(genesis, h, map.groups[i]), keys[i], randomness));
    }
    // Only commit after all signatures succeed; entropy failures leave tx intact.
    for (size_t i = 0; i < map.groups.size(); ++i) {
        auto& sig = tx.vin[map.groups[i].anchor].scriptWitness.stack[0];
        std::copy(signatures[i].begin(), signatures[i].end(), sig.begin());
    }
}
Result Verify(const CMutableTransaction& tx, const std::vector<CTxOut>& spent,
              const B32& genesis, const NonV3Verifier& non_v3)
{
    Result r;
    try {
        auto map = Reconstruct(tx, spent);
        r.stats.groups = map.groups.size();
        r.stats.inputs = map.inputs;
        CheckOwnership(map, spent, genesis, r.stats);
        auto manifest = Manifest(map);
        auto h = BodyHashes(tx, spent, genesis);
        ++r.stats.body_hash_passes;
        h.manifest = PQH384("Mercatura/PQGroupAuth/v1/Manifest", manifest);
        ++r.stats.manifest_hash_passes;
        std::vector<H48> digests;
        std::vector<bool> covered(tx.vin.size(), false);
        for (const auto& group : map.groups) {
            const auto digest = Digest(genesis, h, group);
            ++r.stats.final_digest_hashes;
            ++r.stats.verification_calls;
            Require(VerifyDigest(digest, group.pk,
                                 tx.vin[group.anchor].scriptWitness.stack[0]), Error::Signature);
            digests.push_back(digest);
            for (const auto& member : group.members) covered[member.index] = true;
        }
        for (size_t i = 0; i < covered.size(); ++i) {
            if (!covered[i]) {
                Require(bool(non_v3), Error::NonV3);
                ++r.stats.non_v3_calls;
                Require(non_v3(tx, spent, genesis, static_cast<uint32_t>(i)), Error::NonV3);
            } else r.authorized_inputs.push_back(static_cast<uint32_t>(i));
        }
        r.authorization = std::shared_ptr<const Authorization>(new Authorization(Wire(tx), spent, genesis, r.authorized_inputs,
            std::move(map), std::move(manifest), h, std::move(digests)));
        r.valid = true;
    } catch (const Failure& f) {
        r.error = f.code;
        r.authorized_inputs.clear();
    }
    return r;
}
B32 Derive(const B32& master, const B32& genesis, uint32_t account, uint8_t purpose,
           uint32_t group, std::optional<uint32_t> address)
{
    Require(purpose <= 2, Error::Purpose);
    CHKDF_HMAC_SHA3_384_L32 hkdf{master, AsBytes("Mercatura/PQGroupWalletMaster/v1")};
    Wipe wipe{&hkdf, sizeof(hkdf)};
    const auto tag = AsBytes(address ? "Mercatura/PQGroupAddrNonce/v1" : "Mercatura/PQGroupMLDSA65Key/v1");
    Writer w;
    w.write(std::as_bytes(tag));
    w << uint8_t{0} << genesis << account << purpose << group;
    if (address) w << *address;
    B32 out;
    hkdf.Expand32(w.bytes, out);
    return out;
}
Sizes Measure(const CMutableTransaction& tx)
{
    const auto total = Wire(tx).size(), base = Wire(tx, false).size();
    return {total, base, total - base, base * 3 + total, total};
}
} // namespace mercaweave
