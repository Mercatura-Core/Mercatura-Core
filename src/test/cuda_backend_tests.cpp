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
    size_t free_memory{8ULL << 30}, extra_usage{0};
    std::vector<Launch> launches;
    std::vector<std::function<void()>> copies;
    std::vector<size_t> synchronized_at;
    dim3 grid, block;
    size_t shared{0};
    void* stream{nullptr};
    unsigned pending{0}, streams{0}, mallocs{0}, transfers{0}, syncs{0};
    unsigned fail_malloc{0}, fail_launch{0}, fail_transfer{0}, fail_sync{0};
    bool freed_pending{false};
    void Reset()
    {
        if (pending) throw std::runtime_error{"Reset with pending CUDA work"};
        launches.clear(); copies.clear(); synchronized_at.clear();
        mallocs = transfers = syncs = 0;
        fail_malloc = fail_launch = fail_transfer = fail_sync = 0;
        freed_pending = false;
        extra_usage = 0;
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
void CheckSchedule(unsigned count)
{
    const auto& s{State()};
    Require(s.launches.size() == 770, "Wrong total kernel count");
    Require(s.synchronized_at.size() == 31, "Wrong synchronization cadence");
    size_t index{0};
    auto kernel = [&](const char* name, unsigned block, unsigned begin, unsigned end) {
        const auto& launch{s.launches.at(index++)};
        Require(launch.name == name && launch.grid == count && launch.block == block && launch.count == count &&
                launch.begin == begin && launch.end == end, "Wrong kernel, batch bounds or launch geometry");
    };
    kernel("mh_init", 32, 0, 0);
    for (unsigned begin{0}; begin < 2097152; begin += 16384) kernel("mh_fill", 1, begin, begin + 16384);
    for (unsigned begin{0}; begin < 32768; begin += 64) kernel("mh_bind", 32, begin, begin + 64);
    for (unsigned begin{1}; begin <= 131072; begin += 1024) kernel("mh_mix", 32, begin, begin + 1023);
    kernel("mh_final", 32, 0, 0);
    std::vector<size_t> expected{1};
    for (size_t i{33}; i <= 129; i += 32) expected.push_back(i);
    for (size_t i{161}; i <= 641; i += 32) expected.push_back(i);
    for (size_t i{657}; i <= 769; i += 16) expected.push_back(i);
    expected.push_back(770); expected.push_back(770); // Final kernel, output copy.
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
    if (++s.mallocs == s.fail_malloc) return cudaErrorMemoryAllocation;
    // Kernels are never executed here: a scratchpad needs only a distinct token.
    // Track its requested VRAM size without reserving gigabytes of host memory.
    *pointer = std::calloc(1, bytes >= (128ULL << 20) ? 1 : bytes);
    if (!*pointer) return cudaErrorMemoryAllocation;
    s.allocations.insert(*pointer); s.allocation_bytes[*pointer] = bytes; return cudaSuccess;
}
cudaError_t CUDARTAPI cudaFree(void* pointer)
{
    auto& s{State()}; s.freed_pending |= s.pending != 0;
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
    const bool init{name == "mh_init"};
    s.launches.push_back({name, grid.x, block.x, *static_cast<unsigned*>(args[init ? 2 : 4]),
                         init ? 0 : *static_cast<unsigned*>(args[5]), init ? 0 : *static_cast<unsigned*>(args[6])});
    ++s.pending; return cudaSuccess;
}
cudaError_t CUDARTAPI cudaGetLastError()
{
    auto& s{State()}; return s.launches.size() == s.fail_launch ? cudaErrorLaunchFailure : cudaSuccess;
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
            Require(automatic->Contexts() == 16, "Automatic allocation lost its conservative cap");
        }
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
        std::cout << "CUDA host scheduling, full/partial batches, faults and cleanup passed (fake runtime; no GPU execution)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
