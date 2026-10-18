// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <bench/mercahash_gpu_bench.h>
#include <crypto/mercahash.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace gpu_bench {
Options Parse(std::span<const std::string_view> args)
{
    Options options;
    std::set<std::string_view> seen;
    for (size_t i{0}; i < args.size(); ++i) {
        const auto key{args[i]};
        if (!seen.insert(key).second) throw std::runtime_error{"Duplicate option: " + std::string{key}};
        if (key == "--help") options.help = true;
        else if (key == "--list") options.list = true;
        else if (key == "--json") options.json = true;
        else if (key == "--stage-timing") options.stage_timing = true;
        else if (key == "--device" || key == "--contexts" || key == "--duration" || key == "--warmup") {
            if (++i == args.size()) throw std::runtime_error{"Missing value for " + std::string{key}};
            const auto value{args[i]};
            if (key == "--device") {
                if (!value.starts_with("cuda:") || value.size() == 5 ||
                    !std::all_of(value.begin() + 5, value.end(), [](char c) { return c >= '0' && c <= '9'; }))
                    throw std::runtime_error{"--device must be a CUDA identifier such as cuda:0"};
                options.device = value;
            } else {
                unsigned int number{0};
                const auto [end, error]{std::from_chars(value.data(), value.data() + value.size(), number)};
                if (error != std::errc{} || end != value.data() + value.size())
                    throw std::runtime_error{"Expected unsigned integer for " + std::string{key}};
                if (key == "--contexts") {
                    if (number > 256) throw std::runtime_error{"--contexts must be 0 (automatic) through 256; memory limits also apply"};
                    options.contexts = number;
                } else if (key == "--duration") {
                    if (!number || number > 86400) throw std::runtime_error{"--duration must be 1 through 86400 seconds"};
                    options.duration = number;
                } else {
                    if (number > 3600) throw std::runtime_error{"--warmup must be 0 through 3600 seconds"};
                    options.warmup = number;
                }
            }
        } else throw std::runtime_error{"Unknown option: " + std::string{key}};
    }
    if (options.list && (seen.contains("--device") || seen.contains("--contexts") || seen.contains("--duration") ||
                         seen.contains("--warmup") || options.stage_timing))
        throw std::runtime_error{"--list accepts only --json or --help"};
    return options;
}
std::string_view Help()
{
    return "mercatura-gpu-bench --list [--json]\n"
           "mercatura-gpu-bench [--device cuda:0] [--contexts 0..256] [--duration 1..86400]\n"
           "                    [--warmup 0..3600] [--stage-timing] [--json]\n"
           "Defaults: automatic contexts, 60 seconds measured, 5 seconds warmup.\n"
           "Full MercaHash V1; mandatory GPU vectors and CPU parity precede measurement.\n"
           "Stage timing is opt-in and adds CUDA event overhead. Final batches finish\n"
           "and count in full; elapsed time includes the overrun. No node/wallet access.\n"
           "Exit codes: 0 success, 1 error, 77 no usable device with --list.\n";
}
unsigned int MemoryLimit(uint64_t available, uint64_t maximum_allocation)
{
    // Ask the production budget rather than maintaining a second reserve formula.
    unsigned int result{0};
    for (unsigned int count{1}; count <= 256; ++count) {
        try { mining::CalculateGpuBudget(available, maximum_allocation, count); }
        catch (const std::runtime_error&) { break; }
        result = count;
    }
    return result;
}
void FillHeaders(std::span<mining::GpuHeader> headers, uint64_t first)
{
    if (headers.size() > std::numeric_limits<uint64_t>::max() - first)
        throw std::runtime_error{"Deterministic header sequence exhausted"};
    for (size_t slot{0}; slot < headers.size(); ++slot) {
        auto& header{headers[slot]};
        for (size_t byte{0}; byte < header.size(); ++byte) header[byte] = static_cast<unsigned char>(byte * 37 + 11);
        // Explicit little-endian sequence makes all inputs distinct across batches
        // without RNG, node serialization, allocation, or hashing in the hot loop.
        const uint64_t serial{first + slot};
        for (unsigned int byte{0}; byte < 8; ++byte) header[byte] = static_cast<unsigned char>(serial >> (8 * byte));
        for (unsigned int byte{0}; byte < 4; ++byte) header[76 + byte] = static_cast<unsigned char>(serial >> (8 * byte));
    }
}
void VerifyInputs(mining::GpuBackend& backend)
{
    if (!backend.Contexts() || backend.Contexts() > 256) throw std::runtime_error{"Invalid benchmark backend context count"};
    std::vector<unsigned char> scratch(mercahash::SCRATCHPAD_BYTES);
    uint64_t cursor{0};
    // Two full batches prove disjoint slots and scratchpad reuse; the last batch
    // exercises a short launch whenever the backend has more than one context.
    for (const unsigned int count : {backend.Contexts(), backend.Contexts(), std::max(1U, backend.Contexts() - 1)}) {
        std::vector<mining::GpuHeader> headers(count);
        FillHeaders(headers, cursor);
        const auto actual{backend.Hash(headers, [] { return false; })};
        if (actual.size() != headers.size()) throw std::runtime_error{"CPU parity check returned an incomplete GPU batch"};
        for (size_t slot{0}; slot < headers.size(); ++slot) {
            mining::GpuHash expected{};
            mercahash::HashV1(headers[slot], scratch, expected);
            if (actual[slot] != expected)
                throw std::runtime_error{"CUDA/CPU MercaHash V1 digest mismatch at header " + std::to_string(cursor + slot)};
        }
        cursor += count;
    }
}
void Measurement::AddBatch(size_t expected, size_t completed, double milliseconds)
{
    if (!expected || completed != expected) throw std::runtime_error{"Benchmark returned an incomplete hashing batch"};
    if (!std::isfinite(milliseconds) || milliseconds < 0) throw std::runtime_error{"Invalid batch timing"};
    if (completed > std::numeric_limits<uint64_t>::max() - hashes) throw std::runtime_error{"Hash counter overflow"};
    hashes += completed;
    batch_min_ms = batches ? std::min(batch_min_ms, milliseconds) : milliseconds;
    batch_max_ms = std::max(batch_max_ms, milliseconds);
    batch_total_ms += milliseconds;
    ++batches;
}
double Measurement::Hashrate() const { return elapsed_seconds > 0 ? hashes / elapsed_seconds : 0; }
double Measurement::AverageBatchMs() const { return batches ? batch_total_ms / batches : 0; }
Measurement Measure(mining::GpuBackend& backend, unsigned int seconds, uint64_t& cursor,
                    const std::function<Clock::time_point()>& now)
{
    if (!backend.Contexts() || backend.Contexts() > 256) throw std::runtime_error{"Invalid benchmark backend context count"};
    std::vector<mining::GpuHeader> headers(backend.Contexts());
    Measurement result;
    const auto start{now()};
    auto end{start};
    while (end - start < std::chrono::seconds{seconds}) {
        FillHeaders(headers, cursor);
        const auto before{now()};
        const auto hashes{backend.Hash(headers, [] { return false; })};
        end = now();
        result.AddBatch(headers.size(), hashes.size(), std::chrono::duration<double, std::milli>(end - before).count());
        cursor += headers.size();
    }
    result.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}
UniValue MeasurementJson(const Measurement& result)
{
    UniValue json{UniValue::VOBJ};
    json.pushKV("completed_hashes", result.hashes);
    json.pushKV("completed_batches", result.batches);
    json.pushKV("elapsed_seconds", result.elapsed_seconds);
    json.pushKV("hashes_per_second", result.Hashrate());
    json.pushKV("batch_average_ms", result.AverageBatchMs());
    json.pushKV("batch_min_ms", result.batch_min_ms);
    json.pushKV("batch_max_ms", result.batch_max_ms);
    return json;
}
UniValue DiagnosticsJson(const mining::CudaBenchmarkDiagnostics& diagnostics)
{
    UniValue json{UniValue::VOBJ};
    json.pushKV("enabled", diagnostics.timing_enabled);
    json.pushKV("host_synchronization_overhead_ms", UniValue{});
    // Null means unmeasured, never a claimed zero-cost GPU stage.
    const char* names[]{"upload", "initialization", "fill", "binding", "mixing", "finalization", "download"};
    for (size_t i{0}; i < diagnostics.device_ms.size(); ++i) {
        if (diagnostics.timing_enabled) json.pushKV(std::string{names[i]} + "_device_ms", diagnostics.device_ms[i]);
        else json.pushKV(std::string{names[i]} + "_device_ms", UniValue{});
    }
    if (diagnostics.timing_enabled) json.pushKV("host_synchronize_wait_ms", diagnostics.host_synchronize_wait_ms);
    else json.pushKV("host_synchronize_wait_ms", UniValue{});
    return json;
}
UniValue ErrorJson(std::string_view error)
{
    UniValue json{UniValue::VOBJ};
    json.pushKV("schema_version", 1);
    json.pushKV("status", "error");
    json.pushKV("error", std::string{error});
    return json;
}
} // namespace gpu_bench
