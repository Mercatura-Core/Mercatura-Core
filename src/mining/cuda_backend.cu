// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <cuda_runtime.h>
#include <crypto/mercahash.h>
#include <mining/gpu_backend.h>
#define MH_CUDA
#include <algorithm>
#include <mining/mercahash_gpu_specialized.h>
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
    unsigned int contexts{0}, estimated{0};
    uint64_t memory_bytes{0};
    cudaStream_t stream{nullptr};
    unsigned char *headers{nullptr};
    mh_u64 *states{nullptr};
    mh_u64 *scratch{nullptr};
    unsigned char *outputs{nullptr};

  public:
    CudaBackend(const GpuDevice &selected, unsigned int requested, const std::function<bool()>& cancelled) : device{static_cast<int>(selected.ordinal)}
    {
        try {
            Check(cudaSetDevice(device), "Select NVIDIA device");
            cudaDeviceProp properties{};
            Check(cudaGetDeviceProperties(&properties, device), "Read NVIDIA device properties");
            if (selected.name != properties.name)
                throw std::runtime_error{"NVIDIA enumeration changed; refresh device selection"};
            if (properties.major * 10 + properties.minor < MERCATURA_CUDA_MIN_ARCH)
                throw std::runtime_error{"NVIDIA device no longer meets the required compute capability"};
            // Initialize the stream first, then budget from a fresh query
            // immediately before allocating the mining buffers.
            Check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "Create NVIDIA mining stream");
            const auto allocation{AllocateGpuContexts(requested, [&] {
                size_t available{0}, total{0};
                Check(cudaMemGetInfo(&available, &total), "Read NVIDIA free memory");
                return CalculateGpuBudget(available, available);
            }, [&](unsigned int count, const GpuMemoryBudget& budget) {
                contexts = count;
                memory_bytes = count * uint64_t{mercahash::SCRATCHPAD_BYTES};
                auto allocate = [&](void** buffer, size_t bytes, const char* operation) {
                    const auto error{cudaMalloc(buffer, bytes)};
                    if (error == cudaErrorMemoryAllocation)
                        throw GpuMemoryAllocationError{std::string{operation} + ": " + cudaGetErrorString(error)};
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
            }, [&] { CleanupBuffers(); }, cancelled)};
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
    unsigned int EstimatedContexts() const override { return estimated; }
    uint64_t MemoryBytes() const override
    {
        return memory_bytes;
    }
    std::vector<GpuHash> Hash(std::span<const GpuHeader> input, const std::function<bool()> &cancelled) override
    {
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
        mh_init<<<count, 32, 0, stream>>>(headers, states, count);
        if (!synchronized())
            return {};
        // Validated single-lane fill: queue 32 kernels on the same stream.
        for (unsigned int begin{0}; begin < 2097152; begin += 16384) {
            if (cancelled()) {
                Check(cudaStreamSynchronize(stream),
                      "Drain cancelled fill work");
                return {};
            }

            mh_fill<<<count, 1, 0, stream>>>(headers, states, scratch, outputs, count, begin, begin + 16384);

            Check(cudaGetLastError(),
                  "Launch NVIDIA fill kernel");

            if (((begin / 16384 + 1) % 32) == 0) {
                if (!synchronized())
                    return {};
            }
        }
        // Validated warp-parallel binding: synchronize every 32 chunks.
        // Same-stream ordering preserves kernel dependencies.
        for (unsigned int begin{0}; begin < 32768; begin += 64) {
            if (cancelled()) {
                Check(cudaStreamSynchronize(stream),
                      "Drain cancelled binding work");
                return {};
            }

            mh_bind<<<count, 32, 0, stream>>>(headers, states, scratch, outputs, count, begin, begin + 64);

            Check(cudaGetLastError(),
                  "Launch NVIDIA binding kernel");

            if (((begin / 64 + 1) % 32) == 0) {
                if (!synchronized())
                    return {};
            }
        }
        // Validated mixing cadence: synchronize every 16 kernels.
        for (unsigned int begin{1}; begin <= 131072; begin += 1024) {
            if (cancelled()) {
                Check(cudaStreamSynchronize(stream),
                      "Drain cancelled mixing work");
                return {};
            }

            mh_mix<<<count, 32, 0, stream>>>(
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
        mh_final<<<count, 32, 0, stream>>>(headers, states, scratch, outputs, count, 0, 0);
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
    void CleanupBuffers()
    {
        // Never reset a device: other mining workers or desktop applications may
        // own independent contexts. Ignore teardown errors after device loss.
        cudaSetDevice(device);
        if (stream)
            cudaStreamSynchronize(stream);
        if (headers)
            cudaFree(headers);
        if (states)
            cudaFree(states);
        if (scratch)
            cudaFree(scratch);
        if (outputs)
            cudaFree(outputs);
        headers = outputs = nullptr;
        states = scratch = nullptr;
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
                              CalculateGpuBudget(available, available).contexts > 0;
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
