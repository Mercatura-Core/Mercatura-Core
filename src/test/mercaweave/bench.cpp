// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/fixtures.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sys/resource.h>

using namespace mercaweave;
using namespace mercaweave::test;
namespace {
using Clock = std::chrono::steady_clock;
double Milliseconds(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
double Median(std::vector<double> values) { std::sort(values.begin(), values.end()); return values[values.size() / 2]; }
void Run(size_t n, size_t groups)
{
    auto f = Make(n, groups);
    std::vector<double> signing, verifying, rejecting;
    Statistics stats;
    size_t generated{0};
    // One warm-up and five measured repetitions. No verification cache exists;
    // these are repeated in-process timings, not cold/warm cache measurements.
    for (size_t iteration = 0; iteration < 6; ++iteration) {
        auto start = Clock::now();
        generated = 0;
        Sign(f.tx, f.spent, f.genesis, f.KeyRefs(), [&](B32& out) { ++generated; return ZeroRandomnessForTestsOnly(out); });
        if (generated != groups) throw std::runtime_error("signature generation count");
        const auto sign_ms = Milliseconds(start);
        start = Clock::now();
        const auto result = Verify(f.tx, f.spent, f.genesis);
        const auto verify_ms = Milliseconds(start);
        if (!result.valid || result.stats.verification_calls != groups || result.stats.ownership_checks != n) throw std::runtime_error("benchmark authorization failure");
        stats = result.stats;
        auto& w = f.tx.vin.back().scriptWitness.stack;
        auto& nonce = w[w.size() == 3 ? 2 : 0];
        nonce[0] ^= 1;
        start = Clock::now();
        const auto invalid = Verify(f.tx, f.spent, f.genesis);
        const auto reject_ms = Milliseconds(start);
        nonce[0] ^= 1;
        if (invalid.valid || invalid.error != Error::Ownership || invalid.stats.verification_calls != 0) throw std::runtime_error("invalid ownership benchmark");
        if (iteration) { signing.push_back(sign_ms); verifying.push_back(verify_ms); rejecting.push_back(reject_ms); }
    }
    const auto size = Measure(f.tx);
    // Same deterministic inputs/output and key allocation; genuine v2 signatures
    // generated and checked with the separately implemented research v2 adapter.
    ConvertToV2(f);
    size_t v2_checks{0};
    for (size_t i = 0; i < n; ++i) {
        if (!VerifyV2(f.tx, f.spent, f.genesis, static_cast<uint32_t>(i))) throw std::runtime_error("v2 adapter comparison failure");
        ++v2_checks;
    }
    const auto v2_size = Measure(f.tx);
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const double cpu_ms = (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0 +
                          (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
    const double savings = 100.0 * (1.0 - double(size.total) / double(v2_size.total));
    std::cout << n << ',' << groups << ',' << generated << ',' << stats.verification_calls << ',' << stats.ownership_checks
              << ',' << size.total << ',' << size.witness << ',' << size.stripped << ',' << size.weight << ',' << size.fee_bytes
              << ',' << stats.cost() << ',' << Median(signing) << ',' << Median(verifying) << ',' << Median(rejecting) << ',' << usage.ru_maxrss
              << ',' << cpu_ms << ',' << v2_size.total << ',' << v2_checks << ',' << savings << ','
              << (size.weight <= 400000 ? "within_weight_only" : "exceeds_weight") << ','
              << (v2_size.weight <= 400000 ? "within_weight_only" : "exceeds_weight") << '\n' << std::flush;
}
}
int main()
{
    try {
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "inputs,groups,signatures_per_transaction,verification_calls,ownership_checks,total_bytes,witness_bytes,nonwitness_bytes,weight,fee_bytes_without_sigop_adjustment,group_cost,median_sign_ms,median_verify_ms,median_invalid_ownership_reject_ms,process_peak_rss_kib,cumulative_cpu_ms,v2_adapter_bytes,v2_adapter_verification_calls,savings_percent,v3_weight_check,v2_weight_check\n";
        for (size_t n : {1,10,53,100,500,1000}) {
            Run(n, 1);
            if (n >= 10) Run(n, 10);
            if (n > 10 && n <= MAX_GROUPS) Run(n, n);
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
