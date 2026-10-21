// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <cuda_runtime.h>
#include <crypto/mercahash.h>
#include <mining/gpu_backend.h>
#define MH_CUDA
#include <algorithm>
#include <limits>
namespace mh_v1 {
#include <mining/mercahash_gpu_specialized.h>
static_assert(MH_LINE_COUNT == mercahash::LINE_COUNT && MH_LINE_MASK == mercahash::LINE_MASK);
static_assert(MH_BIND_BLOCK_COUNT == mercahash::BIND_BLOCK_COUNT);
}
#undef BITCOIN_MINING_MERCAHASH_GPU_SPECIALIZED_H
#undef MH_VERSION
#define MH_VERSION 2
#define mh_init mh_init_v2
#define mh_fill mh_fill_v2
#define mh_bind mh_bind_v2
#define mh_mix mh_mix_v2
#define mh_final mh_final_v2
namespace mh_v2 {
#include <mining/mercahash_gpu_specialized.h>
static_assert(MH_LINE_COUNT == mercahash::V2_LINE_COUNT && MH_LINE_MASK == mercahash::V2_LINE_MASK);
static_assert(MH_BIND_BLOCK_COUNT == mercahash::V2_BIND_BLOCK_COUNT);
}
#undef mh_init
#undef mh_fill
#undef mh_bind
#undef mh_mix
#undef mh_final
#undef MH_VERSION
using mh_u64 = unsigned long long;
#include <stdexcept>
#include <util/string.h>
#ifndef MERCATURA_CUDA_MIN_ARCH
#define MERCATURA_CUDA_MIN_ARCH 75
#endif
namespace mining
{
namespace
{
void Check(cudaError_t error, const char *operation)
{
    if (error != cudaSuccess)
        throw std::runtime_error{std::string{operation} + ": " + cudaGetErrorString(error)};
}
class CudaBackend final : public GpuBackend
{
    int device;
    const mercahash::Version version;
    unsigned int contexts{0}, estimated{0};
    unsigned int execution_limit{0}, next_context{0};
    GpuMemoryBudget allocation_budget;
    uint64_t memory_bytes{0};
    cudaStream_t stream{nullptr};
    unsigned char *headers{nullptr};
    mh_u64 *states{nullptr};
    mh_u64 *scratch{nullptr};
    unsigned char *outputs{nullptr};

  public:
    CudaBackend(const GpuDevice &selected, unsigned int requested, const std::function<bool()>& cancelled) : device{static_cast<int>(selected.ordinal)}, version{selected.pow_version}
    {
        try {
            Check(cudaSetDevice(device), "Select NVIDIA device");
            cudaDeviceProp properties{};
            Check(cudaGetDeviceProperties(&properties, device), "Read NVIDIA device properties");
            if (selected.name != properties.name)
                throw std::runtime_error{"NVIDIA enumeration changed; refresh device selection"};
            if (properties.major * 10 + properties.minor < MERCATURA_CUDA_MIN_ARCH)
                throw std::runtime_error{"NVIDIA device no longer meets the required compute capability"};
            if (version == mercahash::Version::V2) {
                if (properties.multiProcessorCount <= 0 || properties.maxGridSize[0] <= 0)
                    throw std::runtime_error{"Invalid NVIDIA execution geometry"};
                int active_blocks{std::numeric_limits<int>::max()};
                const auto occupancy = [&](auto kernel, int threads) {
                    int blocks{0};
                    Check(cudaOccupancyMaxActiveBlocksPerMultiprocessor(&blocks, kernel, threads, 0), "Read NVIDIA V2 kernel occupancy");
                    if (blocks <= 0) throw std::runtime_error{"NVIDIA V2 kernel has no executable occupancy"};
                    active_blocks = std::min(active_blocks, blocks);
                };
                occupancy(mh_v2::mh_init_v2, 32); occupancy(mh_v2::mh_fill_v2, 1);
                occupancy(mh_v2::mh_bind_v2, 32); occupancy(mh_v2::mh_mix_v2, 32); occupancy(mh_v2::mh_final_v2, 32);
                execution_limit = GpuExecutionContexts(std::numeric_limits<unsigned int>::max(),
                    uint64_t{static_cast<unsigned>(properties.multiProcessorCount)} * active_blocks, properties.maxGridSize[0]);
            }
            // Initialize the stream first, then budget from a fresh query
            // immediately before allocating the mining buffers.
            Check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "Create NVIDIA mining stream");
            const auto allocation{AllocateGpuContexts(requested, [&] {
                size_t available{0}, total{0};
                Check(cudaMemGetInfo(&available, &total), "Read NVIDIA free memory");
                return CalculateGpuBudget(available, available, 0, GpuMemorySource::CUDA_FREE, version);
            }, [&](unsigned int count, const GpuMemoryBudget& budget) {
                contexts = count;
                memory_bytes = count * uint64_t{mercahash::ScratchpadBytes(version)};
                auto allocate = [&](void** buffer, size_t bytes, const char* operation) {
                    const auto error{cudaMalloc(buffer, bytes)};
                    if (error == cudaErrorMemoryAllocation) {
                        // Consume the handled OOM so it cannot poison the next
                        // kernel launch check. Never mask a different error.
                        const auto pending{cudaGetLastError()};
                        if (pending != cudaErrorMemoryAllocation) Check(pending, "Read NVIDIA allocation error state");
                        throw GpuMemoryAllocationError{std::string{operation} + ": " + cudaGetErrorString(error)};
                    }
                    Check(error, operation);
                };
                allocate(reinterpret_cast<void **>(&headers), contexts * size_t{80}, "Allocate NVIDIA headers");
                allocate(reinterpret_cast<void **>(&states), contexts * size_t{128}, "Allocate NVIDIA states");
                allocate(reinterpret_cast<void **>(&scratch), static_cast<size_t>(memory_bytes), "Allocate NVIDIA scratchpads");
                allocate(reinterpret_cast<void **>(&outputs), contexts * size_t{32}, "Allocate NVIDIA outputs");
                size_t available{0}, total{0};
                Check(cudaMemGetInfo(&available, &total), "Validate NVIDIA memory headroom");
                if (available < budget.reserved_bytes)
                    throw GpuMemoryAllocationError{"NVIDIA free VRAM fell below the required reserve"};
                allocation_budget = budget;
                allocation_budget.contexts = count;
                allocation_budget.scratchpad_bytes = memory_bytes;
                allocation_budget.overhead_bytes = (64ULL << 20) + uint64_t{count} * 4096;
                allocation_budget.headroom_bytes = available;
                allocation_budget.headroom_measured = true;
            }, [&] { CleanupBuffers(true); }, cancelled, version)};
            estimated = allocation.estimated;
        } catch (...) {
            Cleanup();
            throw;
        }
    }
    ~CudaBackend() override
    {
        Cleanup();
    }
    unsigned int Contexts() const override
    {
        return contexts;
    }
    mercahash::Version PoWVersion() const override { return version; }
    unsigned int EstimatedContexts() const override { return estimated; }
    unsigned int BatchContexts() const override { return version == mercahash::Version::V2 ? std::min({contexts, execution_limit, contexts - next_context}) : contexts; }
    GpuMemoryBudget Budget() const override { return allocation_budget; }
    uint64_t MemoryBytes() const override
    {
        return memory_bytes + (version == mercahash::Version::V2 ? uint64_t{contexts} * 240 : 0);
    }
    std::vector<GpuHash> Hash(std::span<const GpuHeader> input, const std::function<bool()> &cancelled) override
    {
        if (version == mercahash::Version::V2) return HashV2(input, cancelled);
        if (input.empty() || input.size() > contexts)
            throw std::runtime_error{"Invalid NVIDIA hashing batch size"};
        if (cancelled())
            return {};
        Check(cudaSetDevice(device), "Select NVIDIA mining device");
        const unsigned int count{static_cast<unsigned int>(input.size())};
        std::vector<GpuHash> hashes(count);
        struct DrainOnExit {
            cudaStream_t stream;
            bool complete{false};
            ~DrainOnExit()
            {
                // Do not throw during unwinding. Cleanup also handles device loss.
                if (!complete) cudaStreamSynchronize(stream);
            }
        } drain{stream};
        Check(cudaMemcpyAsync(headers, input.data(), input.size_bytes(), cudaMemcpyHostToDevice, stream),
              "Upload NVIDIA headers");
        auto synchronized = [&] {
            Check(cudaGetLastError(), "Launch NVIDIA MercaHash kernel");
            Check(cudaStreamSynchronize(stream), "Synchronize NVIDIA MercaHash kernel");
            return !cancelled();
        };
        if (version == mercahash::Version::V2)
            mh_v2::mh_init_v2<<<count, 32, 0, stream>>>(headers, states, count);
        else
            mh_v1::mh_init<<<count, 32, 0, stream>>>(headers, states, count);
        if (!synchronized())
            return {};
        // Validated single-lane fill: queue 32 kernels on the same stream.
        for (unsigned int begin{0}; begin < mercahash::LineCount(version); begin += 16384) {
            if (cancelled()) {
                Check(cudaStreamSynchronize(stream),
                      "Drain cancelled fill work");
                return {};
            }

            if (version == mercahash::Version::V2)
                mh_v2::mh_fill_v2<<<count, 1, 0, stream>>>(headers, states, scratch, outputs, count, begin, begin + 16384);
            else
                mh_v1::mh_fill<<<count, 1, 0, stream>>>(headers, states, scratch, outputs, count, begin, begin + 16384);

            Check(cudaGetLastError(),
                  "Launch NVIDIA fill kernel");

            if (((begin / 16384 + 1) % 32) == 0) {
                if (!synchronized())
                    return {};
            }
        }
        // Validated warp-parallel binding: synchronize every 32 chunks.
        // Same-stream ordering preserves kernel dependencies.
        for (unsigned int begin{0}; begin < mercahash::BindBlockCount(version); begin += 64) {
            if (cancelled()) {
                Check(cudaStreamSynchronize(stream),
                      "Drain cancelled binding work");
                return {};
            }

            if (version == mercahash::Version::V2)
                mh_v2::mh_bind_v2<<<count, 32, 0, stream>>>(headers, states, scratch, outputs, count, begin, begin + 64);
            else
                mh_v1::mh_bind<<<count, 32, 0, stream>>>(headers, states, scratch, outputs, count, begin, begin + 64);

            Check(cudaGetLastError(),
                  "Launch NVIDIA binding kernel");

            if (((begin / 64 + 1) % 32) == 0) {
                if (!synchronized())
                    return {};
            }
        }
        // Validated mixing cadence: synchronize every 16 kernels.
        for (unsigned int begin{1}; begin <= mercahash::MixSteps(version); begin += 1024) {
            if (cancelled()) {
                Check(cudaStreamSynchronize(stream),
                      "Drain cancelled mixing work");
                return {};
            }

            if (version == mercahash::Version::V2)
                mh_v2::mh_mix_v2<<<count, 32, 0, stream>>>(
                headers, states, scratch, outputs,
                count, begin, begin + 1023);
            else
                mh_v1::mh_mix<<<count, 32, 0, stream>>>(
                headers, states, scratch, outputs,
                count, begin, begin + 1023);

            Check(cudaGetLastError(), "Launch NVIDIA mixing kernel");

            if ((((begin - 1) / 1024 + 1) % 16) == 0) {
                if (!synchronized())
                    return {};
            }
        }
        if (cancelled())
            return {};
        if (version == mercahash::Version::V2)
            mh_v2::mh_final_v2<<<count, 32, 0, stream>>>(headers, states, scratch, outputs, count, 0, 0);
        else
            mh_v1::mh_final<<<count, 32, 0, stream>>>(headers, states, scratch, outputs, count, 0, 0);
        if (!synchronized())
            return {};
        Check(cudaMemcpyAsync(hashes.data(), outputs, hashes.size() * sizeof(GpuHash), cudaMemcpyDeviceToHost, stream),
              "Read NVIDIA MercaHash outputs");
        Check(cudaStreamSynchronize(stream), "Synchronize NVIDIA output transfer");
        drain.complete = true;
        if (cancelled())
            return {};
        return hashes;
    }

  private:
    std::vector<GpuHash> HashV2(std::span<const GpuHeader> input, const std::function<bool()>& cancelled)
    {
        if (input.empty() || input.size() > contexts) throw std::runtime_error{"Invalid NVIDIA V2 hashing batch size"};
        if (cancelled()) return {};
        Check(cudaSetDevice(device), "Select NVIDIA V2 mining device");
        std::vector<GpuHash> hashes(input.size());
        struct Drain {
            cudaStream_t stream;
            bool complete{false};
            ~Drain() { if (!complete) cudaStreamSynchronize(stream); }
        } drain{stream};
        // Rotate across the entire allocated pool. Every launch has at most
        // one occupancy-derived hardware wave and is drained separately.
        // Geometry and latency bounds never cap the total resident capacity.
        for (size_t completed{0}; completed < input.size();) {
            if (cancelled()) return {};
            const auto count{static_cast<unsigned int>(std::min({size_t{BatchContexts()}, input.size() - completed, size_t{contexts - next_context}}))};
            auto* h{headers + size_t{next_context} * 80};
            auto* s{states + size_t{next_context} * 16};
            auto* pad{scratch + size_t{next_context} * (mercahash::V2_SCRATCHPAD_BYTES / sizeof(mh_u64))};
            auto* out{outputs + size_t{next_context} * 32};
            next_context = static_cast<unsigned int>((uint64_t{next_context} + count) % contexts);
            Check(cudaMemcpyAsync(h, input.data() + completed, size_t{count} * 80, cudaMemcpyHostToDevice, stream), "Upload NVIDIA V2 headers");
            const auto synchronized = [&] {
                Check(cudaGetLastError(), "Launch NVIDIA V2 kernel");
                Check(cudaStreamSynchronize(stream), "Synchronize NVIDIA V2 kernel");
                return !cancelled();
            };
            mh_v2::mh_init_v2<<<count, 32, 0, stream>>>(h, s, count);
            if (!synchronized()) return {};
            for (unsigned begin{0}; begin < mercahash::V2_LINE_COUNT; begin += 4096) {
                if (cancelled()) return {};
                mh_v2::mh_fill_v2<<<count, 1, 0, stream>>>(h, s, pad, out, count, begin, begin + 4096);
                if (!synchronized()) return {};
            }
            for (unsigned begin{0}; begin < mercahash::V2_BIND_BLOCK_COUNT; begin += 64) {
                if (cancelled()) return {};
                mh_v2::mh_bind_v2<<<count, 32, 0, stream>>>(h, s, pad, out, count, begin, begin + 64);
                if (!synchronized()) return {};
            }
            for (unsigned begin{1}; begin <= mercahash::V2_MIX_STEPS; begin += 256) {
                if (cancelled()) return {};
                mh_v2::mh_mix_v2<<<count, 32, 0, stream>>>(h, s, pad, out, count, begin, begin + 255);
                if (!synchronized()) return {};
            }
            mh_v2::mh_final_v2<<<count, 32, 0, stream>>>(h, s, pad, out, count, 0, 0);
            if (!synchronized()) return {};
            Check(cudaMemcpyAsync(hashes.data() + completed, out, size_t{count} * 32, cudaMemcpyDeviceToHost, stream), "Read NVIDIA V2 output");
            Check(cudaStreamSynchronize(stream), "Synchronize NVIDIA V2 output");
            if (cancelled()) return {};
            completed += count;
        }
        drain.complete = true;
        return hashes;
    }
    void CleanupBuffers(bool checked = false)
    {
        // Never reset a device: other workers/applications may own contexts.
        // Retry cleanup checks errors; final teardown remains non-throwing.
        auto failure{cudaSetDevice(device)};
        auto record = [&](cudaError_t error) { if (failure == cudaSuccess) failure = error; };
        if (stream) record(cudaStreamSynchronize(stream));
        auto release = [&](auto& buffer) {
            if (!buffer) return;
            const auto error{cudaFree(buffer)};
            record(error);
            // Retain a failed release for the constructor's final cleanup.
            if (error == cudaSuccess || !checked) buffer = nullptr;
        };
        release(headers); release(states); release(scratch); release(outputs);
        if (checked) Check(failure, "Clean up NVIDIA allocation attempt");
    }
    void Cleanup()
    {
        CleanupBuffers();
        if (stream)
            cudaStreamDestroy(stream);
        stream = nullptr;
    }
};
} // namespace
GpuDiscovery DiscoverCudaDevices()
{
    GpuDiscovery result;
    int count{0};
    auto status{cudaGetDeviceCount(&count)};
    if (status != cudaSuccess) {
        result.errors.emplace_back(std::string{"NVIDIA CUDA unavailable: "} + cudaGetErrorString(status));
        return result;
    }
    for (int index{0}; index < count; ++index) {
        GpuDevice entry{GpuVendor::NVIDIA_CUDA};
        entry.ordinal = index;
        entry.id = "cuda:" + util::ToString(index);
        try {
            cudaDeviceProp properties{};
            Check(cudaGetDeviceProperties(&properties, index), "Read NVIDIA capability");
            entry.name = properties.name;
            entry.capability = "CUDA " + util::ToString(properties.major) + "." + util::ToString(properties.minor);
            entry.total_memory = properties.totalGlobalMem;
            Check(cudaSetDevice(index), "Select NVIDIA discovery device");
            size_t available{0}, total{0};
            Check(cudaMemGetInfo(&available, &total), "Read NVIDIA memory");
            entry.available_memory = available;
            entry.max_allocation = available;
            entry.supported = properties.major * 10 + properties.minor >= MERCATURA_CUDA_MIN_ARCH &&
                              CalculateGpuBudget(available, available, 0, GpuMemorySource::CUDA_FREE, mercahash::Version::V2).contexts > 0;
            if (!entry.supported)
                entry.error = "NVIDIA device needs compute capability at least " +
                              util::ToString(MERCATURA_CUDA_MIN_ARCH / 10) + "." +
                              util::ToString(MERCATURA_CUDA_MIN_ARCH % 10) + " and sufficient free VRAM";
        } catch (const std::exception &error) {
            entry.error = error.what();
        }
        result.devices.push_back(std::move(entry));
    }
    if (!count)
        result.errors.emplace_back("No NVIDIA CUDA GPU detected");
    return result;
}
std::unique_ptr<GpuBackend> CreateCudaBackend(const GpuDevice &device, unsigned int contexts, const std::function<bool()>& cancelled)
{
    return std::make_unique<CudaBackend>(device, contexts, cancelled);
}
} // namespace mining
