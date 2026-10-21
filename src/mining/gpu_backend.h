// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_MINING_GPU_BACKEND_H
#define BITCOIN_MINING_GPU_BACKEND_H
#include <array>
#include <crypto/mercahash.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace mining
{
enum class GpuVendor { NVIDIA_CUDA, AMD_OPENCL };
enum class GpuMemorySource { CUDA_FREE, OPENCL_TOTAL, OPENCL_FREE };
// Frozen V1 backend/scheduler bound. V2 capacity is resource-based.
inline constexpr unsigned int MAX_GPU_CONTEXTS{256};
struct GpuDevice {
    GpuDevice() = default;
    explicit GpuDevice(GpuVendor value) : vendor{value} {}
    GpuVendor vendor{GpuVendor::NVIDIA_CUDA};
    mercahash::Version pow_version{mercahash::Version::V1}; // Selected from the immutable mining job.
    unsigned int ordinal{0};
    std::string id;
    std::string name;
    std::string capability;
    uint64_t total_memory{0};
    uint64_t available_memory{0};
    uint64_t max_allocation{0};
    bool available_memory_known{false};
    bool supported{false};
    std::string error;
};
struct GpuDiscovery {
    std::vector<GpuDevice> devices;
    std::vector<std::string> errors;
};
struct GpuConfig {
    bool cpu{true};
    bool gpu{false};
    // Empty selects all supported devices. IDs are namespaced by backend.
    std::vector<std::string> devices;
    unsigned int contexts{0}; // 0: each device's maximum safe budget; positive: manual reduction.
    unsigned int yield_ms{10};
};
struct GpuMemoryBudget {
    unsigned int contexts{0};
    uint64_t scratchpad_bytes{0};
    uint64_t reserved_bytes{0};
    uint64_t overhead_bytes{0};
    uint64_t headroom_bytes{0}; // Remaining free memory when measured; otherwise a conservative budget estimate.
    unsigned int maximum_contexts{0}; // Maximum automatic/manual count, before allocating.
    bool low_headroom{false};
    bool scheduler_limited{false}; // V1 only: more would fit than its frozen 256-context scheduler permits.
    bool headroom_measured{false}; // A backend re-queried free memory after allocating its buffers.
};
GpuMemoryBudget CalculateGpuBudget(uint64_t memory, uint64_t maximum_allocation, unsigned int requested = 0,
                                   GpuMemorySource source = GpuMemorySource::CUDA_FREE,
                                   mercahash::Version version = mercahash::Version::V1);
// Preview only: discovery can become stale. Backends re-query before allocation.
GpuMemoryBudget CalculateGpuBudget(const GpuDevice& device, unsigned int requested = 0);
// Only explicit device-buffer memory exhaustion (or loss of the promised
// reserve) is recoverable. Driver, kernel and verification errors stay fatal.
class GpuMemoryAllocationError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
struct GpuContextAllocation {
    unsigned int estimated{0}, active{0};
};
// Budget is queried with no mining buffers live. Cleanup must release every
// buffer, including partial attempts; initialized runtime objects may be reused.
// A cleanup error aborts the search, even after a recoverable allocation error.
GpuContextAllocation AllocateGpuContexts(unsigned int requested,
    const std::function<GpuMemoryBudget()>& budget,
    const std::function<void(unsigned int, const GpuMemoryBudget&)>& allocate,
    const std::function<void()>& cleanup, const std::function<bool()>& cancelled,
    mercahash::Version version = mercahash::Version::V1);
// Per-launch geometry/32-bit kernel-index and host-size limits, not a total
// resident-context ceiling. The backend supplies its hardware work window.
unsigned int GpuExecutionContexts(unsigned int resident, uint64_t compute_units, uint64_t maximum_groups);
using GpuHeader = std::array<unsigned char, 80>;
using GpuHash = std::array<unsigned char, 32>;
class GpuBackend
{
  public:
    virtual ~GpuBackend() = default;
    virtual unsigned int Contexts() const = 0;
    virtual mercahash::Version PoWVersion() const { return mercahash::Version::V1; }
    virtual unsigned int EstimatedContexts() const { return Contexts(); }
    virtual uint64_t MemoryBytes() const = 0;
    virtual unsigned int BatchContexts() const { return Contexts(); }
    virtual GpuMemoryBudget Budget() const { return {}; }
    // Empty on cancellation; never return partially executed hashes. The
    // predicate is checked between bounded launches, never by a Qt callback.
    virtual std::vector<GpuHash> Hash(std::span<const GpuHeader> headers, const std::function<bool()> &cancelled) = 0;
};
GpuDiscovery DiscoverGpuDevices();
std::unique_ptr<GpuBackend> CreateGpuBackend(const GpuDevice &device, unsigned int contexts,
                                          const std::function<bool()>& cancelled = {});
// Executes the permanent reference vectors on the selected backend at startup.
// A mismatch disables that device before it can mine. A passing self-test is
// runtime evidence only on the device on which it actually executed.
void VerifyGpuBackend(GpuBackend &backend, const std::function<bool()> &cancelled);
#ifdef MERCATURA_CUDA
GpuDiscovery DiscoverCudaDevices();
std::unique_ptr<GpuBackend> CreateCudaBackend(const GpuDevice &device, unsigned int contexts,
                                           const std::function<bool()>& cancelled = {});
#endif
#ifdef MERCATURA_OPENCL
GpuDiscovery DiscoverOpenClDevices();
std::unique_ptr<GpuBackend> CreateOpenClBackend(const GpuDevice &device, unsigned int contexts,
                                             const std::function<bool()>& cancelled = {});
#endif
} // namespace mining
#endif // BITCOIN_MINING_GPU_BACKEND_H
