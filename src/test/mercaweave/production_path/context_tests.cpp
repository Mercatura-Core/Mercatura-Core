// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/production_path/access.h>
#include <test/mercaweave/context_fixtures.h>
#include <consensus/validation.h>
#include <future>
#include <chrono>
#include <iostream>
#include <random>

using namespace mercaweave;
using namespace mercaweave::test;
namespace {
size_t assertions{}, negatives{}, mutations{};
void Check(bool value, const char* message)
{
    ++assertions;
    if (!value) throw std::runtime_error(message);
}
uint256 Genesis(const B32& value) { return uint256{std::span<const unsigned char>{value}}; }
void Fill(CCoinsViewCache& coins, const Fixture& fixture)
{
    for (size_t i = 0; i < fixture.spent.size(); ++i)
        coins.AddCoin(fixture.tx.vin[i].prevout, Coin{CTxOut(fixture.spent[i]), 1, false}, false);
}
bool Validate(const Fixture& fixture, ValidationCache& cache, bool queued, Statistics* work = nullptr)
{
    // Both the original transaction and the view disappear before queued jobs
    // run. The signature cache stays alive, as required by native CScriptCheck.
    std::vector<CScriptCheck> jobs;
    {
        CCoinsView empty;
        CCoinsViewCache coins{&empty};
        Fill(coins, fixture);
        TxValidationState state;
        const CTransaction tx(fixture.tx);
        LOCK(cs_main);
        if (!CheckInputScriptsWithPQGroups(tx, state, coins, STANDARD_SCRIPT_VERIFY_FLAGS,
                cache, queued ? &jobs : nullptr, ProductionPathTestAccess::Execution(fixture.genesis),
                Genesis(fixture.genesis), work)) return false;
    }
    if (queued) {
        std::reverse(jobs.begin(), jobs.end()); // followers before anchors
        CCheckQueue<CScriptCheck> queue{3, 2};
        CCheckQueueControl<CScriptCheck> control(queue);
        control.Add(std::move(jobs));
        return !control.Complete();
    }
    return true;
}
void Shapes()
{
    ValidationCache cache{1 << 20, 1 << 20};
    for (const auto [n,g,v] : {std::tuple{1,1,0}, {10,1,0}, {53,1,0}, {100,1,0},
            {100,10,0}, {100,100,0}, {1000,1,0}, {1024,1,0}, {256,256,0},
            {4096,4,0}, {1,1,1}, {53,1,2}, {10,2,1}}) {
        auto fixture = MixedFixture(n,g,v);
        Statistics work;
        const auto started = std::chrono::steady_clock::now();
        Check(Validate(fixture,cache,false,&work), "inline production Script");
        const auto inline_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        Check(work.verification_calls == size_t(g), "exact one ML-DSA verification per group");
        Check(work.body_hash_passes == 1 && work.manifest_hash_passes == 1, "single hash passes");
        Check(Validate(fixture,cache,true), "owned asynchronous reversed jobs");
        const auto bytes = Measure(fixture.tx);
        if (n == 100 && g == 1 && !v) Check(bytes.total == 13317, "unchanged 100-input vector");
        std::cout << "{\"v3\":" << n << ",\"groups\":" << g << ",\"v2\":" << v
                  << ",\"bytes\":" << bytes.total << ",\"weight\":" << bytes.weight
                  << ",\"group_verifications\":" << work.verification_calls << ",\"inline_us\":" << inline_us << "}\n";
    }
}
void Negatives()
{
    auto good = MixedFixture(10,2,2);
    ValidationCache cache{1 << 20, 1 << 20};
    for (int kind = 0; kind < 16; ++kind) {
        auto tx = good.tx;
        auto spent = good.spent;
        auto genesis = good.genesis;
        switch (kind) {
        case 0: tx.vin[0].scriptWitness.stack[0][100] ^= 1; break;
        case 1: tx.vin[1].scriptWitness.stack[0][100] ^= 1; break;
        case 2: tx.vin[0].scriptWitness.stack[1][100] ^= 1; break;
        case 3: tx.vin[2].scriptWitness.stack[0][0] ^= 1; break;
        case 4: tx.vin[2].scriptWitness.stack[1][0] = 2; break;
        case 5: --tx.vout[0].nValue; break;
        case 6: --spent[0].nValue; break;
        case 7: spent[9].scriptPubKey[3] ^= 1; break;
        case 8: --tx.vin[0].nSequence; break;
        case 9: ++tx.nLockTime; break;
        case 10: ++tx.version; break;
        case 11: tx.vin[0].scriptWitness.stack[0].push_back(0); break;
        case 12: tx.vin[0].scriptWitness.stack[1].pop_back(); break;
        case 13: tx.vin[10].scriptWitness.stack[0][100] ^= 1; break;
        case 14: std::swap(tx.vin[0],tx.vin[1]); break;
        default: genesis[0] ^= 1; break;
        }
        Fixture bad;
        bad.tx = tx; bad.spent = spent; bad.genesis = genesis;
        Check(!Validate(bad,cache,false) && !Validate(bad,cache,true), "negative actual Script entry");
        ++negatives;
    }
    CCoinsView empty;
    CCoinsViewCache coins{&empty}; Fill(coins,good);
    TxValidationState state;
    LOCK(cs_main);
    Check(!CheckInputScriptsWithPQGroups(CTransaction(good.tx),state,coins,STANDARD_SCRIPT_VERIFY_FLAGS & ~SCRIPT_VERIFY_MERCAWEAVE,
            cache,nullptr,{},Genesis(good.genesis)), "missing isolated capability fails closed");
    ++negatives;
    TxValidationState activated;
    Check(CheckInputScriptsWithPQGroups(CTransaction(good.tx),activated,coins,STANDARD_SCRIPT_VERIFY_FLAGS,
            cache,nullptr,{},Genesis(good.genesis)), "activated flags select the normal immutable group context");
}
void Mutations()
{
    auto good = MixedFixture(10,2,0);
    ValidationCache cache{1 << 20, 1 << 20};
    std::mt19937_64 rng{0x574f303743435458ULL};
    for (size_t i = 0; i < 1000; ++i) {
        Fixture bad; bad.tx=good.tx; bad.spent=good.spent; bad.genesis=good.genesis;
        auto& signature = bad.tx.vin[i % 2].scriptWitness.stack[0];
        signature[rng() % signature.size()] ^= 1U << (rng() % 8);
        Check(!Validate(bad,cache,false), "seeded signature mutation"); ++mutations;
    }
}
}
int main()
{
    try {
        Shapes(); Negatives(); Mutations();
        std::cout << "{\"passed\":true,\"assertions\":" << assertions
                  << ",\"negative_cases\":" << negatives << ",\"mutations\":" << mutations << "}\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
