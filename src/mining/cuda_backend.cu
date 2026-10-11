// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <cuda_runtime.h>
#include <mining/gpu_backend.h>
#define MH_CUDA
#include <algorithm>
#include <mining/mercahash_gpu.h>
#include <stdexcept>
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
    unsigned int contexts{0};
    uint64_t memory_bytes{0};
    cudaStream_t stream{nullptr};
    unsigned char *headers{nullptr};
    mh_u64 *states{nullptr};
    mh_u64 *scratch{nullptr};
    unsigned char *outputs{nullptr};

  public:
    CudaBackend(const GpuDevice &selected, unsigned int requested) : device{static_cast<int>(selected.ordinal)}
    {
        try {
            Check(cudaSetDevice(device), "Select NVIDIA device");
            cudaDeviceProp properties{};
            Check(cudaGetDeviceProperties(&properties, device), "Read NVIDIA device properties");
            if (selected.name != properties.name)
                throw std::runtime_error{"NVIDIA enumeration changed; refresh device selection"};
            size_t available{0}, total{0};
            Check(cudaMemGetInfo(&available, &total), "Read NVIDIA free memory");
            const auto budget{CalculateGpuBudget(available, available, requested)};
            contexts = budget.contexts;
            memory_bytes = budget.scratchpad_bytes;
            if (!contexts)
                throw std::runtime_error{
                    "NVIDIA GPU has insufficient available memory for a safe MercaHash scratchpad"};
            Check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "Create NVIDIA mining stream");
            Check(cudaMalloc(reinterpret_cast<void **>(&headers), contexts * size_t{80}), "Allocate NVIDIA headers");
            Check(cudaMalloc(reinterpret_cast<void **>(&states), contexts * size_t{128}), "Allocate NVIDIA states");
            Check(cudaMalloc(reinterpret_cast<void **>(&scratch), static_cast<size_t>(memory_bytes)),
                  "Allocate NVIDIA scratchpads");
            Check(cudaMalloc(reinterpret_cast<void **>(&outputs), contexts * size_t{32}), "Allocate NVIDIA outputs");
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
        auto advance = [&](unsigned int phase, unsigned int begin, unsigned int end) {
            if (cancelled())
                return false;
            mh_stage<<<count, 32, 0, stream>>>(headers, states, scratch, outputs, count, phase, begin, end);
            return synchronized();
        };
        for (unsigned int begin{0}; begin < 2097152; begin += 4096)
            if (!advance(0, begin, begin + 4096))
                return {};
        for (unsigned int begin{0}; begin < 32768; begin += 64)
            if (!advance(1, begin, begin + 64))
                return {};
        for (unsigned int begin{1}; begin <= 131072; begin += 1024)
            if (!advance(2, begin, begin + 1023))
                return {};
        if (!advance(3, 0, 0))
            return {};
        std::vector<GpuHash> hashes(count);
        Check(cudaMemcpyAsync(hashes.data(), outputs, hashes.size() * sizeof(GpuHash), cudaMemcpyDeviceToHost, stream),
              "Read NVIDIA MercaHash outputs");
        Check(cudaStreamSynchronize(stream), "Synchronize NVIDIA output transfer");
        if (cancelled())
            return {};
        return hashes;
    }

  private:
    void Cleanup()
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
        entry.id = "cuda:" + std::to_string(index);
        try {
            cudaDeviceProp properties{};
            Check(cudaGetDeviceProperties(&properties, index), "Read NVIDIA capability");
            entry.name = properties.name;
            entry.capability = "CUDA " + std::to_string(properties.major) + "." + std::to_string(properties.minor);
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
                              std::to_string(MERCATURA_CUDA_MIN_ARCH / 10) + "." +
                              std::to_string(MERCATURA_CUDA_MIN_ARCH % 10) + " and sufficient free VRAM";
        } catch (const std::exception &error) {
            entry.error = error.what();
        }
        result.devices.push_back(std::move(entry));
    }
    if (!count)
        result.errors.emplace_back("No NVIDIA CUDA GPU detected");
    return result;
}
std::unique_ptr<GpuBackend> CreateCudaBackend(const GpuDevice &device, unsigned int contexts)
{
    return std::make_unique<CudaBackend>(device, contexts);
}
} // namespace mining
