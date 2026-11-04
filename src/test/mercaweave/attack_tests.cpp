// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/fixtures.h>
#include <test/mercaweave/mutation_seeds.h>
#include <future>
#include <iostream>
#include <random>
#include <set>

using namespace mercaweave;
using namespace mercaweave::test;
namespace {
size_t assertions{0}, attacks{0}, mutations{0};
void Check(bool ok, const char* name) { ++assertions; if (!ok) throw std::runtime_error(name); }
void Reject(const Fixture& f, Error expected, size_t calls)
{
    const auto r = Verify(f.tx, f.spent, f.genesis);
    Check(!r.valid && r.error == expected && !r.authorization && r.authorized_inputs.empty(), "hostile transaction rejects completely");
    Check(r.stats.verification_calls == calls, "rejection-stage signature accounting");
    ++attacks;
}
// Deliberately malicious coordinator: bypass the SAFE Sign entrypoint and sign
// a digest without ownership checking. The top-level verifier must still reject.
void CoordinatorSign(Fixture& f, const Mapping& map, const KeyPair& key)
{
    const auto h = TransactionHashes(f.tx, f.spent, f.genesis, map);
    const auto sig = SignDigest(Digest(f.genesis, h, map.groups[0]), key, B32{});
    f.tx.vin[map.groups[0].anchor].scriptWitness.stack[0].assign(sig.begin(), sig.end());
    Check(VerifyDigest(Digest(f.genesis, h, map.groups[0]), map.groups[0].pk, sig), "attack has a genuinely valid group signature");
}
void UnauthorizedFollowers()
{
    auto f = Make(3, 1);
    SignFixture(f);
    const auto original_tx = f.tx;
    const auto original_spent = f.spent;
    const auto record = Verify(f.tx, f.spent, f.genesis).authorization;
    auto other = Make(2, 2);
    B32 nonce;
    std::copy(f.tx.vin[2].scriptWitness.stack[0].begin(), f.tx.vin[2].scriptWitness.stack[0].end(), nonce.begin());
    f.spent[2].scriptPubKey = Output(f.genesis, other.keys[1]->pk, nonce);
    const auto actual_victim_spent = f.spent;
    CoordinatorSign(f, Reconstruct(f.tx, f.spent), *f.keys[0]);
    Reject(f, Error::Ownership, 0);
    Check(!record->Matches(original_tx, actual_victim_spent, f.genesis), "old record rejects victim's authenticated output context");
    Check(!Verify(f.tx, f.spent, f.genesis, [](const auto&, const auto&, const auto&, auto) { return true; }).valid,
          "permissive non-v3 callback cannot bypass actual v3 ownership");
    // Fabricated metadata is outside the API trust boundary: self-consistent
    // invented outputs can pass, but the authenticated victim outputs cannot.
    Check(Verify(original_tx, original_spent, f.genesis).valid, "fabricated self-consistent metadata cannot be authenticated by this library");
    Check(!Verify(original_tx, actual_victim_spent, f.genesis).valid, "actual authenticated victim script rejects forged follower");
    f.tx = original_tx;
    f.spent = original_spent;
    f.tx.vin[2].scriptWitness.stack[0][0] ^= 1;
    CoordinatorSign(f, Reconstruct(f.tx, f.spent), *f.keys[0]);
    Reject(f, Error::Ownership, 0);
    f.tx = original_tx;
    f.tx.vin[0].scriptWitness.stack[1].assign(other.keys[1]->pk.begin(), other.keys[1]->pk.end());
    CoordinatorSign(f, Reconstruct(f.tx, f.spent), *other.keys[1]);
    Reject(f, Error::Ownership, 0);
    f.tx = original_tx;
    auto map = Reconstruct(f.tx, f.spent);
    std::swap(map.groups[0].members[1], map.groups[0].members[2]);
    bool rejected{false};
    try { Manifest(map); } catch (const Failure& e) { rejected = e.code == Error::Reference; }
    Check(rejected, "reordered supplied manifest is noncanonical"); ++attacks;
    map = Reconstruct(f.tx, f.spent);
    map.groups[0].members.pop_back();
    --map.inputs;
    CoordinatorSign(f, map, *f.keys[0]);
    Reject(f, Error::Signature, 1);
    Check(!record->Matches(f.tx, f.spent, f.genesis), "record does not match coordinator's forged-manifest signature");
    auto multi = Make(6, 2);
    SignFixture(multi);
    const auto multi_tx = multi.tx;
    WriteLE32(multi.tx.vin[2].scriptWitness.stack[1].data(), 1);
    const auto remapped = Reconstruct(multi.tx, multi.spent);
    const auto h = TransactionHashes(multi.tx, multi.spent, multi.genesis, remapped);
    for (size_t k = 0; k < remapped.groups.size(); ++k) {
        const auto sig = SignDigest(Digest(multi.genesis, h, remapped.groups[k]), *multi.keys[k], B32{});
        multi.tx.vin[remapped.groups[k].anchor].scriptWitness.stack[0].assign(sig.begin(), sig.end());
    }
    Reject(multi, Error::Ownership, 0);
    multi.tx = multi_tx;
    multi.tx.vin[1].scriptWitness.stack[1] = multi.tx.vin[0].scriptWitness.stack[1];
    Reject(multi, Error::DuplicateKey, 0);
    multi.tx = multi_tx;
    multi.tx.vin[2].scriptWitness.stack[1].resize(3);
    Reject(multi, Error::Witness, 0);
    multi.tx = multi_tx;
    multi.tx.vin[2].scriptWitness.stack[1].resize(5);
    Reject(multi, Error::Witness, 0);
    multi.tx = multi_tx;
    multi.spent.pop_back();
    Reject(multi, Error::Context, 0);
    f.tx = original_tx;
    f.spent = original_spent;
    std::mt19937_64 rng{FOLLOWER_MUTATION_SEED};
    for (size_t i = 0; i < 10000; ++i) {
        uint32_t ref = static_cast<uint32_t>(rng());
        if (ref == 0) ref = UINT32_MAX;
        WriteLE32(f.tx.vin[2].scriptWitness.stack[1].data(), ref);
        const auto r = Verify(f.tx, f.spent, f.genesis);
        Check(!r.valid && r.error == Error::Reference && r.stats.verification_calls == 0 &&
              r.stats.ownership_checks == 0, "randomized illegal reference rejects before all crypto");
        Check(!record->Matches(f.tx, f.spent, f.genesis), "old authorization never matches a forged reference");
        ++mutations;
    }
}
void TraversalAndConcurrency()
{
    auto f = Make(20, 4);
    SignFixture(f);
    const auto map = Reconstruct(f.tx, f.spent);
    std::set<uint32_t> indexes;
    std::set<PublicKey> keys;
    for (const auto& group : map.groups) {
        Check(keys.insert(group.pk).second, "one group per raw public key");
        for (const auto& m : group.members) Check(indexes.insert(m.index).second, "one membership per input");
    }
    Check(indexes.size() == f.tx.vin.size(), "complete membership coverage");
    const auto h = TransactionHashes(f.tx, f.spent, f.genesis, map);
    auto reversed = map;
    std::reverse(reversed.groups.begin(), reversed.groups.end());
    Statistics stats;
    CheckOwnership(reversed, f.spent, f.genesis, stats);
    Check(stats.ownership_checks == f.tx.vin.size(), "ownership independent of traversal order");
    size_t calls{0};
    for (const auto& group : reversed.groups) {
        Check(VerifyDigest(Digest(f.genesis, h, group), group.pk, f.tx.vin[group.anchor].scriptWitness.stack[0]), "signature independent of group traversal");
        ++calls;
    }
    Check(calls == 4, "one signature check per traversed group");
    for (const bool valid : {true, false}) {
        if (!valid) f.tx.vin[3].scriptWitness.stack[0][100] ^= 1;
        std::vector<std::future<Result>> jobs;
        for (size_t worker = 0; worker < 4; ++worker) jobs.push_back(std::async(std::launch::async, [&] { return Verify(f.tx, f.spent, f.genesis); }));
        for (auto& job : jobs) {
            const auto r = job.get();
            Check(r.valid == valid && r.stats.verification_calls == 4 && r.stats.ownership_checks == 20,
                  "parallel frozen-input validation is deterministic");
        }
    }
}
}
int main()
{
    try {
        UnauthorizedFollowers();
        TraversalAndConcurrency();
        std::cout << "PASS attacks assertions=" << assertions << " attacks=" << attacks << " reference_mutations=" << mutations << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL attacks: " << e.what() << '\n'; return 1; }
}
