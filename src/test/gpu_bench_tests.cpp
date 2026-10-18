// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <bench/mercahash_gpu_bench.h>
#include <crypto/mercahash.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error{message};
}
template <typename F> void Throws(F call)
{
    try { call(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error{"Expected a clear failure"};
}
gpu_bench::Options Parse(std::initializer_list<std::string_view> args)
{
    return gpu_bench::Parse(std::span{args.begin(), args.size()});
}
class FakeBackend final : public mining::GpuBackend {
public:
    bool incomplete{false}, corrupt{false}, fail{false};
    std::vector<size_t> sizes;
    unsigned int Contexts() const override { return 2; }
    uint64_t MemoryBytes() const override { return 256ULL << 20; }
    std::vector<mining::GpuHash> Hash(std::span<const mining::GpuHeader> headers, const std::function<bool()>&) override
    {
        if (fail) throw std::runtime_error{"Injected CUDA execution error"};
        sizes.push_back(headers.size());
        std::vector<mining::GpuHash> result(headers.size());
        std::vector<unsigned char> scratch(mercahash::SCRATCHPAD_BYTES);
        for (size_t slot{0}; slot < headers.size(); ++slot) mercahash::HashV1(headers[slot], scratch, result[slot]);
        if (corrupt) result[0][0] ^= 1;
        if (incomplete) result.pop_back();
        return result;
    }
};
void RunTests()
{
    using namespace gpu_bench;
    const auto options{Parse({"--device", "cuda:1", "--contexts", "20", "--duration", "60", "--warmup", "0", "--json", "--stage-timing"})};
    Require(options.device == "cuda:1" && options.contexts == 20 && options.duration == 60 && options.warmup == 0 && options.json && options.stage_timing, "CLI values");
    Require(Parse({}).contexts == 0 && Parse({"--help"}).help && Parse({"--list", "--json"}).list, "CLI defaults and flags");
    for (auto args : {std::vector<std::string_view>{"--contexts", "257"}, {"--contexts", "-1"}, {"--duration", "0"},
                      {"--duration", "nan"}, {"--duration", "1.5"}, {"--duration", "999999999999999999999"},
                      {"--warmup", "3601"}, {"--device", "opencl:0"}, {"--device", "cuda:"}, {"--duration"},
                      {"--json", "--json"}, {"--list", "--duration", "1"}, {"--bogus"}})
        Throws([&] { gpu_bench::Parse(args); });
    Require(MemoryLimit(512ULL << 20, 512ULL << 20) == 0, "Low memory");
    Require(MemoryLimit(6ULL << 30, 6ULL << 30) == 23, "6 GiB budget");
    Require(MemoryLimit(6ULL << 30, 128ULL << 20) == 1, "Allocation limit");
    Require(MemoryLimit(UINT64_MAX, UINT64_MAX) == 256, "Maximum policy limit");
    Require(mining::CalculateGpuBudget(6ULL << 30, 6ULL << 30).contexts == 16, "Automatic cap unchanged");
    Throws([] { mining::CalculateGpuBudget(6ULL << 30, 6ULL << 30, 24); });
    const uint64_t boundary{2 * (mercahash::SCRATCHPAD_BYTES + 4096ULL) * 5};
    Require(MemoryLimit(boundary, boundary) == 5 && MemoryLimit(boundary - 2, boundary) == 4, "Exact budget boundary");
    std::array<mining::GpuHeader, 2> a{}, b{};
    FillHeaders(a, 0); FillHeaders(b, 0);
    Require(a == b && a[0] != a[1], "Deterministic distinct inputs");
    FillHeaders(b, 2); Require(a[0] != b[0] && a[1] != b[0], "Distinct batches");
    FillHeaders(b, 0x0102030405060708ULL); Require(b[0][0] == 8 && b[0][7] == 1 && b[0][76] == 8, "Little endian inputs");
    Throws([&] { FillHeaders(a, UINT64_MAX); });
    Measurement result;
    Require(result.Hashrate() == 0 && result.AverageBatchMs() == 0, "Empty measurements");
    result.AddBatch(16, 16, 2000); result.AddBatch(16, 16, 3000); result.elapsed_seconds = 5;
    Require(result.hashes == 32 && result.Hashrate() == 6.4 && result.AverageBatchMs() == 2500 && result.batch_min_ms == 2000 && result.batch_max_ms == 3000, "Counting and timing");
    Throws([&] { result.AddBatch(16, 15, 1); });
    Throws([&] { result.AddBatch(0, 0, 1); });
    Throws([&] { result.AddBatch(1, 1, std::numeric_limits<double>::quiet_NaN()); });
    Throws([&] { result.AddBatch(1, 1, -1); });
    const auto before{result.hashes}; Require(before == 32, "Incomplete work not counted");
    UniValue parsed;
    Require(parsed.read(MeasurementJson(result).write()) && parsed["completed_hashes"].getInt<uint64_t>() == 32, "JSON numeric counters");
    const auto error{ErrorJson("CUDA \"error\"\n")};
    Require(parsed.read(error.write()) && parsed["error"].get_str() == "CUDA \"error\"\n", "JSON escaped errors");
    mining::CudaBenchmarkDiagnostics diagnostics;
    Require(DiagnosticsJson(diagnostics)["fill_device_ms"].isNull(), "Disabled timing is null");
    diagnostics.timing_enabled = true; diagnostics.device_ms[2] = 12.5;
    Require(DiagnosticsJson(diagnostics)["fill_device_ms"].get_real() == 12.5, "Timing units");
    diagnostics.ResetTimings(); Require(diagnostics.device_ms[2] == 0, "Warmup timings excluded");
    FakeBackend backend;
    auto instant{Clock::time_point{}}; unsigned int calls{0}; uint64_t cursor{0};
    const auto measured{Measure(backend, 1, cursor, [&] { return instant + std::chrono::seconds{calls++ == 2 ? 2 : 0}; })};
    Require(measured.hashes == 2 && measured.batches == 1 && measured.elapsed_seconds == 2 && measured.Hashrate() == 1 && cursor == 2, "Final overrun batch counted with actual elapsed time");
    Require(Measure(backend, 0, cursor).hashes == 0, "Zero warmup");
    backend.incomplete = true; Throws([&] { Measure(backend, 1, cursor); });
    backend.incomplete = false; backend.fail = true; Throws([&] { Measure(backend, 1, cursor); });
    backend.fail = false; backend.sizes.clear(); VerifyInputs(backend);
    Require(backend.sizes == std::vector<size_t>{2, 2, 1}, "Full, reused and short parity batches");
    backend.corrupt = true; Throws([&] { VerifyInputs(backend); });
    backend.corrupt = false; backend.incomplete = true; Throws([&] { VerifyInputs(backend); });
}
}
int main()
{
    try { RunTests(); std::cout << "GPU benchmark software tests passed (no GPU qualification)\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
