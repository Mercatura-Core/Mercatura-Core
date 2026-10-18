// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_MINING_CUDA_DIAGNOSTICS_H
#define BITCOIN_MINING_CUDA_DIAGNOSTICS_H

#include <mining/gpu_backend.h>
#include <array>
#include <cstddef>

namespace mining {
// Benchmark-only opt-in. Production construction supplies no diagnostics object.
enum class CudaStage : size_t { UPLOAD, INITIALIZATION, FILL, BIND, MIX, FINALIZATION, DOWNLOAD, COUNT };
struct CudaKernelResources {
    int registers_per_thread{0};
    size_t local_bytes_per_thread{0};
    size_t static_shared_bytes{0};
    int maximum_active_blocks_per_sm{0};
};
struct CudaBenchmarkDiagnostics {
    bool timing_enabled{false};
    uint64_t available_memory_before_allocation{0};
    unsigned int memory_context_limit{0};
    int driver_version{0}, runtime_version{0}, multiprocessors{0};
    CudaKernelResources init_kernel, stage_kernel;
    std::array<double, static_cast<size_t>(CudaStage::COUNT)> device_ms{};
    double host_synchronize_wait_ms{0};
    void ResetTimings() { device_ms.fill(0); host_synchronize_wait_ms = 0; }
};
// Keep diagnostics alive until backend destruction; set timing_enabled before
// construction and do not change it while the backend exists. Reset only between calls.
std::unique_ptr<GpuBackend> CreateCudaBenchmarkBackend(const GpuDevice& device, unsigned int contexts,
                                                       CudaBenchmarkDiagnostics& diagnostics);
} // namespace mining
#endif // BITCOIN_MINING_CUDA_DIAGNOSTICS_H
