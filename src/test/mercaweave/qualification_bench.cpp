// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "fixtures.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <new>
#include <sys/resource.h>

// Counts ONLY ordinary C++ new/new[] requests in a separate untimed pass.
// C realloc/malloc (including CScript's prevector storage) are NOT counted.
namespace allocation {
bool active{false};
size_t calls{0}, bytes{0}, largest{0};
}
void* operator new(std::size_t n)
{
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    if (allocation::active) {
        ++allocation::calls;
        allocation::bytes += n;
        allocation::largest = std::max(allocation::largest, n);
    }
    return p;
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace mercaweave;
using namespace mercaweave::test;
namespace {
using Clock = std::chrono::steady_clock;
volatile size_t sink{0};
struct Timing { double median, worst; };
Timing Time(const std::function<void()>& run)
{
    std::vector<double> samples;
    for (size_t i = 0; i < 14; ++i) {
        const auto start = Clock::now();
        run();
        const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (i >= 3) samples.push_back(ms);
    }
    std::sort(samples.begin(), samples.end());
    return {samples[samples.size() / 2], samples.back()};
}
void Require(bool ok) { if (!ok) throw std::runtime_error("resource qualification invariant failed"); }
void Run(size_t n, size_t g, bool large_output)
{
    auto f = Make(n, g);
    if (large_output) f.tx.vout[0].scriptPubKey = CScript{} << OP_RETURN << Bytes(1 << 20, 0x42);
    SignFixture(f);
    const auto map = Reconstruct(f.tx, f.spent);
    const auto manifest = Manifest(map);
    const auto hashes = TransactionHashes(f.tx, f.spent, f.genesis, map);
    std::vector<H48> digests;
    for (const auto& group : map.groups) digests.push_back(Digest(f.genesis, hashes, group));
    const auto mapping = Time([&] { const auto m = Reconstruct(f.tx, f.spent); sink = m.groups.size(); });
    const auto ownership = Time([&] { Statistics s; CheckOwnership(map, f.spent, f.genesis, s); Require(s.ownership_checks == n); sink = s.ownership_checks; });
    const auto construction = Time([&] { const auto m = Manifest(map); sink = m.size(); });
    const auto body = Time([&] { const auto h = BodyHashes(f.tx, f.spent, f.genesis); Require(h.tx == hashes.tx); sink = h.tx[0]; });
    const auto manifest_hash = Time([&] { const auto h = PQH384("Mercatura/PQGroupAuth/v1/Manifest", manifest); Require(h == hashes.manifest); sink = h[0]; });
    const auto finals = Time([&] {
        for (const auto& group : map.groups) { const auto d = Digest(f.genesis, hashes, group); sink = d[0]; }
    });
    const auto signatures = Time([&] {
        for (size_t k = 0; k < map.groups.size(); ++k) Require(VerifyDigest(digests[k], map.groups[k].pk, f.tx.vin[map.groups[k].anchor].scriptWitness.stack[0]));
    });
    Statistics stats;
    auto verify = [&] {
        const auto r = Verify(f.tx, f.spent, f.genesis);
        Require(r.valid && r.stats.verification_calls == g && r.stats.ownership_checks == n);
        stats = r.stats;
        sink = r.authorized_inputs.size();
    };
    const auto full = Time(verify);
    allocation::calls = allocation::bytes = allocation::largest = 0;
    allocation::active = true;
    verify();
    allocation::active = false;
    const auto calls = allocation::calls, requested = allocation::bytes, largest = allocation::largest;
    auto malformed = f.tx;
    malformed.vin[0].scriptWitness.stack.pop_back();
    const auto malformed_time = Time([&] {
        const auto r = Verify(malformed, f.spent, f.genesis);
        Require(!r.valid && r.stats.verification_calls == 0 && r.stats.ownership_checks == 0);
    });
    auto early = f.tx;
    early.vin[0].scriptWitness.stack[2][0] ^= 1;
    const auto early_time = Time([&] {
        const auto r = Verify(early, f.spent, f.genesis);
        Require(!r.valid && r.error == Error::Ownership && r.stats.verification_calls == 0 && r.stats.ownership_checks == 1);
    });
    auto late = f.tx;
    const auto last_member = map.groups.back().members.back().index;
    auto& witness = late.vin[last_member].scriptWitness.stack;
    witness[witness.size() == 3 ? 2 : 0][0] ^= 1;
    const auto late_time = Time([&] {
        const auto r = Verify(late, f.spent, f.genesis);
        Require(!r.valid && r.error == Error::Ownership && r.stats.verification_calls == 0 && r.stats.ownership_checks == n);
    });
    auto signature = f.tx;
    signature.vin[map.groups.back().anchor].scriptWitness.stack[0][100] ^= 1;
    const auto bad_signature = Time([&] {
        const auto r = Verify(signature, f.spent, f.genesis);
        Require(!r.valid && r.error == Error::Signature && r.stats.verification_calls == g && r.stats.ownership_checks == n);
    });
    const auto wire = Wire(f.tx);
    const auto decoding = Time([&] { const auto tx = DecodeTransaction(wire); sink = tx.vin.size(); });
    // Tiny hostile wire count: preflight must not allocate a declared vector.
    const Bytes oversized{2,0,0,0,0,1,0xfe,0xff,0xff,0xff,0xff};
    allocation::calls = allocation::bytes = allocation::largest = 0;
    allocation::active = true;
    bool rejected{false};
    try { DecodeTransaction(oversized); } catch (const Failure&) { rejected = true; }
    allocation::active = false;
    Require(rejected && allocation::largest < 4096);
    const auto sizes = Measure(f.tx);
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::cout << "inputs,groups,label,bytes,weight,cost,verification_calls,ownership_checks,median_verify_ms,worst_verify_ms,mapping_ms,ownership_ms,manifest_build_ms,components_and_body_ms,manifest_hash_ms,final_digests_ms,mldsa_verify_ms,decode_ms,malformed_ms,early_commitment_ms,late_commitment_ms,late_signature_ms,worst_late_signature_ms,cpp_new_calls,cpp_requested_bytes,cpp_largest_request_bytes,oversized_wire_largest_request_bytes,peak_rss_kib,weight_only\n";
    std::cout << std::fixed << std::setprecision(6) << n << ',' << g << ',' << (large_output ? "large_output_1mib" : "group_bounds")
        << ',' << sizes.total << ',' << sizes.weight << ',' << stats.cost() << ',' << stats.verification_calls << ',' << stats.ownership_checks
        << ',' << full.median << ',' << full.worst << ',' << mapping.median << ',' << ownership.median << ',' << construction.median
        << ',' << body.median << ',' << manifest_hash.median << ',' << finals.median << ',' << signatures.median << ',' << decoding.median
        << ',' << malformed_time.median << ',' << early_time.median << ',' << late_time.median << ',' << bad_signature.median << ',' << bad_signature.worst
        << ',' << calls << ',' << requested << ',' << largest << ',' << allocation::largest << ',' << usage.ru_maxrss
        << ',' << (sizes.weight <= 400000 ? "within_weight_only" : "exceeds_weight") << '\n';
}
}
int main(int argc, char** argv)
{
    try {
        if (argc < 3 || argc > 4) throw std::runtime_error("usage: qualification_bench INPUTS GROUPS [--large-output]");
        if (argc == 4 && std::string_view(argv[3]) != "--large-output") throw std::runtime_error("unknown workload option");
        const auto n = std::stoull(argv[1]), g = std::stoull(argv[2]);
        if (n > MAX_INPUTS || g > MAX_GROUPS || g == 0 || n == 0 || g > n || (n + g - 1) / g > MAX_MEMBERS) throw std::runtime_error("workload outside locked limits");
        Run(n, g, argc == 4 && std::string_view(argv[3]) == "--large-output");
        return 0;
    } catch (const std::exception& e) { allocation::active = false; std::cerr << "FAIL qualification: " << e.what() << '\n'; return 1; }
}
