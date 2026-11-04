// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/fixtures.h>
#include <test/mercaweave/native_v2.h>
#include <crypto/sha256.h>
#include <iostream>

using namespace mercaweave;
using namespace mercaweave::test;
namespace {
size_t assertions{0}, rejected{0}, accepted{0};
void Check(bool ok, const char* name) { ++assertions; if (!ok) throw std::runtime_error(name); }
void Good(const Fixture& f, size_t groups, size_t native_inputs, script_verify_flags flags = MANDATORY_SCRIPT_VERIFY_FLAGS)
{
    const auto r = NativeSession(f.tx, f.spent, f.genesis, flags).VerifyMixed();
    Check(r.grouped.valid && r.grouped.stats.verification_calls == groups &&
          r.grouped.stats.ownership_checks == r.grouped.stats.inputs && r.script_checks == native_inputs &&
          r.precomputations == 1, "complete native/grouped verification");
    ++accepted;
}
void Bad(const Fixture& f, Error error)
{
    const auto r = NativeSession(f.tx, f.spent, f.genesis).VerifyMixed();
    Check(!r.grouped.valid && r.grouped.error == error && !r.grouped.authorization, "native/grouped rejection");
    ++rejected;
}
bool NativeCheck(const Fixture& f, uint32_t index)
{
    ScriptError error;
    size_t calls{0};
    return NativeSession(f.tx, f.spent, f.genesis).Check(index, error, calls);
}
void SignV2(Fixture& f, uint32_t first)
{
    const NativeSession native(f.tx, f.spent, f.genesis);
    for (size_t i = first; i < f.tx.vin.size(); ++i) {
        Check(native.Digest(i) == V2Digest(f.tx, f.spent, f.genesis, i), "production/research v2 digest equality");
        const auto sig = SignDigest(native.Digest(i), *f.keys[0], B32{}, "Mercatura/PQAuth/v1");
        f.tx.vin[i].scriptWitness.stack[0].assign(sig.begin(), sig.end());
    }
}
Fixture Mixed(size_t n = 3, size_t groups = 1, size_t v2_count = 2)
{
    auto f = Make(n, groups);
    auto v2 = Make(v2_count, 1);
    ConvertToV2(v2);
    for (size_t i = 0; i < v2_count; ++i) {
        v2.tx.vin[i].prevout.n += 100000;
        f.tx.vin.push_back(v2.tx.vin[i]);
        f.spent.push_back(v2.spent[i]);
    }
    SignV2(f, n);
    SignFixture(f);
    return f;
}
void Interoperability()
{
    for (size_t n : {1, 2, 7, 10}) {
        auto f = Make(n, 1);
        ConvertToV2(f);
        Good(f, 0, n);
        const NativeSession native(f.tx, f.spent, f.genesis);
        for (size_t i = 0; i < n; ++i) Check(native.Digest(i) == V2Digest(f.tx, f.spent, f.genesis, i), "all production digest components");
    }
    auto mixed = Mixed();
    Good(mixed, 1, 2);
    Check(NativeSession(mixed.tx, mixed.spent, mixed.genesis).VerifyMixed().pq_checker_calls == 2, "two genuine production PQ checker calls");
    auto multi = Mixed(7, 3, 3);
    Good(multi, 3, 3);
    auto tx = mixed.tx;
    auto spent = mixed.spent;
    mixed.tx.vin[3].scriptWitness.stack[0][17] ^= 1;
    Bad(mixed, Error::NonV3);
    Check(NativeSession(mixed.tx, mixed.spent, mixed.genesis).VerifyMixed().grouped.stats.verification_calls == 1, "valid v3 cannot authorize invalid v2");
    mixed.tx = tx;
    mixed.spent[3].nValue++;
    Check(!NativeCheck(mixed, 3), "v2 authenticates changed v2 amount");
    Bad(mixed, Error::Signature);
    mixed.spent = spent;
    mixed.spent[3].scriptPubKey[3] ^= 1;
    Check(!NativeCheck(mixed, 3), "v2 authenticates changed v2 script");
    Bad(mixed, Error::Signature);
    mixed.spent = spent;
    mixed.tx.vin[1].scriptWitness.stack[0][0] ^= 1;
    Check(NativeCheck(mixed, 3), "v3 witness nonce outside v2 signed scope");
    Bad(mixed, Error::Ownership);
    mixed.tx = tx;
    mixed.tx.vin[0].scriptWitness.stack[0][0] ^= 1;
    Check(NativeCheck(mixed, 3), "other group's signature outside v2 signed scope");
    Bad(mixed, Error::Signature);
    mixed.tx = tx;
    mixed.spent[1].nValue++;
    Check(!NativeCheck(mixed, 3), "v2 authenticates v3 spent amounts");
    Bad(mixed, Error::Signature);
    mixed.spent = spent;
    mixed.genesis[0] ^= 1;
    Check(!NativeCheck(mixed, 3), "native v2 wrong network");
    Bad(mixed, Error::Ownership);
    mixed.genesis[0] ^= 1;
    const NativeSession session(mixed.tx, mixed.spent, mixed.genesis);
    auto sig = SignDigest(session.Digest(3), *mixed.keys[0], B32{}, CONTEXT);
    mixed.tx.vin[3].scriptWitness.stack[0].assign(sig.begin(), sig.end());
    Bad(mixed, Error::NonV3);
    mixed.tx = tx;
    for (const auto mode : {0, 1}) {
        ScriptError error;
        size_t calls{0};
        Check(!session.Check(3, error, calls, mode != 0, mode == 0), "missing network or precomputation fails closed"); ++rejected;
    }
    mixed.tx.vin[3].scriptSig << OP_1;
    Bad(mixed, Error::NonV3);
    mixed.tx = tx;
    mixed.tx.vin[3].scriptWitness.stack.push_back({});
    Bad(mixed, Error::NonV3);
    mixed.tx = tx;
    mixed.tx.vin[3].scriptWitness.stack[0].pop_back();
    Bad(mixed, Error::NonV3);
    mixed.tx = tx;
    // Existing unknown-v3 permissiveness is NOT group authorization. The test
    // adapter must route v3 through the research verifier, never this success.
    mixed.tx.vin[0].scriptWitness.stack[0][0] ^= 1;
    Check(NativeCheck(mixed, 0), "production interpreter still treats unknown v3 permissively");
    Bad(mixed, Error::Signature);
    mixed.tx = tx;
    // NativeSession owns a snapshot: mutable caller data cannot stale its cache.
    const NativeSession frozen(mixed.tx, mixed.spent, mixed.genesis);
    mixed.tx.version++;
    Check(frozen.VerifyMixed().grouped.valid, "immutable native session retains its original transaction");
    Bad(mixed, Error::Signature);
    const auto record = frozen.VerifyMixed().grouped.authorization;
    Check(!record->Matches(mixed.tx, mixed.spent, mixed.genesis), "snapshot authorization cannot match modified caller");
}
void Malleability()
{
    auto f = Mixed();
    const auto original = f.tx;
    const CTransaction old_tx{original};
    const auto map = Reconstruct(f.tx, f.spent);
    const auto hashes = TransactionHashes(f.tx, f.spent, f.genesis, map);
    Sign(f.tx, f.spent, f.genesis, f.KeyRefs(), SystemRandomnessForTests);
    Good(f, 1, 2);
    Check(old_tx.GetHash() == CTransaction(f.tx).GetHash() && old_tx.GetWitnessHash() != CTransaction(f.tx).GetWitnessHash(), "randomized v3 changes wtxid only");
    Check(NativeCheck(f, 3), "native v2 survives v3 signature randomization");
    f.tx = original;
    B32 randomness{};
    Check(SystemRandomnessForTests(randomness), "test-only secure entropy");
    const auto sig = SignDigest(NativeSession(f.tx, f.spent, f.genesis).Digest(3), *f.keys[0], randomness, "Mercatura/PQAuth/v1");
    f.tx.vin[3].scriptWitness.stack[0].assign(sig.begin(), sig.end());
    Good(f, 1, 2);
    Check(old_tx.GetHash() == CTransaction(f.tx).GetHash() && old_tx.GetWitnessHash() != CTransaction(f.tx).GetWitnessHash(), "randomized v2 changes wtxid only");
    Check(TransactionHashes(f.tx, f.spent, f.genesis, map).tx == hashes.tx, "v3 digest excludes other signatures");
    f.tx = original;
    f.tx.vin[1].scriptSig << OP_0;
    Bad(f, Error::ScriptSig);

    // Supported non-signature predicates, not classical signature ownership.
    for (int form : {0, 1, 2}) {
        auto mixed = Make(2, 1);
        auto extra = Make(1, 1);
        extra.tx.vin[0].prevout.n = 999;
        extra.tx.vin[0].scriptWitness.stack.clear();
        const CScript predicate = CScript{} << OP_DROP << OP_TRUE;
        const Bytes redeem(predicate.begin(), predicate.end());
        CScript locking;
        if (form == 0) {
            locking = predicate;
            extra.tx.vin[0].scriptSig = CScript{} << Bytes{0x41};
        } else if (form == 1) {
            std::array<unsigned char, 20> hash;
            CHash160().Write(redeem).Finalize(hash);
            locking << OP_HASH160 << Bytes(hash.begin(), hash.end()) << OP_EQUAL;
            extra.tx.vin[0].scriptSig = CScript{} << Bytes{0x41} << redeem;
        } else {
            B32 hash;
            CSHA256().Write(redeem.data(), redeem.size()).Finalize(hash.data());
            locking << OP_0 << Bytes(hash.begin(), hash.end());
            extra.tx.vin[0].scriptWitness.stack = {Bytes{0x41}, redeem};
        }
        mixed.tx.vin.push_back(extra.tx.vin[0]);
        mixed.spent.emplace_back(1000, locking);
        SignFixture(mixed);
        Good(mixed, 1, 1, STANDARD_SCRIPT_VERIFY_FLAGS);
        const auto before_tx = CTransaction(mixed.tx);
        const auto before_hashes = TransactionHashes(mixed.tx, mixed.spent, mixed.genesis, Reconstruct(mixed.tx, mixed.spent));
        const auto auth = NativeSession(mixed.tx, mixed.spent, mixed.genesis).VerifyMixed().grouped.authorization;
        if (form == 2) mixed.tx.vin[2].scriptWitness.stack[0][0] = 0x42;
        else if (form == 1) mixed.tx.vin[2].scriptSig = CScript{} << Bytes{0x42} << redeem;
        else mixed.tx.vin[2].scriptSig = CScript{} << Bytes{0x42};
        Good(mixed, 1, 1, STANDARD_SCRIPT_VERIFY_FLAGS);
        const auto after_tx = CTransaction(mixed.tx);
        Check(before_hashes.tx == TransactionHashes(mixed.tx, mixed.spent, mixed.genesis, Reconstruct(mixed.tx, mixed.spent)).tx, "non-v3 mutable data excluded from signed scope");
        Check((before_tx.GetHash() != after_tx.GetHash()) == (form != 2), "legacy/P2SH scriptSig changes txid; P2WSH witness does not");
        Check(before_tx.GetWitnessHash() != after_tx.GetWitnessHash(), "all changed wire witnesses change wtxid");
        Check(!auth->Matches(mixed.tx, mixed.spent, mixed.genesis), "full authorization record detects permitted malleability");
    }
    for (const auto opcode : {OP_CHECKSIG, OP_CHECKMULTISIG}) {
        auto legacy = Make(1, 1);
        legacy.tx.vin[0].scriptWitness.stack.clear();
        legacy.tx.vin[0].scriptSig = CScript{} << Bytes{1} << Bytes(33, 2);
        legacy.spent[0].scriptPubKey = CScript{} << opcode;
        Check(!NativeCheck(legacy, 0), "production classical signature opcodes remain forbidden"); ++rejected;
    }
    auto taproot = Make(1, 1);
    taproot.spent[0].scriptPubKey = CScript{} << OP_1 << Bytes(32, 0xcc);
    taproot.tx.vin[0].scriptWitness.stack = {Bytes(64)};
    Check(!NativeCheck(taproot, 0), "production taproot key-path remains forbidden"); ++rejected;
}
}
int main()
{
    try {
        auto f = Make(2, 1);
        ConvertToV2(f);
        NativeSession native{f.tx, f.spent, f.genesis};
        for (uint32_t i = 0; i < 2; ++i) Check(native.Digest(i) == V2Digest(f.tx, f.spent, f.genesis, i), "native/research digest agreement");
        auto result = native.VerifyMixed();
        Check(result.grouped.valid && result.script_checks == 2 && result.pq_checker_calls == 2, "real native v2 verification");
        Interoperability();
        Malleability();
        std::cout << "PASS native assertions=" << assertions << " accepted=" << accepted << " rejected=" << rejected << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL native: " << e.what() << '\n'; return 1; }
}
