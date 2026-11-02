// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "fixtures.h"
#include <crypto/mercatura_pqderive.h>
#include <crypto/sha3.h>
#include <algorithm>
#include <iostream>
#include <random>

using namespace mercaweave;
using namespace mercaweave::test;
namespace {
size_t assertions{0}, positive{0}, negative{0}, structural{0}, randomized{0};
void Check(bool condition, std::string_view label)
{
    ++assertions;
    if (!condition) throw std::runtime_error(std::string(label));
}
template<class F> void Throws(F&& f, Error expected, std::string_view label)
{
    try { f(); } catch (const Failure& e) { Check(e.code == expected, label); ++negative; return; }
    throw std::runtime_error(std::string(label) + ": did not reject");
}
void Good(Fixture& f, size_t groups, const NonV3Verifier& other = {})
{
    const auto r = Verify(f.tx, f.spent, f.genesis, other);
    Check(r.valid, "valid signature authorization");
    Check(r.stats.verification_calls == groups, "exact group verification calls");
    Check(r.stats.ownership_checks == r.stats.inputs, "one ownership check per input");
    Check(r.authorized_inputs.size() == r.stats.inputs, "complete v3 coverage");
    Check(r.authorization && r.authorization->Matches(f.tx, f.spent, f.genesis) && r.authorization->Inputs().size() == r.stats.inputs,
          "immutable transaction-scoped authorization bound to current context");
    ++positive;
}
using Mutation = std::function<void(CMutableTransaction&, std::vector<CTxOut>&, B32&)>;
void Bad(const Fixture& f, std::string_view label, const Mutation& mutate, std::optional<Error> error = {})
{
    auto tx = f.tx;
    auto spent = f.spent;
    auto genesis = f.genesis;
    mutate(tx, spent, genesis);
    const auto r = Verify(tx, spent, genesis, VerifyV2);
    Check(!r.valid && r.error.has_value() && r.authorized_inputs.empty() && !r.authorization, label);
    if (error) Check(r.error == error, label);
    ++negative;
}
Fixture Synthetic()
{
    auto f = Make(2, 1, false, true);
    PublicKey pk{};
    for (size_t i = 0; i < pk.size(); ++i) pk[i] = static_cast<unsigned char>(i);
    for (size_t i = 0; i < 2; ++i) {
        B32 nonce;
        nonce.fill(i == 0 ? 0x10 : 0x20);
        B32 txid;
        txid.fill(i == 0 ? 0xa1 : 0xb2);
        f.tx.vin[i].prevout = {Txid::FromUint256(uint256{std::span<const unsigned char>{txid}}), static_cast<uint32_t>(i + 1)};
        f.tx.vin[i].nSequence = 0xfffffffe - i;
        f.spent[i] = {static_cast<CAmount>(100 * (i + 1)), Output(f.genesis, pk, nonce)};
        if (i == 0) f.tx.vin[i].scriptWitness.stack = {Bytes(3309), Bytes(pk.begin(), pk.end()), Bytes(nonce.begin(), nonce.end())};
        else f.tx.vin[i].scriptWitness.stack = {Bytes(nonce.begin(), nonce.end()), Bytes(4)};
    }
    f.tx.vout[0].nValue = 250;
    return f;
}
void EmitHashes(const Fixture& f)
{
    const auto map = Reconstruct(f.tx, f.spent);
    const auto h = TransactionHashes(f.tx, f.spent, f.genesis, map);
    std::cout << "{\"Hprev\":\"" << Hex(h.prev) << "\",\"Hamount\":\"" << Hex(h.amount)
        << "\",\"Hscript\":\"" << Hex(h.script) << "\",\"Hseq\":\"" << Hex(h.sequence)
        << "\",\"Hout\":\"" << Hex(h.output) << "\",\"Htx\":\"" << Hex(h.tx)
        << "\",\"Hmanifest\":\"" << Hex(h.manifest) << "\",\"manifest\":\"" << Hex(Manifest(map))
        << "\",\"digests\":[";
    for (size_t i = 0; i < map.groups.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << '"' << Hex(Digest(f.genesis, h, map.groups[i])) << '"';
    }
    std::cout << "]}";
}
void AddV2Input(Fixture& f)
{
    auto v2 = Make(1, 1);
    ConvertToV2(v2);
    v2.tx.vin[0].prevout.n = 777;
    f.tx.vin.push_back(v2.tx.vin[0]);
    f.spent.push_back(v2.spent[0]);
    const auto sig = SignDigest(V2Digest(f.tx, f.spent, f.genesis, f.tx.vin.size() - 1), *f.keys[0], B32{}, "Mercatura/PQAuth/v1");
    f.tx.vin.back().scriptWitness.stack[0].assign(sig.begin(), sig.end());
}
void Vectors()
{
    auto synthetic = Synthetic();
    std::cout << "{\"synthetic\":";
    EmitHashes(synthetic);
    std::cout << ",\"cases\":[";
    for (size_t k = 0; k < 4; ++k) {
        auto f = Make(k == 0 ? 2 : 7, k < 2 ? 1 : 3, k == 1);
        if (k == 3) AddV2Input(f);
        SignFixture(f);
        if (!Verify(f.tx, f.spent, f.genesis, VerifyV2).valid) throw std::runtime_error("permanent v3 fixture signature validation");
        if (k) std::cout << ',';
        std::cout << "{\"genesis\":\"" << Hex(f.genesis) << "\",\"wire\":\"" << Hex(Wire(f.tx)) << "\",\"spent\":[";
        for (size_t i = 0; i < f.spent.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << "{\"amount\":" << f.spent[i].nValue << ",\"script\":\"" << Hex(f.spent[i].scriptPubKey) << "\"}";
        }
        std::cout << "],\"hashes\":";
        EmitHashes(f);
        auto v2 = Make(k == 0 ? 2 : 7, k < 2 ? 1 : 3, k == 1);
        ConvertToV2(v2);
        for (size_t i = 0; i < v2.tx.vin.size(); ++i) {
            if (!VerifyV2(v2.tx, v2.spent, v2.genesis, static_cast<uint32_t>(i))) throw std::runtime_error("permanent v2 adapter fixture signature validation");
        }
        std::cout << ",\"v2_wire\":\"" << Hex(Wire(v2.tx)) << "\"}";
    }
    std::cout << "],\"derivation\":[";
    B32 master{}, genesis{};
    for (size_t i = 0; i < 32; ++i) { master[i] = i + 32; genesis[i] = i; }
    for (uint8_t purpose = 0; purpose < 3; ++purpose) {
        if (purpose) std::cout << ',';
        auto seed = Derive(master, genesis, 7, purpose, 9);
        KeyPair key(seed);
        const auto nonce = Derive(master, genesis, 7, purpose, 9, 11);
        std::cout << "{\"seed\":\"" << Hex(seed) << "\",\"nonce\":\"" << Hex(nonce)
                  << "\",\"pk\":\"" << Hex(key.pk) << "\",\"commitment\":\"" << Hex(Commitment(genesis, key.pk, nonce)) << "\"}";
    }
    std::cout << "]}\n";
}
void PositiveTests()
{
    for (const auto [n, g] : std::vector<std::pair<size_t, size_t>>{{1,1},{2,1},{3,1},{10,1},{53,1},{100,1},{1000,1},{7,2},{53,10},{13,7},{10,10}}) {
        auto f = Make(n, g);
        SignFixture(f);
        Good(f, g);
        const auto sizes = Measure(f.tx);
        CTransaction immutable{f.tx};
        Check(sizes.total == GetSerializeSize(TX_WITH_WITNESS(immutable)), "actual immutable transaction size");
        Check(sizes.witness == 2 + 5301 * g + 39 * (n - g), "actual anchor follower bytes plus marker/flag");
        Check(sizes.weight == sizes.stripped * 3 + sizes.total, "weight");
        const auto wire = Wire(f.tx);
        Reader reader{wire};
        CMutableTransaction roundtrip;
        reader >> TX_WITH_WITNESS(roundtrip);
        Check(reader.remaining() == 0 && Wire(roundtrip) == wire, "consensus serialization round trip");
        for (const auto& in : f.tx.vin) {
            const auto wb = SerializeBytes(in.scriptWitness.stack);
            Check(DecodeWitness(wb).stack == in.scriptWitness.stack, "bounded witness round trip");
        }
    }
    auto reused = Make(8, 2, true);
    SignFixture(reused);
    Good(reused, 2);
    Check(reused.spent[0].scriptPubKey == reused.spent[2].scriptPubKey, "same address repeated payments allowed");
    auto mixed = Make(3, 1);
    AddV2Input(mixed);
    SignFixture(mixed);
    Good(mixed, 1, VerifyV2);
    Check(!Verify(mixed.tx, mixed.spent, mixed.genesis).valid, "missing non-v3 verifier fails closed");
    Bad(mixed, "invalid v2 cannot bypass via valid v3", [](auto& t, auto&, auto&) { t.vin.back().scriptWitness.stack[0][0] ^= 1; }, Error::NonV3);
    Bad(mixed, "mixed amount committed", [](auto&, auto& s, auto&) { s.back().nValue++; }, Error::Signature);
    auto unrelated = mixed.tx;
    unrelated.vin.back().scriptWitness.stack[0][1] ^= 1;
    const auto before = TransactionHashes(mixed.tx, mixed.spent, mixed.genesis, Reconstruct(mixed.tx, mixed.spent));
    const auto after = TransactionHashes(unrelated, mixed.spent, mixed.genesis, Reconstruct(unrelated, mixed.spent));
    Check(before.tx == after.tx && before.manifest == after.manifest, "unrelated witness excluded from group digest");
    unrelated = mixed.tx;
    unrelated.vin.back().scriptSig << OP_1;
    Check(TransactionHashes(unrelated, mixed.spent, mixed.genesis, Reconstruct(unrelated, mixed.spent)).tx == before.tx,
          "unrelated scriptSig intentionally excluded");
    Check(!Verify(unrelated, mixed.spent, mixed.genesis, VerifyV2).valid, "independent v2 rejects scriptSig");
    auto f = Make(2, 1);
    SignFixture(f);
    const auto deterministic = Wire(f.tx);
    SignFixture(f);
    Check(Wire(f.tx) == deterministic, "deterministic signature reproducibility");
    Sign(f.tx, f.spent, f.genesis, f.KeyRefs(), SystemRandomnessForTests);
    Good(f, 1);
    Check(Wire(f.tx) != deterministic, "randomized signature changes witness");
    Check(f.tx.GetHash() == [&] { Reader r{deterministic}; CMutableTransaction t; r >> TX_WITH_WITNESS(t); return t.GetHash(); }(), "random signatures preserve txid");
    const auto random_wire = Wire(f.tx);
    Throws([&] { Sign(f.tx, f.spent, f.genesis, f.KeyRefs(), {}); }, Error::Randomness, "missing entropy source");
    Check(random_wire == Wire(f.tx), "entropy failure atomicity");
    Throws([&] { Sign(f.tx, f.spent, f.genesis, f.KeyRefs(), [](B32&) { return false; }); }, Error::Randomness, "failed entropy source");
    auto two = Make(4, 2);
    const auto unsigned_wire = Wire(two.tx);
    size_t draws{0};
    Throws([&] { Sign(two.tx, two.spent, two.genesis, two.KeyRefs(), [&](B32& out) { out.fill(0); return ++draws == 1; }); }, Error::Randomness, "second group entropy failure");
    Check(unsigned_wire == Wire(two.tx), "no partial signatures on second failure");
}
void NegativeTests()
{
    auto f = Make(3, 1);
    SignFixture(f);
    for (const size_t item : {0U, 1U, 2U}) {
        Bad(f, "missing anchor item", [=](auto& t, auto&, auto&) { t.vin[0].scriptWitness.stack.erase(t.vin[0].scriptWitness.stack.begin() + item); });
        Bad(f, "short anchor item", [=](auto& t, auto&, auto&) { t.vin[0].scriptWitness.stack[item].pop_back(); }, Error::Witness);
        Bad(f, "long anchor item", [=](auto& t, auto&, auto&) { t.vin[0].scriptWitness.stack[item].push_back(0); }, Error::Witness);
    }
    Bad(f, "extra witness item", [](auto& t, auto&, auto&) { t.vin[0].scriptWitness.stack.push_back({}); }, Error::Witness);
    Bad(f, "signature corrupt", [](auto& t, auto&, auto&) { t.vin[0].scriptWitness.stack[0][100] ^= 1; }, Error::Signature);
    Bad(f, "public key corrupt", [](auto& t, auto&, auto&) { t.vin[0].scriptWitness.stack[1][100] ^= 1; }, Error::Ownership);
    Bad(f, "anchor nonce corrupt", [](auto& t, auto&, auto&) { t.vin[0].scriptWitness.stack[2][0] ^= 1; }, Error::Ownership);
    Bad(f, "follower nonce corrupt", [](auto& t, auto&, auto&) { t.vin[1].scriptWitness.stack[0][0] ^= 1; }, Error::Ownership);
    Bad(f, "follower to self", [](auto& t, auto&, auto&) { WriteLE32(t.vin[1].scriptWitness.stack[1].data(), 1); }, Error::Reference);
    Bad(f, "follower to future", [](auto& t, auto&, auto&) { WriteLE32(t.vin[1].scriptWitness.stack[1].data(), 2); }, Error::Reference);
    Bad(f, "nonexistent anchor", [](auto& t, auto&, auto&) { WriteLE32(t.vin[1].scriptWitness.stack[1].data(), UINT32_MAX); }, Error::Reference);
    Bad(f, "follower chain", [](auto& t, auto&, auto&) { WriteLE32(t.vin[2].scriptWitness.stack[1].data(), 1); }, Error::Reference);
    Bad(f, "short reference", [](auto& t, auto&, auto&) { t.vin[1].scriptWitness.stack[1].pop_back(); }, Error::Witness);
    Bad(f, "oversized reference", [](auto& t, auto&, auto&) { t.vin[1].scriptWitness.stack[1].push_back(0); }, Error::Witness);
    Bad(f, "extra follower item", [](auto& t, auto&, auto&) { t.vin[1].scriptWitness.stack.push_back({}); }, Error::Witness);
    Bad(f, "missing follower nonce", [](auto& t, auto&, auto&) { t.vin[1].scriptWitness.stack.erase(t.vin[1].scriptWitness.stack.begin()); }, Error::Witness);
    Bad(f, "duplicate anchor key", [](auto& t, auto&, auto&) { t.vin[1].scriptWitness = t.vin[0].scriptWitness; }, Error::DuplicateKey);
    Bad(f, "anchor not lowest", [](auto& t, auto&, auto&) { std::swap(t.vin[0].scriptWitness, t.vin[1].scriptWitness); }, Error::Reference);
    Bad(f, "omitted coverage", [](auto& t, auto&, auto&) { t.vin[1].scriptWitness.stack.clear(); }, Error::Witness);
    Bad(f, "nonempty scriptSig", [](auto& t, auto&, auto&) { t.vin[1].scriptSig << OP_1; }, Error::ScriptSig);
    Bad(f, "wrong spent commitment", [](auto&, auto& s, auto&) { s[1].scriptPubKey[8] ^= 1; }, Error::Ownership);
    Bad(f, "short v3 program", [](auto&, auto& s, auto&) { s[1].scriptPubKey.pop_back(); s[1].scriptPubKey[1] = 31; }, Error::Program);
    Bad(f, "long v3 program", [](auto&, auto& s, auto&) { s[1].scriptPubKey.push_back(0); s[1].scriptPubKey[1] = 33; }, Error::Program);
    Bad(f, "wrong witness version", [](auto&, auto& s, auto&) { s[1].scriptPubKey[0] = OP_4; });
    Bad(f, "P2SH wrapped v3", [](auto& t, auto& s, auto&) {
        Bytes redeem(s[1].scriptPubKey.begin(), s[1].scriptPubKey.end());
        t.vin[1].scriptSig << redeem;
        s[1].scriptPubKey = CScript{} << OP_HASH160 << Bytes(20) << OP_EQUAL;
    }, Error::Wrapped);
    Bad(f, "wrong genesis", [](auto&, auto&, auto& g) { g[0] ^= 1; }, Error::Ownership);
    Bad(f, "outpoint mutation", [](auto& t, auto&, auto&) { t.vin[1].prevout.n++; }, Error::Signature);
    Bad(f, "spent amount mutation", [](auto&, auto& s, auto&) { s[1].nValue++; }, Error::Signature);
    Bad(f, "sequence mutation", [](auto& t, auto&, auto&) { t.vin[1].nSequence--; }, Error::Signature);
    Bad(f, "version mutation", [](auto& t, auto&, auto&) { t.version++; }, Error::Signature);
    Bad(f, "locktime mutation", [](auto& t, auto&, auto&) { t.nLockTime++; }, Error::Signature);
    Bad(f, "output amount mutation", [](auto& t, auto&, auto&) { t.vout[0].nValue--; }, Error::Signature);
    Bad(f, "destination mutation", [](auto& t, auto&, auto&) { t.vout[0].scriptPubKey[10] ^= 1; }, Error::Signature);
    Bad(f, "input ordering", [](auto& t, auto& s, auto&) { std::swap(t.vin[1], t.vin[2]); std::swap(s[1], s[2]); }, Error::Signature);
    Bad(f, "input count mutation", [](auto& t, auto& s, auto&) { t.vin.pop_back(); s.pop_back(); t.vout[0].nValue = 100; });
    Bad(f, "output count mutation", [](auto& t, auto&, auto&) { t.vout.emplace_back(0, CScript{}); }, Error::Signature);
    Bad(f, "missing spent context", [](auto&, auto& s, auto&) { s.pop_back(); }, Error::Context);
    Bad(f, "negative amount", [](auto&, auto& s, auto&) { s[0].nValue = -1; }, Error::Amount);
    Bad(f, "sum overflow guarded", [](auto&, auto& s, auto&) { s[0].nValue = MAX_MONEY; }, Error::Amount);
    Bad(f, "duplicate outpoint", [](auto& t, auto&, auto&) { t.vin[1].prevout = t.vin[0].prevout; }, Error::DuplicateOutpoint);
    Bad(f, "null outpoint/coinbase", [](auto& t, auto&, auto&) { t.vin[1].prevout.SetNull(); }, Error::Format);
    Bad(f, "empty transaction inputs", [](auto& t, auto& s, auto&) { t.vin.clear(); s.clear(); }, Error::Format);
    Bad(f, "empty transaction outputs", [](auto& t, auto&, auto&) { t.vout.clear(); }, Error::Format);
    Bad(f, "surplus spent context", [](auto&, auto& s, auto&) { s.push_back(s[0]); }, Error::Context);
    Bad(f, "output exceeds inputs", [](auto& t, auto&, auto&) { t.vout[0].nValue = 3001; }, Error::Amount);
    Bad(f, "output amount above bound", [](auto& t, auto&, auto&) { t.vout[0].nValue = MAX_MONEY + 1; }, Error::Amount);
    Bad(f, "non-v3 witness stack global limit", [](auto& t, auto& s, auto&) {
        s[2].scriptPubKey = CScript{} << OP_2 << Bytes(32);
        t.vin[2].scriptWitness.stack.resize(1004);
    }, Error::Witness);
    auto multi = Make(7, 2);
    multi.tx.vout.emplace_back(1, CScript{} << OP_1);
    SignFixture(multi);
    Bad(multi, "output reorder", [](auto& t, auto&, auto&) { std::swap(t.vout[0], t.vout[1]); }, Error::Signature);
    Bad(multi, "group reorder", [](auto& t, auto& s, auto&) { std::swap(t.vin[0], t.vin[1]); std::swap(s[0], s[1]); });
    Bad(multi, "inconsistent membership", [](auto& t, auto&, auto&) { WriteLE32(t.vin[2].scriptWitness.stack[1].data(), 1); }, Error::Ownership);
    Bad(multi, "big endian reference", [](auto& t, auto&, auto&) { std::reverse(t.vin[3].scriptWitness.stack[1].begin(), t.vin[3].scriptWitness.stack[1].end()); }, Error::Reference);
    const auto multimap = Reconstruct(multi.tx, multi.spent);
    const auto multihashes = TransactionHashes(multi.tx, multi.spent, multi.genesis, multimap);
    auto reordered = multimap;
    std::reverse(reordered.groups.begin(), reordered.groups.end());
    Throws([&] { Manifest(reordered); }, Error::Reference, "noncanonical manifest group order");
    reordered = multimap;
    reordered.groups[0].members[0].nonce[0] ^= 1;
    const auto altered_manifest = TransactionHashes(multi.tx, multi.spent, multi.genesis, reordered);
    Check(!VerifyDigest(Digest(multi.genesis, altered_manifest, multimap.groups[0]), multimap.groups[0].pk,
                        multi.tx.vin[0].scriptWitness.stack[0]), "manifest mutation invalidates signature"); ++negative;
    size_t forward{0}, backward{0};
    for (const auto& group : multimap.groups) forward += VerifyDigest(Digest(multi.genesis, multihashes, group), group.pk, multi.tx.vin[group.anchor].scriptWitness.stack[0]);
    for (auto it = multimap.groups.rbegin(); it != multimap.groups.rend(); ++it) backward += VerifyDigest(Digest(multi.genesis, multihashes, *it), it->pk, multi.tx.vin[it->anchor].scriptWitness.stack[0]);
    Check(forward == 2 && backward == forward, "group signature processing order independent");
    // P2SH classification uses only its final redeem script, not preceding data.
    auto argument = multi.tx;
    auto argument_spent = multi.spent;
    argument.vin[0].scriptSig = CScript{} << Bytes(multi.spent[0].scriptPubKey.begin(), multi.spent[0].scriptPubKey.end()) << Bytes{OP_1};
    argument_spent[0].scriptPubKey = CScript{} << OP_HASH160 << Bytes(20) << OP_EQUAL;
    // Retarget former followers so no missing anchor masks classification.
    for (size_t i = 2; i < argument.vin.size(); i += 2) { argument_spent[i].scriptPubKey = CScript{} << OP_1; }
    Check(Reconstruct(argument, argument_spent).groups.size() == 1, "v3-shaped P2SH argument is not redeem script");
    auto mixed = Make(3, 1);
    AddV2Input(mixed);
    // Place v2 at index 0; followers now attempt to reference this non-anchor.
    Bad(mixed, "follower referencing v2", [](auto& t, auto& s, auto&) { std::swap(t.vin[0], t.vin[3]); std::swap(s[0], s[3]); }, Error::Reference);
    const auto map = Reconstruct(f.tx, f.spent);
    const auto hashes = TransactionHashes(f.tx, f.spent, f.genesis, map);
    const auto digest = Digest(f.genesis, hashes, map.groups[0]);
    for (const auto context : {std::string_view{"wrong"}, std::string_view{"Mercatura/PQGroupAuth/v1\0", 25}}) {
        const auto sig = SignDigest(digest, *f.keys[0], B32{}, context);
        Check(!VerifyDigest(digest, f.keys[0]->pk, sig), "wrong FIPS context rejected"); ++negative;
    }
    auto bad_digest = digest;
    bad_digest[0] ^= 1;
    Check(!VerifyDigest(bad_digest, f.keys[0]->pk, f.tx.vin[0].scriptWitness.stack[0]), "wrong digest"); ++negative;
    const auto wrong_tag = PQH384("Mercatura/PQGroupAuth/v1/final", digest);
    Check(!VerifyDigest(digest, f.keys[0]->pk, SignDigest(wrong_tag, *f.keys[0], B32{})), "wrong digest tag"); ++negative;
    H48 unprefixed;
    const std::string_view tag{"Mercatura/PQGroupAuth/v1/Final"};
    SHA3_384().Write({reinterpret_cast<const unsigned char*>(tag.data()), tag.size()}).Write(digest).Finalize(unprefixed);
    Check(!VerifyDigest(digest, f.keys[0]->pk, SignDigest(unprefixed, *f.keys[0], B32{})), "missing CompactSize tag length"); ++negative;
    B32 seed{}; seed[0] = 99;
    KeyPair wrong_key(seed);
    Check(!VerifyDigest(digest, f.keys[0]->pk, SignDigest(digest, wrong_key, B32{})), "wrong signing key"); ++negative;
    Signature signature{};
    B32 zero{};
    Check(!mercatura_mldsa65_sign(signature.data(), signature.size(), digest.data(), digest.size(), nullptr, 0,
                                zero.data(), 31, f.keys[0]->sk.data(), f.keys[0]->sk.size()), "wrapper rejects short randomness"); ++negative;
    Check(!mercatura_mldsa65_sign(signature.data(), signature.size(), digest.data(), digest.size(), nullptr, 0,
                                nullptr, 32, f.keys[0]->sk.data(), f.keys[0]->sk.size()), "wrapper rejects null randomness"); ++negative;
    auto mutated_map = map;
    mutated_map.groups[0].members.push_back(mutated_map.groups[0].members[0]);
    Throws([&] { Manifest(mutated_map); }, Error::Reference, "duplicate manifest member");
    mutated_map = map;
    mutated_map.inputs++;
    Throws([&] { Manifest(mutated_map); }, Error::Context, "missing manifest member");
    // No authorization context/cache is accepted by Verify: every call rebuilds
    // mapping, hashes and ownership. This repeated mutation exercises that fact.
    Good(f, 1);
    const auto context = Verify(f.tx, f.spent, f.genesis).authorization;
    auto changed_tx = f.tx;
    auto changed_spent = f.spent;
    auto changed_genesis = f.genesis;
    changed_tx.vin[0].scriptWitness.stack[0][0] ^= 1;
    changed_spent[0].nValue++;
    changed_genesis[0] ^= 1;
    Check(!context->Matches(changed_tx, f.spent, f.genesis), "immutable record rejects changed witness");
    Check(!context->Matches(f.tx, changed_spent, f.genesis), "immutable record rejects changed spent context");
    Check(!context->Matches(f.tx, f.spent, changed_genesis), "immutable record rejects changed network");
    changed_tx = f.tx;
    changed_tx.version++;
    Check(!context->Matches(changed_tx, f.spent, f.genesis), "immutable record rejects changed transaction");
    Bad(f, "no stale acceptance after successful validation", [](auto&, auto& s, auto&) { s[0].nValue++; }, Error::Signature);
}
void DerivationTests()
{
    B32 master{}, genesis{};
    for (size_t i = 0; i < 32; ++i) { master[i] = i + 32; genesis[i] = i; }
    auto seed = Derive(master, genesis, 7, 0, 9);
    Check(seed == Derive(master, genesis, 7, 0, 9), "same group seed");
    auto nonce = Derive(master, genesis, 7, 0, 9, 11);
    Check(nonce == Derive(master, genesis, 7, 0, 9, 11), "same nonce");
    Check(nonce != Derive(master, genesis, 7, 0, 9, 12), "address nonce separation");
    Check(seed != Derive(master, genesis, 8, 0, 9), "account separation");
    Check(seed != Derive(master, genesis, 7, 1, 9), "purpose separation");
    Check(seed != Derive(master, genesis, 7, 2, 9), "mining purpose separation");
    Check(seed != Derive(master, genesis, 7, 0, 10), "group separation");
    auto other_genesis = genesis; other_genesis[0] ^= 1;
    Check(seed != Derive(master, other_genesis, 7, 0, 9), "network key separation");
    Check(nonce != Derive(master, other_genesis, 7, 0, 9, 11), "network nonce separation");
    KeyPair k1(seed), k2(Derive(master, genesis, 7, 0, 9));
    Check(k1.pk == k2.pk, "reproducible derived keys");
    Check(Output(genesis, k1.pk, nonce) != Output(genesis, k1.pk, Derive(master, genesis, 7, 0, 9, 12)), "distinct derived receiving outputs");
    const auto v2 = DeriveMercaturaPQChildSeed(master, uint256{std::span<const unsigned char>{genesis}}, 7, 0, 9);
    Check(v2 && *v2 != seed, "v2 namespace unaffected and separate");
    for (uint8_t p : {3, 127, 255}) Throws([&] { Derive(master, genesis, 7, p, UINT32_MAX, UINT32_MAX); }, Error::Purpose, "invalid purpose");
    Check(Derive(master, genesis, UINT32_MAX, 2, UINT32_MAX, UINT32_MAX) != nonce, "uint32 derivation boundaries");
    ++positive;
}
void ResourceAndParsingTests()
{
    for (uint64_t value : {uint64_t{0}, uint64_t{252}, uint64_t{253}, uint64_t{65535}, uint64_t{65536}, uint64_t{UINT32_MAX}, UINT64_MAX}) {
        Writer w;
        WriteCompactSize(w, value);
        Reader r{w.bytes};
        Check(ReadCompactSize(r, false) == value && r.remaining() == 0, "CompactSize integer boundaries");
    }
    for (auto [n, g] : std::vector<std::pair<size_t, size_t>>{{1024,1},{256,256},{4096,4}}) {
        auto f = Make(n, g, false, true);
        const auto map = Reconstruct(f.tx, f.spent);
        Statistics stats;
        CheckOwnership(map, f.spent, f.genesis, stats);
        Check(map.inputs == n && map.groups.size() == g && stats.ownership_checks == n, "maximum structural bounds");
        Check(!Manifest(map).empty(), "maximum structural manifest");
        ++structural;
    }
    for (auto [n, g, e] : std::vector<std::tuple<size_t, size_t, Error>>{{1025,1,Error::MembersLimit},{257,257,Error::GroupsLimit},{4097,5,Error::InputsLimit}}) {
        auto f = Make(n, g, false, true);
        const auto r = Verify(f.tx, f.spent, f.genesis);
        Check(!r.valid && r.error == e && r.stats.verification_calls == 0 && r.stats.ownership_checks == 0, "above limit rejected before crypto");
        ++negative;
    }
    auto f = Make(2, 1);
    SignFixture(f);
    const auto anchor = SerializeBytes(f.tx.vin[0].scriptWitness.stack);
    for (size_t n = 0; n < anchor.size(); ++n) {
        bool rejected{false};
        try { DecodeWitness(std::span{anchor}.first(n)); } catch (const Failure&) { rejected = true; }
        Check(rejected, "every truncated anchor prefix"); ++randomized;
    }
    for (const auto& bad : {Bytes{0xfd,2,0}, Bytes{0xfe,3,0,0,0}, Bytes{0xff,3,0,0,0,0,0,0,0}, Bytes{3,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}}) {
        bool rejected{false};
        try { DecodeWitness(bad); } catch (const Failure&) { rejected = true; }
        Check(rejected, "noncanonical and oversized CompactSize"); ++negative;
    }
    std::mt19937_64 rng{0x4d65726361576561ULL};
    for (size_t i = 0; i < 2000; ++i) {
        auto wire = anchor;
        const auto at = rng() % wire.size();
        wire[at] ^= static_cast<unsigned char>(1 + rng() % 255);
        try {
            auto witness = DecodeWitness(wire);
            auto tx = f.tx;
            tx.vin[0].scriptWitness = witness;
            Check(!Verify(tx, f.spent, f.genesis).valid, "mutated decoded witness rejects");
        } catch (const Failure&) { ++assertions; }
        ++randomized;
    }
    for (size_t i = 0; i < 1000; ++i) {
        Bytes wire(rng() % 128);
        for (auto& b : wire) b = static_cast<unsigned char>(rng());
        try { const auto w = DecodeWitness(wire); Check(SerializeBytes(w.stack) == wire, "canonical decode roundtrip"); }
        catch (const Failure&) { ++assertions; }
        ++randomized;
    }
}
} // namespace
int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--vectors") { Vectors(); return 0; }
        PositiveTests();
        NegativeTests();
        DerivationTests();
        ResourceAndParsingTests();
        std::cout << "PASS assertions=" << assertions << " positive_cases=" << positive
                  << " negative_cases=" << negative << " structural_boundary_cases=" << structural
                  << " malformed_cases=" << randomized << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << " after " << assertions << " assertions\n";
        return 1;
    }
}
