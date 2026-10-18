// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <bench/mercahash_gpu_bench.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
UniValue DeviceJson(const mining::GpuDevice& device)
{
    UniValue json{UniValue::VOBJ};
    json.pushKV("id", device.id);
    json.pushKV("name", device.name);
    json.pushKV("compute_capability", device.capability);
    json.pushKV("supported", device.supported);
    json.pushKV("error", device.error);
    json.pushKV("total_memory_bytes", device.total_memory);
    json.pushKV("available_memory_bytes", device.available_memory);
    json.pushKV("memory_context_limit", gpu_bench::MemoryLimit(device.available_memory, device.max_allocation));
    return json;
}
UniValue ResourcesJson(const mining::CudaKernelResources& resources)
{
    UniValue json{UniValue::VOBJ};
    json.pushKV("registers_per_thread", resources.registers_per_thread);
    json.pushKV("local_bytes_per_thread", static_cast<uint64_t>(resources.local_bytes_per_thread));
    json.pushKV("static_shared_bytes", static_cast<uint64_t>(resources.static_shared_bytes));
    json.pushKV("theoretical_max_active_32_thread_blocks_per_sm", resources.maximum_active_blocks_per_sm);
    return json;
}
}
int main(int argc, char** argv)
{
    bool json_requested{false};
    try {
        std::vector<std::string_view> args;
        for (int i{1}; i < argc; ++i) { args.emplace_back(argv[i]); if (args.back() == "--json") json_requested = true; }
        const auto options{gpu_bench::Parse(args)};
        if (options.help) { std::cout << gpu_bench::Help(); return 0; }
        const auto discovery_start{gpu_bench::Clock::now()};
        const auto discovery{mining::DiscoverCudaDevices()};
        const double discovery_ms{std::chrono::duration<double, std::milli>(gpu_bench::Clock::now() - discovery_start).count()};
        if (options.list) {
            UniValue report{UniValue::VOBJ}, devices{UniValue::VARR}, errors{UniValue::VARR};
            report.pushKV("schema_version", 1);
            report.pushKV("status", "discovery");
            for (const auto& device : discovery.devices) devices.push_back(DeviceJson(device));
            for (const auto& error : discovery.errors) errors.push_back(error);
            report.pushKV("devices", devices); report.pushKV("errors", errors);
            if (options.json) std::cout << report.write(2) << '\n';
            else {
                for (const auto& error : discovery.errors) std::cout << error << '\n';
                for (const auto& device : discovery.devices)
                    std::cout << device.id << " " << device.name << " " << device.capability
                              << " free=" << device.available_memory << " bytes; safe maximum="
                              << gpu_bench::MemoryLimit(device.available_memory, device.max_allocation)
                              << " contexts; " << (device.supported ? "supported" : device.error) << '\n';
            }
            return std::any_of(discovery.devices.begin(), discovery.devices.end(), [](const auto& device) { return device.supported; }) ? 0 : 77;
        }
        const auto selected{std::find_if(discovery.devices.begin(), discovery.devices.end(),
                                        [&](const auto& device) { return device.id == options.device; })};
        if (selected == discovery.devices.end()) throw std::runtime_error{"CUDA device not found: " + options.device + "; use --list"};
        const auto maximum{gpu_bench::MemoryLimit(selected->available_memory, selected->max_allocation)};
        if (options.contexts > maximum)
            throw std::runtime_error{"Requested " + std::to_string(options.contexts) + " contexts exceeds current safe maximum " + std::to_string(maximum)};
        mining::CudaBenchmarkDiagnostics diagnostics;
        diagnostics.timing_enabled = options.stage_timing;
        const auto initialization_start{gpu_bench::Clock::now()};
        auto backend{mining::CreateCudaBenchmarkBackend(*selected, options.contexts, diagnostics)};
        const double setup_ms{std::chrono::duration<double, std::milli>(gpu_bench::Clock::now() - initialization_start).count()};
        std::cerr << "Checking permanent GPU vectors and CPU parity (excluded from measurement)...\n";
        const auto verification_start{gpu_bench::Clock::now()};
        mining::VerifyGpuBackend(*backend, [] { return false; });
        gpu_bench::VerifyInputs(*backend);
        const double verification_ms{std::chrono::duration<double, std::milli>(gpu_bench::Clock::now() - verification_start).count()};
        // Start after the deterministic corpus used for CPU parity.
        uint64_t cursor{static_cast<uint64_t>(backend->Contexts()) * 3};
        std::cerr << "Warmup: " << options.warmup << " seconds; requested=" << options.contexts
                  << ", actual=" << backend->Contexts() << ", memory maximum=" << diagnostics.memory_context_limit << " contexts\n";
        const auto warmup{gpu_bench::Measure(*backend, options.warmup, cursor)};
        diagnostics.ResetTimings();
        std::cerr << "Measuring " << options.duration << " seconds, stage timing " << (options.stage_timing ? "enabled" : "disabled") << "...\n";
        const auto result{gpu_bench::Measure(*backend, options.duration, cursor)};
        UniValue report{UniValue::VOBJ};
        report.pushKV("schema_version", 1); report.pushKV("status", "ok");
        report.pushKV("algorithm", "MercaHash V1");
        report.pushKV("source_commit", GPU_BENCH_SOURCE_COMMIT);
        report.pushKV("cuda_architectures", GPU_BENCH_CUDA_ARCHITECTURES);
        report.pushKV("device", DeviceJson(*selected));
        report.pushKV("cuda_driver_version", diagnostics.driver_version);
        report.pushKV("cuda_runtime_version", diagnostics.runtime_version);
        report.pushKV("multiprocessors", diagnostics.multiprocessors);
        report.pushKV("requested_contexts", options.contexts);
        report.pushKV("actual_contexts", backend->Contexts());
        report.pushKV("memory_context_limit_at_allocation", diagnostics.memory_context_limit);
        report.pushKV("free_memory_before_allocation_bytes", diagnostics.available_memory_before_allocation);
        report.pushKV("scratchpad_bytes", backend->MemoryBytes());
        report.pushKV("device_discovery_ms", discovery_ms);
        report.pushKV("backend_initialization_ms", setup_ms);
        report.pushKV("verification_ms", verification_ms);
        report.pushKV("warmup_requested_seconds", options.warmup);
        report.pushKV("warmup_elapsed_seconds", warmup.elapsed_seconds);
        report.pushKV("requested_duration_seconds", options.duration);
        report.pushKV("measurement", gpu_bench::MeasurementJson(result));
        report.pushKV("timing", gpu_bench::DiagnosticsJson(diagnostics));
        report.pushKV("init_kernel", ResourcesJson(diagnostics.init_kernel));
        report.pushKV("stage_kernel", ResourcesJson(diagnostics.stage_kernel));
        report.pushKV("verification", "permanent vectors and all slots of two distinct full batches plus a short batch matched Core HashV1");
        report.pushKV("cuda_execution_errors", 0);
        if (options.json) std::cout << report.write(2) << '\n';
        else {
            std::cout << selected->name << " (" << selected->capability << ")\n"
                      << "Contexts: requested=" << options.contexts << " actual=" << backend->Contexts()
                      << " safe maximum=" << diagnostics.memory_context_limit << "\n"
                      << "Completed: " << result.hashes << " hashes in " << result.elapsed_seconds << " seconds = " << result.Hashrate() << " H/s\n"
                      << "Batch latency ms: average=" << result.AverageBatchMs() << " min=" << result.batch_min_ms << " max=" << result.batch_max_ms << "\n"
                      << "Detailed measurements and resources:\n" << report.write(2) << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        if (json_requested) std::cout << gpu_bench::ErrorJson(error.what()).write(2) << '\n';
        else std::cerr << "mercatura-gpu-bench: " << error.what() << '\n';
        return 1;
    }
}
