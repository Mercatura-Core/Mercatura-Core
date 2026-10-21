// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

// Link the actual CUDA backend's host object against a deterministic fake CUDA
// runtime. No driver/device instructions execute in this separate test binary.
#include <cuda_runtime.h>
#ifndef MERCATURA_CUDA_MIN_ARCH
#define MERCATURA_CUDA_MIN_ARCH 75
#endif
#include <mining/gpu_backend.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
struct Launch { std::string name; unsigned grid, block, count, begin, end; uintptr_t scratch{0}; };
struct Runtime {
    std::map<const void*, std::string> kernels;
    std::set<void*> allocations;
    std::map<void*, size_t> allocation_bytes;
    size_t free_memory{8ULL << 30}, extra_usage{0}, stream_memory{0};
    std::vector<Launch> launches;
    std::vector<mining::GpuHeader> observed_v2_headers;
    std::vector<std::function<void()>> copies;
    std::vector<size_t> synchronized_at;
    dim3 grid, block;
    size_t shared{0};
    void* stream{nullptr};
    unsigned pending{0}, streams{0}, mallocs{0}, transfers{0}, syncs{0};
    unsigned fail_malloc{0}, fail_launch{0}, fail_transfer{0}, fail_sync{0};
    unsigned max_contexts{256}, header_attempts{0}, frees{0}, fail_free{0};
    size_t scratchpad_bytes{128ULL << 20};
    int compute_units{325}, max_grid{2147483647}, active_blocks{1};
    cudaError_t allocation_error{cudaErrorMemoryAllocation};
    cudaError_t last_error{cudaSuccess}, allocation_last_error{cudaSuccess};
    bool dirty_retry{false};
    std::function<void()> on_allocation;
    bool freed_pending{false};
    void Reset()
    {
        if (pending) throw std::runtime_error{"Reset with pending CUDA work"};
        launches.clear(); copies.clear(); synchronized_at.clear();
        observed_v2_headers.clear();
        mallocs = transfers = syncs = 0;
        fail_malloc = fail_launch = fail_transfer = fail_sync = 0;
        max_contexts = 256; header_attempts = frees = fail_free = 0;
        scratchpad_bytes = 128ULL << 20; compute_units = 325; max_grid = 2147483647; active_blocks = 1;
        allocation_error = cudaErrorMemoryAllocation;
        last_error = allocation_last_error = cudaSuccess;
        dirty_retry = false; on_allocation = {};
        freed_pending = false;
        extra_usage = 0;
        stream_memory = 0;
    }
};
Runtime& State() { static Runtime runtime; return runtime; }
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error{message};
}
void ExpectFailure(const std::function<void()>& action)
{
    bool failed{false};
    try { action(); } catch (const std::runtime_error&) { failed = true; }
    Require(failed, "Expected an injected CUDA error");
    Require(State().pending == 0, "Error returned before queued work drained");
    Require(!State().freed_pending, "Freed device memory while work was queued");
}
void CheckSchedule(unsigned count, bool v2 = false)
{
    const auto& s{State()};
    if (v2) {
        Require(s.launches.size() == 194 && s.synchronized_at.size() == 195, "Wrong bounded V2 launch cadence");
        unsigned i{0};
        const auto check = [&](const char* name, unsigned block, unsigned begin, unsigned end) {
            const auto& launch{s.launches.at(i++)};
            Require(launch.name == name && launch.grid == count && launch.block == block && launch.count == count &&
                    launch.begin == begin && launch.end == end, "Wrong V2 launch bounds or geometry");
        };
        check("mh_init_v2", 32, 0, 0);
        for (unsigned begin{0}; begin < 262144; begin += 4096) check("mh_fill_v2", 1, begin, begin + 4096);
        for (unsigned begin{0}; begin < 4096; begin += 64) check("mh_bind_v2", 32, begin, begin + 64);
        for (unsigned begin{1}; begin <= 16384; begin += 256) check("mh_mix_v2", 32, begin, begin + 255);
        check("mh_final_v2", 32, 0, 0);
        for (unsigned n{0}; n < 194; ++n) Require(s.synchronized_at[n] == n + 1, "V2 kernel was not drained individually");
        Require(s.synchronized_at.back() == 194 && s.pending == 0, "V2 output transfer not drained");
        return;
    }
    Require(s.launches.size() == (v2 ? 98U : 770U), "Wrong total kernel count");
    Require(s.synchronized_at.size() == (v2 ? 6U : 31U), "Wrong synchronization cadence");
    size_t index{0};
    auto kernel = [&](const char* name, unsigned block, unsigned begin, unsigned end) {
        const auto& launch{s.launches.at(index++)};
        Require(launch.name == std::string{name} + (v2 ? "_v2" : "") && launch.grid == count && launch.block == block && launch.count == count &&
                launch.begin == begin && launch.end == end, "Wrong kernel, batch bounds or launch geometry");
    };
    kernel("mh_init", 32, 0, 0);
    for (unsigned begin{0}; begin < (v2 ? 262144U : 2097152U); begin += 16384) kernel("mh_fill", 1, begin, begin + 16384);
    for (unsigned begin{0}; begin < (v2 ? 4096U : 32768U); begin += 64) kernel("mh_bind", 32, begin, begin + 64);
    for (unsigned begin{1}; begin <= (v2 ? 16384U : 131072U); begin += 1024) kernel("mh_mix", 32, begin, begin + 1023);
    kernel("mh_final", 32, 0, 0);
    std::vector<size_t> expected{1};
    for (size_t i{33}; i <= 129; i += 32) expected.push_back(i);
    for (size_t i{161}; i <= 641; i += 32) expected.push_back(i);
    for (size_t i{657}; i <= 769; i += 16) expected.push_back(i);
    expected.push_back(770); expected.push_back(770); // Final kernel, output copy.
    if (v2) expected = {1, 49, 81, 97, 98, 98};
    Require(s.synchronized_at == expected, "Queued groups crossed a synchronization boundary");
    Require(s.pending == 0, "Successful batch left queued work");
}
} // namespace

// CUDA registration hooks keep this executable independent of libcudart and a
// physical GPU. The real application continues to link the actual CUDA runtime.
extern "C" {
void** CUDARTAPI __cudaRegisterFatBinary(void*) { static void* handle; return &handle; }
void CUDARTAPI __cudaRegisterFatBinaryEnd(void**) {}
void CUDARTAPI __cudaUnregisterFatBinary(void**) {}
void CUDARTAPI __cudaRegisterVar(void**, char*, char*, const char*, int, size_t, int, int) {}
void CUDARTAPI __cudaRegisterFunction(void**, const char* host, char*, const char* name, int, uint3*, uint3*, dim3*, dim3*, int*)
{
    State().kernels[host] = name;
}
unsigned CUDARTAPI __cudaPushCallConfiguration(dim3 grid, dim3 block, size_t shared, void* stream)
{
    auto& s{State()}; s.grid = grid; s.block = block; s.shared = shared; s.stream = stream; return 0;
}
cudaError_t CUDARTAPI __cudaPopCallConfiguration(dim3* grid, dim3* block, size_t* shared, void* stream)
{
    const auto& s{State()}; *grid = s.grid; *block = s.block; *shared = s.shared;
    *static_cast<cudaStream_t*>(stream) = static_cast<cudaStream_t>(s.stream); return cudaSuccess;
}
cudaError_t CUDARTAPI cudaSetDevice(int) { return cudaSuccess; }
cudaError_t CUDARTAPI cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(int* blocks, const void*, int, size_t, unsigned)
{
    *blocks = State().active_blocks; return cudaSuccess;
}
cudaError_t CUDARTAPI cudaGetDeviceCount(int* count) { *count = 1; return cudaSuccess; }
cudaError_t CUDARTAPI cudaGetDeviceProperties(cudaDeviceProp* prop, int)
{
    *prop = {}; std::strcpy(prop->name, "Fake CUDA runtime (no hardware)");
    // Match the binary's configured minimum, including RTX 3050-only builds.
    prop->major = MERCATURA_CUDA_MIN_ARCH / 10;
    prop->minor = MERCATURA_CUDA_MIN_ARCH % 10;
    prop->multiProcessorCount = State().compute_units;
    prop->maxGridSize[0] = State().max_grid;
    prop->totalGlobalMem = 8ULL << 30; return cudaSuccess;
}
cudaError_t CUDARTAPI cudaMemGetInfo(size_t* available, size_t* total)
{
    *total = std::max<size_t>(8ULL << 30, State().free_memory);
    *available = State().free_memory;
    if (State().streams) *available -= State().stream_memory;
    for (const auto& [pointer, bytes] : State().allocation_bytes) *available -= bytes;
    if (!State().allocation_bytes.empty()) *available -= State().extra_usage;
    return cudaSuccess;
}
const char* CUDARTAPI cudaGetErrorString(cudaError_t) { return "injected CUDA failure"; }
cudaError_t CUDARTAPI cudaStreamCreateWithFlags(cudaStream_t* stream, unsigned)
{
    ++State().streams; *stream = reinterpret_cast<cudaStream_t>(1); return cudaSuccess;
}
cudaError_t CUDARTAPI cudaStreamDestroy(cudaStream_t)
{
    --State().streams; return cudaSuccess;
}
cudaError_t CUDARTAPI cudaMalloc(void** pointer, size_t bytes)
{
    auto& s{State()};
    if (bytes < (128ULL << 20) && s.allocation_bytes.empty()) ++s.header_attempts;
    if (bytes >= (128ULL << 20)) {
        // A scratchpad probe owns exactly its headers and states, never buffers
        // left from a previous successful or partial allocation.
        s.dirty_retry |= s.allocations.size() != 2;
    }
    if (s.on_allocation) s.on_allocation();
    if (++s.mallocs == s.fail_malloc || bytes / s.scratchpad_bytes > s.max_contexts) {
        s.last_error = s.allocation_last_error == cudaSuccess ? s.allocation_error : s.allocation_last_error;
        return s.allocation_error;
    }
    // Kernels are never executed here: a scratchpad needs only a distinct token.
    // Track its requested VRAM size without reserving gigabytes of host memory.
    *pointer = std::calloc(1, bytes >= s.scratchpad_bytes ? 1 : bytes);
    if (!*pointer) return s.last_error = cudaErrorMemoryAllocation;
    s.allocations.insert(*pointer); s.allocation_bytes[*pointer] = bytes; return cudaSuccess;
}
cudaError_t CUDARTAPI cudaFree(void* pointer)
{
    auto& s{State()}; s.freed_pending |= s.pending != 0;
    if (++s.frees == s.fail_free) return cudaErrorInvalidValue;
    s.allocations.erase(pointer); s.allocation_bytes.erase(pointer); std::free(pointer); return cudaSuccess;
}
cudaError_t CUDARTAPI cudaMemcpyAsync(void* dst, const void* src, size_t bytes, cudaMemcpyKind, cudaStream_t)
{
    auto& s{State()};
    s.copies.emplace_back([=] { std::memcpy(dst, src, bytes); }); ++s.pending;
    return ++s.transfers == s.fail_transfer ? cudaErrorInvalidValue : cudaSuccess;
}
cudaError_t CUDARTAPI cudaLaunchKernel(const void* function, dim3 grid, dim3 block, void** args, size_t, cudaStream_t)
{
    auto& s{State()};
    const auto& name{s.kernels.at(function)};
    const bool init{name == "mh_init" || name == "mh_init_v2"};
    if (name == "mh_init_v2") {
        const auto* headers{reinterpret_cast<const mining::GpuHeader*>(*static_cast<unsigned char**>(args[0]))};
        const auto count{*static_cast<unsigned*>(args[2])};
        s.copies.emplace_back([headers, count] {
            State().observed_v2_headers.insert(State().observed_v2_headers.end(), headers, headers + count);
        });
    }
    s.launches.push_back({name, grid.x, block.x, *static_cast<unsigned*>(args[init ? 2 : 4]),
                         init ? 0 : *static_cast<unsigned*>(args[5]), init ? 0 : *static_cast<unsigned*>(args[6])});
    if (!init && name.ends_with("_v2"))
        s.launches.back().scratch = reinterpret_cast<uintptr_t>(*static_cast<unsigned long long**>(args[2]));
    ++s.pending; return cudaSuccess;
}
#if CUDART_VERSION >= 13000
// CUDA 13 uses an opaque kernel handle for the same host launch contract.
// These stubs remain confined to the fake-runtime executable.
cudaError_t CUDARTAPI __cudaGetKernel(cudaKernel_t* kernel, const void* function)
{
    *kernel = reinterpret_cast<cudaKernel_t>(const_cast<void*>(function));
    return cudaSuccess;
}
cudaError_t CUDARTAPI __cudaLaunchKernel(cudaKernel_t kernel, dim3 grid, dim3 block, void** args,
                                       size_t shared, cudaStream_t stream)
{
    return cudaLaunchKernel(reinterpret_cast<const void*>(kernel), grid, block, args, shared, stream);
}
#endif
cudaError_t CUDARTAPI cudaGetLastError()
{
    auto& s{State()};
    const auto error{s.fail_launch && s.launches.size() == s.fail_launch ? cudaErrorLaunchFailure : s.last_error};
    s.last_error = cudaSuccess;
    return error;
}
cudaError_t CUDARTAPI cudaStreamSynchronize(cudaStream_t)
{
    auto& s{State()};
    for (auto& copy : s.copies) copy();
    s.copies.clear(); s.pending = 0; s.synchronized_at.push_back(s.launches.size());
    return ++s.syncs == s.fail_sync ? cudaErrorLaunchFailure : cudaSuccess;
}
} // extern "C"

int main()
{
    try {
        auto& state{State()};
        const auto discovery{mining::DiscoverCudaDevices()};
        Require(discovery.devices.size() == 1 && discovery.devices[0].supported, "Fake CUDA discovery failed");
        const auto device{discovery.devices[0]};
        state.free_memory = 6ULL << 30;
        {
            auto automatic{mining::CreateCudaBackend(device, 0)};
            Require(automatic->Contexts() == 43, "Automatic did not select the maximum safe fresh-memory count");
        }
        state.stream_memory = 256ULL << 20;
        {
            auto automatic{mining::CreateCudaBackend(device, 0)};
            Require(automatic->Contexts() == 41, "Automatic budget query preceded runtime/stream initialization");
        }
        state.Reset();
        {
            auto manual{mining::CreateCudaBackend(device, 36)};
            Require(manual->Contexts() == 36 && manual->MemoryBytes() == (4608ULL << 20), "Explicit 36-context allocation failed");
        }
        // Discovery's old 8 GiB snapshot must not authorize a now-unsafe count.
        state.Reset(); state.free_memory = 5ULL << 30;
        ExpectFailure([&] { mining::CreateCudaBackend(device, 36); });
        Require(state.mallocs == 0 && state.streams == 0, "Stale memory budget allocated buffers");
        // Simulate another application taking VRAM after the preflight query.
        state.Reset(); state.free_memory = 5200ULL << 20; state.extra_usage = 512ULL << 20;
        ExpectFailure([&] { mining::CreateCudaBackend(device, 36); });
        Require(state.allocations.empty() && state.streams == 0, "Lost headroom leaked allocations");
        state.Reset(); state.free_memory = 6ULL << 30;
        for (unsigned fail{1}; fail <= 4; ++fail) {
            state.Reset(); state.fail_malloc = fail;
            ExpectFailure([&] { mining::CreateCudaBackend(device, 36); });
            Require(state.allocations.empty() && state.streams == 0, "Advanced manual allocation failure leaked resources");
        }
        // Exercise the real CUDA allocation path with deterministic memory
        // pressure. Hash kernels still never execute in this host-only test.
        state.Reset(); state.max_contexts = 39;
        {
            auto automatic{mining::CreateCudaBackend(device, 0)};
            Require(automatic->Contexts() == 39 && automatic->EstimatedContexts() == 43, "Automatic fallback did not find the highest viable count");
            Require(state.header_attempts <= 20 && !state.dirty_retry, "Excessive retries or dirty CUDA probe");
            Require(state.allocations.size() == 4, "Final CUDA allocation is incomplete");
            Require(state.last_error == cudaSuccess, "Handled allocation OOM poisoned CUDA error state");
            const auto syncs_before_hash{state.syncs};
            Require(automatic->Hash(std::vector<mining::GpuHeader>(1), [] { return false; }).size() == 1,
                    "First hash batch after fallback failed");
            Require(state.launches.size() == 770 && state.syncs - syncs_before_hash == 31 && state.pending == 0,
                    "First post-fallback batch changed the validated launch/synchronization schedule");
        }
        Require(state.allocations.empty() && state.streams == 0, "Fallback leaked CUDA resources");
        state.Reset(); state.max_contexts = 0;
        ExpectFailure([&] { mining::CreateCudaBackend(device, 0); });
        Require(state.allocations.empty() && state.streams == 0 && state.header_attempts <= 20, "No-viable CUDA fallback leaked or exceeded retry bound");
        state.Reset(); state.max_contexts = 35;
        ExpectFailure([&] { mining::CreateCudaBackend(device, 36); });
        Require(state.header_attempts == 1 && state.allocations.empty(), "Manual CUDA request was silently reduced");
        state.Reset(); state.max_contexts = 39; state.allocation_error = cudaErrorInvalidValue;
        ExpectFailure([&] { mining::CreateCudaBackend(device, 0); });
        Require(state.header_attempts == 1 && state.allocations.empty(), "Unrelated CUDA error triggered fallback");
        state.Reset(); state.max_contexts = 39; state.allocation_last_error = cudaErrorLaunchFailure;
        ExpectFailure([&] { mining::CreateCudaBackend(device, 0); });
        Require(state.header_attempts == 1 && state.allocations.empty(), "OOM handling masked a different CUDA driver error");
        for (const bool release_error : {false, true}) {
            state.Reset(); state.max_contexts = 39;
            if (release_error) state.fail_free = 1;
            else state.fail_sync = 1;
            ExpectFailure([&] { mining::CreateCudaBackend(device, 0); });
            Require(state.header_attempts == 1, "CUDA cleanup driver error triggered allocation retries");
            Require(state.allocations.empty() && state.streams == 0, "Final cleanup did not release a failed probe");
        }
        state.Reset(); state.max_contexts = 39;
        bool stopping{false};
        state.on_allocation = [&] { stopping = true; };
        ExpectFailure([&] { mining::CreateCudaBackend(device, 0, [&] { return stopping; }); });
        Require(state.header_attempts == 1 && state.allocations.empty() && state.streams == 0, "Cancelled fallback retried or leaked CUDA resources");
        // A lost post-allocation reserve is recoverable in Automatic mode.
        state.Reset(); state.extra_usage = 512ULL << 20;
        {
            auto automatic{mining::CreateCudaBackend(device, 0)};
            Require(automatic->EstimatedContexts() == 43 && automatic->Contexts() == 39, "Automatic did not recover the required post-allocation reserve");
        }
        state.Reset(); state.free_memory = 8ULL << 30;
        std::vector<mining::GpuHeader> full(2), partial(1);
        {
            auto backend{mining::CreateCudaBackend(device, 2)};
            state.Reset();
            unsigned polls{0};
            Require(backend->Hash(full, [&] { ++polls; return false; }).size() == 2, "Incomplete full batch");
            CheckSchedule(2);
            const unsigned checkpoints{polls};
            state.Reset();
            Require(backend->Hash(partial, [] { return false; }).size() == 1, "Incomplete partial batch");
            CheckSchedule(1);
            // Cancel at every callback, including while kernels are queued in
            // every phase, then reuse the same buffers for a complete new hash.
            for (unsigned stop{1}; stop <= checkpoints; ++stop) {
                state.Reset(); polls = 0;
                Require(backend->Hash(full, [&] { return ++polls >= stop; }).empty(), "Cancelled batch escaped");
                Require(state.pending == 0, "Cancellation failed to drain the stream");
                state.Reset();
                Require(backend->Hash(partial, [] { return false; }).size() == 1, "Restart after cancellation failed");
            }
            for (unsigned fail : {1U, 2U, 129U, 130U, 641U, 642U, 769U, 770U}) {
                state.Reset(); state.fail_launch = fail;
                ExpectFailure([&] { backend->Hash(full, [] { return false; }); });
            }
            for (unsigned fail{1}; fail <= 31; ++fail) {
                state.Reset(); state.fail_sync = fail;
                ExpectFailure([&] { backend->Hash(full, [] { return false; }); });
            }
            for (unsigned fail : {1U, 2U}) {
                state.Reset(); state.fail_transfer = fail;
                ExpectFailure([&] { backend->Hash(full, [] { return false; }); });
            }
            for (unsigned fail : {1U, 4U, 200U, 700U, checkpoints}) {
                state.Reset(); polls = 0;
                ExpectFailure([&] {
                    backend->Hash(full, [&] {
                        if (++polls == fail) throw std::runtime_error{"Injected cancellation callback failure"};
                        return false;
                    });
                });
            }
            state.Reset();
            ExpectFailure([&] { backend->Hash({}, [] { return false; }); });
            const std::vector<mining::GpuHeader> oversized(3);
            ExpectFailure([&] { backend->Hash(oversized, [] { return false; }); });
            Require(state.allocations.size() == 4, "Lost a live device allocation");
            std::cout << checkpoints << " cancellation checkpoints and restarts passed\n";
        }
        Require(state.allocations.empty() && state.streams == 0 && !state.freed_pending, "Backend cleanup leaked resources");
        for (unsigned fail{1}; fail <= 4; ++fail) {
            state.Reset(); state.fail_malloc = fail;
            ExpectFailure([&] { mining::CreateCudaBackend(device, 2); });
            Require(state.allocations.empty() && state.streams == 0, "Partial initialization leaked resources");
        }
        auto v2_device{device};
        v2_device.pow_version = mercahash::Version::V2;
        state.Reset(); state.scratchpad_bytes = 16ULL << 20; state.max_contexts = UINT_MAX; state.free_memory = 8ULL << 30;
        {
            auto automatic{mining::CreateCudaBackend(v2_device, 0)};
            Require(automatic->Contexts() == mining::CalculateGpuBudget(8ULL << 30, 8ULL << 30, 0, mining::GpuMemorySource::CUDA_FREE, mercahash::Version::V2).contexts, "V2 automatic budget did not use the actual scratchpad size");
            Require(automatic->MemoryBytes() == uint64_t{automatic->Contexts()} * ((16ULL << 20) + 240), "V2 automatic scratchpad byte count is wrong");
        }
        {
            auto backend{mining::CreateCudaBackend(v2_device, 2)};
            Require(backend->PoWVersion() == mercahash::Version::V2, "CUDA lost V2 selection");
            Require(backend->MemoryBytes() == (32ULL << 20) + 480, "V2 per-context scratchpad layout is wrong");
            state.Reset(); state.scratchpad_bytes = 16ULL << 20; state.max_contexts = UINT_MAX; unsigned polls{0};
            Require(backend->Hash(full, [&] { ++polls; return false; }).size() == 2, "Incomplete V2 full batch");
            CheckSchedule(2, true);
            const unsigned checkpoints{polls};
            state.Reset(); state.scratchpad_bytes = 16ULL << 20; state.max_contexts = UINT_MAX;
            Require(backend->Hash(partial, [] { return false; }).size() == 1, "Incomplete V2 partial batch");
            CheckSchedule(1, true);
            for (unsigned stop{1}; stop <= checkpoints; ++stop) {
                state.Reset(); state.scratchpad_bytes = 16ULL << 20; state.max_contexts = UINT_MAX; polls = 0;
                Require(backend->Hash(full, [&] { return ++polls >= stop; }).empty(), "Cancelled V2 work escaped");
                Require(state.pending == 0, "V2 cancellation did not drain the stream");
                state.Reset(); state.scratchpad_bytes = 16ULL << 20; state.max_contexts = UINT_MAX;
                Require(backend->Hash(partial, [] { return false; }).size() == 1, "V2 buffer reuse after cancellation failed");
            }
            for (unsigned fail : {1U, 2U, 17U, 18U, 49U, 81U, 82U, 97U, 98U, 129U, 193U, 194U}) {
                state.Reset(); state.scratchpad_bytes = 16ULL << 20; state.max_contexts = UINT_MAX; state.fail_launch = fail;
                ExpectFailure([&] { backend->Hash(full, [] { return false; }); });
            }
            for (unsigned fail : {1U, 2U, 65U, 129U, 193U, 194U, 195U}) {
                state.Reset(); state.scratchpad_bytes = 16ULL << 20; state.max_contexts = UINT_MAX; state.fail_sync = fail;
                ExpectFailure([&] { backend->Hash(full, [] { return false; }); });
            }
            for (unsigned fail : {1U, 2U}) {
                state.Reset(); state.fail_transfer = fail;
                ExpectFailure([&] { backend->Hash(full, [] { return false; }); });
            }
            std::cout << checkpoints << " V2 cancellation checkpoints and restarts passed (fake runtime)\n";
        }
        for (unsigned fail{1}; fail <= 4; ++fail) {
            state.Reset(); state.scratchpad_bytes = 16ULL << 20; state.max_contexts = UINT_MAX; state.fail_malloc = fail;
            ExpectFailure([&] { mining::CreateCudaBackend(v2_device, 2); });
            Require(state.allocations.empty() && state.streams == 0, "V2 partial allocation leaked resources");
        }
        for (const unsigned n : {1U, 128U, 192U, 255U, 256U, 257U, 325U, 512U, 1024U, 8193U}) {
            state.Reset(); state.scratchpad_bytes = 16ULL << 20; state.max_contexts = UINT_MAX;
            state.free_memory = 2 * (uint64_t{n} * ((16ULL << 20) + 4096) + (576ULL << 20));
            {
                auto backend{mining::CreateCudaBackend(v2_device, n)};
                Require(backend->Contexts() == n && backend->BatchContexts() == std::min(n, 325U), "V2 resident/execution capacity mismatch");
                state.Reset();
                std::vector<mining::GpuHeader> input(n);
                for (unsigned i{0}; i < n; ++i)
                    for (unsigned byte{0}; byte < 4; ++byte) input[i][byte] = i >> (8 * byte);
                Require(backend->Hash(input, [] { return false; }).size() == n, "Large V2 batch failed");
                Require(state.observed_v2_headers == input, "V2 resident-window upload lost header order or isolation");
                uintptr_t scratch_base{0};
                for (const auto& [pointer, bytes] : state.allocation_bytes)
                    if (bytes == uint64_t{n} * (16ULL << 20)) scratch_base = reinterpret_cast<uintptr_t>(pointer);
                unsigned covered{0};
                unsigned scratch_covered{0};
                for (const auto& launch : state.launches) {
                    Require(launch.grid <= 325 && launch.grid == launch.count, "V2 launch exceeded the hardware window");
                    if (launch.name == "mh_init_v2") covered += launch.count;
                    if (launch.name == "mh_fill_v2" && launch.begin == 0) {
                        Require(launch.scratch == scratch_base + uint64_t{scratch_covered} * (16ULL << 20), "V2 resident scratchpad windows overlap or skip contexts");
                        scratch_covered += launch.count;
                    }
                }
                Require(covered == n && scratch_covered == n && state.pending == 0, "Large V2 batch lost contexts or left queued work");
            }
            Require(state.allocations.empty() && state.streams == 0, "Large V2 allocation leaked");
        }
        state.Reset(); state.scratchpad_bytes = 16ULL << 20; state.max_contexts = 325; state.free_memory = 32ULL << 30;
        {
            auto backend{mining::CreateCudaBackend(v2_device, 0)};
            Require(backend->Contexts() == 325 && !state.dirty_retry, "V2 allocation fallback did not find the real limit");
        }
        state.Reset(); state.max_grid = 17;
        {
            auto backend{mining::CreateCudaBackend(v2_device, 325)};
            Require(backend->Contexts() == 325 && backend->BatchContexts() == 17, "CUDA grid bound incorrectly capped resident contexts");
        }
        state.Reset(); state.compute_units = 20; state.active_blocks = 16;
        {
            auto backend{mining::CreateCudaBackend(v2_device, 325)};
            Require(backend->BatchContexts() == 320, "V2 execution window did not use driver kernel occupancy");
            state.Reset();
            Require(backend->Hash(std::vector<mining::GpuHeader>(325), [] { return false; }).size() == 325, "Occupancy-derived batch above 256 failed");
            Require(state.launches.front().count == 320, "V2 launch was still constrained to a 256-nonce lease");
        }
        std::cout << "CUDA host scheduling, full/partial batches, faults and cleanup passed (fake runtime; no GPU execution)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
