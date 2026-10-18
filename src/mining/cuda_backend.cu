// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <cuda_runtime.h>
#include <chrono>
#include <mining/cuda_diagnostics.h>
#include <mining/gpu_backend.h>
#define MH_CUDA
#include <algorithm>
#include <mining/mercahash_gpu.h>
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
    CudaBenchmarkDiagnostics* diagnostics{nullptr};
    cudaEvent_t event_begin{nullptr}, event_end{nullptr}, init_begin{nullptr}, init_end{nullptr};
    unsigned int contexts{0};
    uint64_t memory_bytes{0};
    cudaStream_t stream{nullptr};
    unsigned char *headers{nullptr};
    mh_u64 *states{nullptr};
    mh_u64 *scratch{nullptr};
    unsigned char *outputs{nullptr};

  public:
    CudaBackend(const GpuDevice &selected, unsigned int requested, CudaBenchmarkDiagnostics* measured = nullptr)
        : device{static_cast<int>(selected.ordinal)}, diagnostics{measured}
    {
        try {
            Check(cudaSetDevice(device), "Select NVIDIA device");
            cudaDeviceProp properties{};
            Check(cudaGetDeviceProperties(&properties, device), "Read NVIDIA device properties");
            if (selected.name != properties.name)
                throw std::runtime_error{"NVIDIA enumeration changed; refresh device selection"};
            size_t available{0}, total{0};
            Check(cudaMemGetInfo(&available, &total), "Read NVIDIA free memory");
            if (diagnostics) {
                diagnostics->available_memory_before_allocation = available;
                diagnostics->memory_context_limit = 0;
                // Query the existing policy; never duplicate or relax its reserve.
                for (unsigned int candidate{1}; candidate <= 256; ++candidate) {
                    try { CalculateGpuBudget(available, available, candidate); }
                    catch (const std::runtime_error&) { break; }
                    diagnostics->memory_context_limit = candidate;
                }
                Check(cudaDriverGetVersion(&diagnostics->driver_version), "Read CUDA driver version");
                Check(cudaRuntimeGetVersion(&diagnostics->runtime_version), "Read CUDA runtime version");
                diagnostics->multiprocessors = properties.multiProcessorCount;
                auto resources = [&](auto kernel, CudaKernelResources& result) {
                    cudaFuncAttributes attributes{};
                    Check(cudaFuncGetAttributes(&attributes, kernel), "Read CUDA kernel resources");
                    result.registers_per_thread = attributes.numRegs;
                    result.local_bytes_per_thread = attributes.localSizeBytes;
                    result.static_shared_bytes = attributes.sharedSizeBytes;
                    Check(cudaOccupancyMaxActiveBlocksPerMultiprocessor(&result.maximum_active_blocks_per_sm,
                                                                       kernel, 32, 0), "Read theoretical CUDA occupancy");
                };
                resources(mh_init, diagnostics->init_kernel);
                resources(mh_stage, diagnostics->stage_kernel);
            }
            const auto budget{CalculateGpuBudget(available, available, requested)};
            contexts = budget.contexts;
            memory_bytes = budget.scratchpad_bytes;
            if (!contexts)
                throw std::runtime_error{
                    "NVIDIA GPU has insufficient available memory for a safe MercaHash scratchpad"};
            Check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "Create NVIDIA mining stream");
            if (Timed()) {
                Check(cudaEventCreate(&event_begin), "Create CUDA timing start event");
                Check(cudaEventCreate(&event_end), "Create CUDA timing end event");
                Check(cudaEventCreate(&init_begin), "Create CUDA init start event");
                Check(cudaEventCreate(&init_end), "Create CUDA init end event");
            }
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
        BeginTiming();
        Check(cudaMemcpyAsync(headers, input.data(), input.size_bytes(), cudaMemcpyHostToDevice, stream),
              "Upload NVIDIA headers");
        // Upload remains ordered before init in the same stream, without an extra
        // host synchronization. Its event pair is consumed after the existing wait.
        if (Timed()) Check(cudaEventRecord(event_end, stream), "Record CUDA upload end");
        auto synchronized = [&](CudaStage stage) {
            Check(cudaGetLastError(), "Launch NVIDIA MercaHash kernel");
            EndTiming();
            Synchronize("Synchronize NVIDIA MercaHash kernel");
            CollectTiming(stage);
            return !cancelled();
        };
        // A second event pair avoids changing the existing launch/wait
        // sequence. Other stages reuse one pair after each original stream wait.
        // Collect upload on the first wait; init needs its own pair only here.
        if (Timed()) {
            Check(cudaEventRecord(init_begin, stream), "Record CUDA init start");
        }
        mh_init<<<count, 32, 0, stream>>>(headers, states, count);
        Check(cudaGetLastError(), "Launch NVIDIA MercaHash init kernel");
        if (Timed()) Check(cudaEventRecord(init_end, stream), "Record CUDA init end");
        Synchronize("Synchronize NVIDIA MercaHash init kernel");
        CollectTiming(CudaStage::UPLOAD);
        if (Timed()) {
            float elapsed{0};
            Check(cudaEventElapsedTime(&elapsed, init_begin, init_end), "Read CUDA init elapsed time");
            diagnostics->device_ms[static_cast<size_t>(CudaStage::INITIALIZATION)] += elapsed;
        }
        if (cancelled())
            return {};
        auto advance = [&](unsigned int phase, unsigned int begin, unsigned int end) {
            if (cancelled())
                return false;
            BeginTiming();
            mh_stage<<<count, 32, 0, stream>>>(headers, states, scratch, outputs, count, phase, begin, end);
            return synchronized(static_cast<CudaStage>(static_cast<size_t>(CudaStage::FILL) + phase));
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
        BeginTiming();
        Check(cudaMemcpyAsync(hashes.data(), outputs, hashes.size() * sizeof(GpuHash), cudaMemcpyDeviceToHost, stream),
              "Read NVIDIA MercaHash outputs");
        EndTiming();
        Synchronize("Synchronize NVIDIA output transfer");
        CollectTiming(CudaStage::DOWNLOAD);
        if (cancelled())
            return {};
        return hashes;
    }

  private:
    bool Timed() const { return diagnostics && diagnostics->timing_enabled; }
    void BeginTiming() { if (Timed()) Check(cudaEventRecord(event_begin, stream), "Record CUDA timing start"); }
    void EndTiming() { if (Timed()) Check(cudaEventRecord(event_end, stream), "Record CUDA timing end"); }
    void CollectTiming(CudaStage stage)
    {
        if (!Timed()) return;
        float elapsed{0};
        Check(cudaEventElapsedTime(&elapsed, event_begin, event_end), "Read CUDA elapsed time");
        diagnostics->device_ms[static_cast<size_t>(stage)] += elapsed;
    }
    void Synchronize(const char* operation)
    {
        if (!Timed()) { Check(cudaStreamSynchronize(stream), operation); return; }
        const auto before{std::chrono::steady_clock::now()};
        Check(cudaStreamSynchronize(stream), operation);
        diagnostics->host_synchronize_wait_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
    }
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
        if (event_begin) cudaEventDestroy(event_begin);
        if (event_end) cudaEventDestroy(event_end);
        if (init_begin) cudaEventDestroy(init_begin);
        if (init_end) cudaEventDestroy(init_end);
        event_begin = event_end = init_begin = init_end = nullptr;
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
std::unique_ptr<GpuBackend> CreateCudaBackend(const GpuDevice &device, unsigned int contexts)
{
    return std::make_unique<CudaBackend>(device, contexts);
}
std::unique_ptr<GpuBackend> CreateCudaBenchmarkBackend(const GpuDevice& device, unsigned int contexts,
                                                       CudaBenchmarkDiagnostics& diagnostics)
{
    if (!device.supported || device.vendor != GpuVendor::NVIDIA_CUDA)
        throw std::runtime_error{"Unsupported NVIDIA benchmark device: " + device.error};
    return std::make_unique<CudaBackend>(device, contexts, &diagnostics);
}
} // namespace mining
