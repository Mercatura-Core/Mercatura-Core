// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

// Link the actual CUDA backend's host object against a deterministic fake CUDA
// runtime. No driver/device instructions execute in this separate test binary.
#include <cuda_runtime.h>
#include <mining/gpu_backend.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {
struct Launch { std::string name; unsigned grid, block, count, begin, end; };
struct Runtime {
    std::map<const void*, std::string> kernels;
    std::set<void*> allocations;
    std::map<void*, size_t> allocation_bytes;
    size_t free_memory{8ULL << 30}, extra_usage{0}, stream_memory{0};
    std::vector<Launch> launches;
    std::vector<std::function<void()>> copies;
    std::vector<size_t> synchronized_at;
    dim3 grid, block;
    size_t shared{0};
    void* stream{nullptr};
    unsigned pending{0}, streams{0}, mallocs{0}, transfers{0}, syncs{0};
    unsigned fail_malloc{0}, fail_launch{0}, fail_transfer{0}, fail_sync{0};
    unsigned max_contexts{256}, header_attempts{0}, frees{0}, fail_free{0};
    cudaError_t allocation_error{cudaErrorMemoryAllocation};
    cudaError_t last_error{cudaSuccess}, allocation_last_error{cudaSuccess};
    bool dirty_retry{false};
    std::function<void()> on_allocation;
    bool freed_pending{false};
    void Reset()
    {
        if (pending) throw std::runtime_error{"Reset with pending CUDA work"};
        launches.clear(); copies.clear(); synchronized_at.clear();
        mallocs = transfers = syncs = 0;
        fail_malloc = fail_launch = fail_transfer = fail_sync = 0;
        max_contexts = 256; header_attempts = frees = fail_free = 0;
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
cudaError_t CUDARTAPI cudaGetDeviceCount(int* count) { *count = 1; return cudaSuccess; }
cudaError_t CUDARTAPI cudaGetDeviceProperties(cudaDeviceProp* prop, int)
{
    *prop = {}; std::strcpy(prop->name, "Fake CUDA runtime (no hardware)");
    prop->major = 8; prop->minor = 0; prop->totalGlobalMem = 8ULL << 30; return cudaSuccess;
}
cudaError_t CUDARTAPI cudaMemGetInfo(size_t* available, size_t* total)
{
    *total = 8ULL << 30;
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
    if (++s.mallocs == s.fail_malloc || bytes / (128ULL << 20) > s.max_contexts) {
        s.last_error = s.allocation_last_error == cudaSuccess ? s.allocation_error : s.allocation_last_error;
        return s.allocation_error;
    }
    // Kernels are never executed here: a scratchpad needs only a distinct token.
    // Track its requested VRAM size without reserving gigabytes of host memory.
    *pointer = std::calloc(1, bytes >= (128ULL << 20) ? 1 : bytes);
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
    s.launches.push_back({name, grid.x, block.x, *static_cast<unsigned*>(args[init ? 2 : 4]),
                         init ? 0 : *static_cast<unsigned*>(args[5]), init ? 0 : *static_cast<unsigned*>(args[6])});
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
        state.Reset(); state.free_memory = 8ULL << 30;
        {
            auto automatic{mining::CreateCudaBackend(v2_device, 0)};
            Require(automatic->Contexts() == mining::MAX_GPU_CONTEXTS, "V2 automatic budget did not use the actual scratchpad size");
            Require(automatic->MemoryBytes() == (4ULL << 30), "V2 automatic scratchpad byte count is wrong");
        }
        {
            auto backend{mining::CreateCudaBackend(v2_device, 2)};
            Require(backend->PoWVersion() == mercahash::Version::V2, "CUDA lost V2 selection");
            Require(backend->MemoryBytes() == (32ULL << 20), "V2 per-context scratchpad layout is wrong");
            state.Reset(); unsigned polls{0};
            Require(backend->Hash(full, [&] { ++polls; return false; }).size() == 2, "Incomplete V2 full batch");
            CheckSchedule(2, true);
            const unsigned checkpoints{polls};
            state.Reset();
            Require(backend->Hash(partial, [] { return false; }).size() == 1, "Incomplete V2 partial batch");
            CheckSchedule(1, true);
            for (unsigned stop{1}; stop <= checkpoints; ++stop) {
                state.Reset(); polls = 0;
                Require(backend->Hash(full, [&] { return ++polls >= stop; }).empty(), "Cancelled V2 work escaped");
                Require(state.pending == 0, "V2 cancellation did not drain the stream");
                state.Reset();
                Require(backend->Hash(partial, [] { return false; }).size() == 1, "V2 buffer reuse after cancellation failed");
            }
            for (unsigned fail : {1U, 2U, 17U, 18U, 49U, 81U, 82U, 97U, 98U}) {
                state.Reset(); state.fail_launch = fail;
                ExpectFailure([&] { backend->Hash(full, [] { return false; }); });
            }
            for (unsigned fail{1}; fail <= 6; ++fail) {
                state.Reset(); state.fail_sync = fail;
                ExpectFailure([&] { backend->Hash(full, [] { return false; }); });
            }
            std::cout << checkpoints << " V2 cancellation checkpoints and restarts passed (fake runtime)\n";
        }
        for (unsigned fail{1}; fail <= 4; ++fail) {
            state.Reset(); state.fail_malloc = fail;
            ExpectFailure([&] { mining::CreateCudaBackend(v2_device, 2); });
            Require(state.allocations.empty() && state.streams == 0, "V2 partial allocation leaked resources");
        }
        std::cout << "CUDA host scheduling, full/partial batches, faults and cleanup passed (fake runtime; no GPU execution)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
