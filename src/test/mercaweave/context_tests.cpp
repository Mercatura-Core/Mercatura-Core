// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/context_fixtures.h>
#include <barrier>
#include <future>
#include <iostream>
#include <random>
#include <type_traits>

using namespace mercaweave;
using namespace mercaweave::test;
using namespace mercaweave::research;
namespace {
size_t assertions{0}, mismatches{0}, mutations{0};
void Check(bool b, const char* name) { ++assertions; if (!b) throw std::runtime_error(name); }
RequestPtr View(const Fixture& f, script_verify_flags flags = MANDATORY_SCRIPT_VERIFY_FLAGS) { return Capture(f.tx, f.spent, f.genesis, flags); }
void Tests()
{
    static_assert(!std::is_default_constructible_v<ValidatedContext>);
    static_assert(!std::is_copy_constructible_v<ValidatedContext>);
    static_assert(!std::is_aggregate_v<ValidatedContext>);
    static_assert(!std::is_constructible_v<ValidatedContext, Result>);
    static_assert(!std::is_default_constructible_v<InputChecks>);
    auto f = MixedFixture(6, 2, 1);
    const auto request = View(f);
    Check(CheckInput(request, 0) == ContextError::MissingContext, "missing context never authorizes anchor");
    Check(CheckInput(request, 2) == ContextError::MissingContext, "missing context never authorizes follower");
    Check(CheckInput(request, 6) == ContextError::None, "v2 retains genuine independent native path without group context");
    auto prepared = Prepare(request);
    Check(prepared.context && prepared.error == ContextError::None, "complete factory success");
    Check(prepared.work.verification_calls == 2 && prepared.work.ownership_checks == 6 && prepared.native_pq_calls == 1,
          "one verify per group and one native-v2 verify");
    Check(prepared.work.body_hash_passes == 1 && prepared.work.manifest_hash_passes == 1 && prepared.work.final_digest_hashes == 2,
          "group components/manifest computed once, final digest once per group");
    const auto record = prepared.context;
    Check(record->TxIdentity() == CTransaction(f.tx).GetHash() && record->WitnessIdentity() == CTransaction(f.tx).GetWitnessHash(), "both exact identities bound");
    const auto map = Reconstruct(f.tx, f.spent);
    const auto hashes = TransactionHashes(f.tx, f.spent, f.genesis, map);
    Check(record->GroupProof().ManifestBytes() == Manifest(map) && record->GroupProof().Commitments().manifest == hashes.manifest,
          "validated canonical manifest retained");
    for (size_t k = 0; k < 2; ++k) Check(record->GroupProof().GroupDigests()[k] == Digest(f.genesis, hashes, map.groups[k]), "verified group digests retained");
    const auto bound = Bind(request, record);
    Check(bound.checks != nullptr, "bind current snapshot");
    for (uint32_t i = 0; i < 7; ++i) Check(bound.checks->Check(i) == ContextError::None, "every indexed proof passes");
    Check(record->Inputs()[0].kind == InputKind::V3 && record->Inputs()[6].kind == InputKind::Native, "native/v3 proofs remain distinct");
    Check(bound.checks->Check(UINT32_MAX) == ContextError::InputIndex && CheckInput(request, 7, record) == ContextError::InputIndex, "invalid indexes reject");
    auto identical = View(f);
    Check(Bind(identical, record).checks != nullptr, "equal distinct immutable request binds by complete value");
    auto reject_binding = [&](RequestPtr changed) {
        Check(!Bind(changed, record).checks && Bind(changed, record).error == ContextError::BindingMismatch, "incompatible complete binding rejects");
        Check(CheckInput(changed, 0, record) == ContextError::BindingMismatch, "per-input adapter cannot bypass failed binding");
        ++mismatches;
    };
    const auto original = f.tx;
    const auto spent = f.spent;
    f.tx.version++; reject_binding(View(f)); f.tx = original;
    f.tx.vin[0].scriptWitness.stack[0][0] ^= 1; reject_binding(View(f)); f.tx = original;
    f.spent[0].nValue++; reject_binding(View(f)); f.spent = spent;
    f.spent[0].scriptPubKey[3] ^= 1; reject_binding(View(f)); f.spent = spent;
    f.genesis[0] ^= 1; reject_binding(View(f)); f.genesis[0] ^= 1;
    reject_binding(View(f, MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_MINIMALDATA));
    WriteLE32(f.tx.vin[2].scriptWitness.stack[1].data(), 1); reject_binding(View(f)); f.tx = original;
    f.tx.vin[6].scriptWitness.stack[0][0] ^= 1; reject_binding(View(f)); f.tx = original;
    Check(!Prepare(View(f, SCRIPT_VERIFY_NONE)).context, "flag downgrade cannot bypass native witness-v2 checks");
    Check(Prepare(View(f, SCRIPT_VERIFY_NONE)).error == ContextError::UnsupportedFlags, "unsupported flag set explicit");
    f.tx.vin[1].scriptWitness.stack[0][100] ^= 1;
    const auto failed = Prepare(View(f));
    Check(!failed.context && failed.validation_error == Error::Signature && failed.work.verification_calls == 2,
          "failed second group publishes no partially valid context");
    Check(CheckInput(View(f), 2, failed.context) == ContextError::MissingContext, "incomplete result cannot authorize any follower");
    reject_binding(View(f)); f.tx = original;
    const auto digest = record->GroupProof().GroupDigests()[0];
    Check(VerifyDigest(digest, f.keys[0]->pk, f.tx.vin[0].scriptWitness.stack[0]), "known good signature models prior signature-cache hit");
    f.spent[4].scriptPubKey[9] ^= 1;
    const auto ownership = Prepare(View(f));
    Check(!ownership.context && ownership.validation_error == Error::Ownership && ownership.work.verification_calls == 0,
          "known signature cannot bypass current ownership");
    reject_binding(View(f)); f.spent = spent;
    auto appended = Make(1, 1);
    appended.tx.vin[0].prevout.n = 7777;
    appended.tx.vin[0].scriptWitness.stack = f.tx.vin[2].scriptWitness.stack;
    f.tx.vin.push_back(appended.tx.vin[0]); f.spent.push_back(appended.spent[0]);
    reject_binding(View(f));
    Check(!Prepare(View(f)).context, "prior verified group never authorizes a new follower");
    f.tx = original; f.spent = spent;
    const auto new_uncached = Prepare(View(f));
    Check(new_uncached.context && new_uncached.work.verification_calls == 2 && new_uncached.work.ownership_checks == 6,
          "cache absence/eviction leads to complete identical fresh verification");
    Check(!Bind({}, record).checks && !Bind(request, {}).checks && !Prepare({}).context, "null and incomplete inputs reject");
    std::barrier start{4};
    std::vector<std::future<bool>> jobs;
    for (uint32_t worker = 0; worker < 4; ++worker) jobs.push_back(std::async(std::launch::async, [&, worker] {
        start.arrive_and_wait();
        for (uint32_t round = 0; round < 100; ++round) for (uint32_t i = 0; i < 7; ++i)
            if (bound.checks->Check((i * 3 + worker) % 7) != ContextError::None) return false;
        return bound.checks->Check(UINT32_MAX) == ContextError::InputIndex;
    }));
    for (auto& job : jobs) Check(job.get(), "barrier-coordinated concurrent authorization reads");
    std::mt19937_64 rng{0x574f303343545854ULL};
    const auto wire = Wire(f.tx);
    for (size_t i = 0; i < 2000; ++i) {
        auto bytes = wire;
        bytes[rng() % bytes.size()] ^= static_cast<unsigned char>(1 + rng() % 255);
        try {
            const auto tx = DecodeTransaction(bytes);
            const auto changed = Capture(tx, f.spent, f.genesis, request->flags);
            Check(!Bind(changed, record).checks, "seeded context cannot reuse mismatched authorization");
            Check(!Prepare(changed).context, "seeded corrupt native/v3 transaction never publishes context");
        } catch (const Failure&) { ++assertions; }
        ++mutations;
    }
}
}
int main()
{
    try { Tests(); std::cout << "PASS context assertions=" << assertions << " mismatches=" << mismatches << " mutations=" << mutations << '\n'; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL context: " << e.what() << '\n'; return 1; }
}
