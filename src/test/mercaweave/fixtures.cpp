// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/fixtures.h>
#include <crypto/hex_base.h>
#include <crypto/mercatura_pqkey.h>
#include <support/cleanse.h>
#include <cerrno>
#include <sys/random.h>

namespace mercaweave::test {
std::vector<std::reference_wrapper<const KeyPair>> Fixture::KeyRefs() const
{
    std::vector<std::reference_wrapper<const KeyPair>> refs;
    for (const auto& k : keys) refs.push_back(*k);
    return refs;
}
bool ZeroRandomnessForTestsOnly(B32& out) { out.fill(0); return true; }
bool SystemRandomnessForTests(B32& out)
{
    size_t pos{0};
    while (pos < out.size()) {
        const auto n = getrandom(out.data() + pos, out.size() - pos, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { memory_cleanse(out.data(), out.size()); return false; }
        pos += static_cast<size_t>(n);
    }
    return true;
}
Fixture Make(size_t inputs, size_t groups, bool repeated_nonce, bool structural_only)
{
    if (groups == 0 || groups > inputs || inputs > 5000) throw Failure(Error::Format);
    Fixture f;
    for (size_t i = 0; i < 32; ++i) f.genesis[i] = static_cast<unsigned char>(i);
    std::vector<PublicKey> pks;
    for (size_t k = 0; k < groups; ++k) {
        B32 seed{};
        WriteLE32(seed.data(), static_cast<uint32_t>(k));
        seed[31] = 0x42;
        if (structural_only) {
            PublicKey pk{};
            WriteLE32(pk.data(), static_cast<uint32_t>(k));
            pks.push_back(pk);
        } else {
            f.keys.push_back(std::make_unique<KeyPair>(seed));
            pks.push_back(f.keys.back()->pk);
        }
        memory_cleanse(seed.data(), seed.size());
    }
    for (size_t i = 0; i < inputs; ++i) {
        const size_t k = i % groups;
        B32 nonce{};
        WriteLE32(nonce.data(), repeated_nonce ? 0 : static_cast<uint32_t>(i));
        nonce[31] = 0x90;
        B32 txid{};
        txid.fill(0xa5);
        WriteLE32(txid.data(), static_cast<uint32_t>(i + 1));
        f.tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{std::span<const unsigned char>{txid}}), static_cast<uint32_t>(i)}, CScript{}, 0xfffffffe);
        if (i < groups) {
            f.tx.vin.back().scriptWitness.stack = {Bytes(3309), Bytes(pks[k].begin(), pks[k].end()), Bytes(nonce.begin(), nonce.end())};
        } else {
            Bytes ref(4);
            WriteLE32(ref.data(), static_cast<uint32_t>(k));
            f.tx.vin.back().scriptWitness.stack = {Bytes(nonce.begin(), nonce.end()), ref};
        }
        f.spent.emplace_back(1000, Output(f.genesis, pks[k], nonce));
    }
    CScript dest;
    dest << OP_2 << Bytes(32, 0xcc);
    f.tx.vout.emplace_back(static_cast<CAmount>(inputs) * 900, dest);
    return f;
}
void SignFixture(Fixture& f) { Sign(f.tx, f.spent, f.genesis, f.KeyRefs(), ZeroRandomnessForTestsOnly); }
H48 V2Digest(const CMutableTransaction& tx, const std::vector<CTxOut>& spent, const B32& genesis, uint32_t index)
{
    if (spent.size() != tx.vin.size() || index >= tx.vin.size()) throw Failure(Error::Context);
    MercaturaPQHashWriter prev{"Mercatura/PQAuth/v1/Prevouts"}, amount{"Mercatura/PQAuth/v1/Amounts"},
        script{"Mercatura/PQAuth/v1/ScriptPubKeys"}, seq{"Mercatura/PQAuth/v1/Sequences"}, out{"Mercatura/PQAuth/v1/Outputs"};
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        prev << tx.vin[i].prevout;
        amount << spent[i].nValue;
        script << spent[i].scriptPubKey;
        seq << tx.vin[i].nSequence;
    }
    for (const auto& o : tx.vout) out << o;
    MercaturaPQHashWriter final{"Mercatura/PQAuth/v1/Final"};
    final << tx.version << tx.nLockTime << uint64_t(tx.vin.size()) << uint64_t(tx.vout.size()) << genesis
          << prev.GetHash() << amount.GetHash() << script.GetHash() << seq.GetHash() << out.GetHash() << index;
    return final.GetHash();
}
bool VerifyV2(const CMutableTransaction& tx, const std::vector<CTxOut>& spent, const B32& genesis, uint32_t index)
{
    if (spent.size() != tx.vin.size() || index >= tx.vin.size()) return false;
    const auto& in = tx.vin[index];
    const auto& w = in.scriptWitness.stack;
    if (!in.scriptSig.empty() || w.size() != 2 || w[0].size() != 3309 || w[1].size() != 1952) return false;
    MercaturaPQKeyCommitment c;
    if (!ComputeMercaturaPQKeyCommitmentV1(c, w[1])) return false;
    CScript expected;
    expected << OP_2 << Bytes(c.begin(), c.end());
    return spent[index].scriptPubKey == expected && VerifyDigest(V2Digest(tx, spent, genesis, index), w[1], w[0], "Mercatura/PQAuth/v1");
}
void ConvertToV2(Fixture& f)
{
    for (size_t i = 0; i < f.tx.vin.size(); ++i) {
        const auto& key = *f.keys[i % f.keys.size()];
        MercaturaPQKeyCommitment c;
        if (!ComputeMercaturaPQKeyCommitmentV1(c, key.pk)) throw Failure(Error::Witness);
        CScript s;
        s << OP_2 << Bytes(c.begin(), c.end());
        f.spent[i].scriptPubKey = s;
        f.tx.vin[i].scriptWitness.stack = {Bytes(3309), Bytes(key.pk.begin(), key.pk.end())};
    }
    B32 zero{}; // Test-only deterministic signing randomness.
    for (size_t i = 0; i < f.tx.vin.size(); ++i) {
        const auto sig = SignDigest(V2Digest(f.tx, f.spent, f.genesis, static_cast<uint32_t>(i)), *f.keys[i % f.keys.size()], zero, "Mercatura/PQAuth/v1");
        f.tx.vin[i].scriptWitness.stack[0].assign(sig.begin(), sig.end());
    }
}
std::string Hex(std::span<const unsigned char> bytes) { return HexStr(bytes); }
Bytes Unhex(std::string_view text)
{
    if (text.size() % 2) throw Failure(Error::Encoding);
    Bytes b;
    auto nibble = [](char c) -> unsigned char {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw Failure(Error::Encoding);
    };
    for (size_t i = 0; i < text.size(); i += 2) b.push_back(nibble(text[i]) * 16 + nibble(text[i + 1]));
    return b;
}
} // namespace mercaweave::test
