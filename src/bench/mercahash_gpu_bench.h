// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_BENCH_MERCAHASH_GPU_BENCH_H
#define BITCOIN_BENCH_MERCAHASH_GPU_BENCH_H
#include <mining/cuda_diagnostics.h>
#include <univalue.h>
#include <chrono>
#include <functional>
#include <span>
#include <string_view>

namespace gpu_bench {
using Clock = std::chrono::steady_clock;
struct Options {
    bool help{false}, list{false}, json{false}, stage_timing{false};
    std::string device{"cuda:0"};
    unsigned int contexts{0}, duration{60}, warmup{5};
};
Options Parse(std::span<const std::string_view> args);
std::string_view Help();
unsigned int MemoryLimit(uint64_t available, uint64_t maximum_allocation);
void FillHeaders(std::span<mining::GpuHeader> headers, uint64_t first);
void VerifyInputs(mining::GpuBackend& backend);
struct Measurement {
    uint64_t hashes{0}, batches{0};
    double elapsed_seconds{0}, batch_total_ms{0}, batch_min_ms{0}, batch_max_ms{0};
    void AddBatch(size_t expected, size_t completed, double milliseconds);
    double Hashrate() const;
    double AverageBatchMs() const;
};
// Injectable clock lets tests exercise final-batch inclusion without a device.
Measurement Measure(mining::GpuBackend& backend, unsigned int seconds, uint64_t& cursor,
                    const std::function<Clock::time_point()>& now = [] { return Clock::now(); });
UniValue MeasurementJson(const Measurement& result);
UniValue DiagnosticsJson(const mining::CudaBenchmarkDiagnostics& diagnostics);
UniValue ErrorJson(std::string_view error);
} // namespace gpu_bench
#endif // BITCOIN_BENCH_MERCAHASH_GPU_BENCH_H
