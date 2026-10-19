// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <algorithm>
#include <crypto/mercahash.h>
#include <limits>
#include <mining/gpu_backend.h>
#include <stdexcept>
namespace mining
{
GpuMemoryBudget CalculateGpuBudget(uint64_t memory, uint64_t maximum_allocation, unsigned int requested,
                                   GpuMemorySource source)
{
    // CUDA sizes from measured free memory. OpenCL total memory is NOT free:
    // without a reliable free-memory query, allow at most a quarter of the
    // reported global pool, also bounded by CL_DEVICE_MAX_MEM_ALLOC_SIZE.
    // Its set-aside is a conservative capacity limit, not measured headroom.
    const uint64_t reserve{std::max<uint64_t>(512ULL << 20,
        source == GpuMemorySource::CUDA_FREE ? memory / 20 : memory - memory / 4)};
    // Headers/states/results and allocation rounding, plus a separate allowance
    // for runtime/JIT/program/stream overhead. Neither consumes the VRAM reserve.
    constexpr uint64_t fixed_overhead{64ULL << 20};
    const uint64_t context_bytes{mercahash::SCRATCHPAD_BYTES + 4096};
    const uint64_t usable{memory > reserve ? memory - reserve : 0};
    const uint64_t capacity{std::min({usable > fixed_overhead ? (usable - fixed_overhead) / context_bytes : 0,
                                    maximum_allocation / mercahash::SCRATCHPAD_BYTES,
                                    uint64_t{std::numeric_limits<size_t>::max()} / mercahash::SCRATCHPAD_BYTES})};
    const auto maximum{static_cast<unsigned int>(std::min<uint64_t>(capacity, MAX_GPU_CONTEXTS))};
    if (requested > maximum)
        throw std::runtime_error{"GPU context count exceeds the safe memory budget"};
    const auto count{requested ? requested : maximum};
    const uint64_t scratch{count * uint64_t{mercahash::SCRATCHPAD_BYTES}};
    const uint64_t overhead{count ? fixed_overhead + count * uint64_t{4096} : 0};
    const uint64_t headroom{memory - scratch - overhead};
    return {count, scratch, reserve, overhead, headroom, maximum,
            count > 0 && headroom < std::max<uint64_t>(1ULL << 30, memory / 10), capacity > MAX_GPU_CONTEXTS};
}
GpuMemoryBudget CalculateGpuBudget(const GpuDevice& device, unsigned int requested)
{
    const bool cuda{device.vendor == GpuVendor::NVIDIA_CUDA};
    return CalculateGpuBudget(cuda ? device.available_memory : device.total_memory, device.max_allocation, requested,
                              cuda ? GpuMemorySource::CUDA_FREE : GpuMemorySource::OPENCL_TOTAL);
}
GpuContextAllocation AllocateGpuContexts(unsigned int requested,
    const std::function<GpuMemoryBudget()>& budget,
    const std::function<void(unsigned int, const GpuMemoryBudget&)>& allocate,
    const std::function<void()>& cleanup, const std::function<bool()>& cancelled)
{
    unsigned int lower{0}, upper{MAX_GPU_CONTEXTS}, next{MAX_GPU_CONTEXTS}, estimated{0};
    // Binary search plus revalidation of the last successful allocation. Bound
    // retries even if other processes continually change memory availability.
    unsigned int attempts{2};
    for (unsigned int n{MAX_GPU_CONTEXTS}; n; n /= 2) attempts += 2;
    for (unsigned int attempt{0}; attempt < attempts; ++attempt) {
        try {
            if (cancelled && cancelled()) throw std::runtime_error{"GPU allocation cancelled"};
            const auto current{budget()};
            if (attempt == 0) estimated = current.contexts;
            upper = std::min(upper, current.contexts);
            if (!upper) throw GpuMemoryAllocationError{"No safe GPU context allocation is available"};
            if (requested > upper) throw GpuMemoryAllocationError{"GPU context count exceeds the safe memory budget"};
            next = requested ? requested : std::min(next, upper);
            allocate(next, current);
            if (cancelled && cancelled()) throw std::runtime_error{"GPU allocation cancelled"};
            if (requested || next == upper) return {estimated, next};
            lower = next;
        } catch (const GpuMemoryAllocationError&) {
            cleanup();
            if (requested || !upper || next <= 1) throw;
            upper = std::min(upper, next - 1);
            if (lower > upper) lower = 0; // A previously successful count became unavailable.
            next = lower + (upper - lower + 1) / 2;
            continue;
        } catch (...) {
            cleanup();
            throw;
        }
        // Release successful probes too: each new query sees the full available
        // pool. The final returned allocation is always freshly validated.
        cleanup();
        if (lower > upper) lower = 0;
        next = lower + (upper - lower + 1) / 2;
    }
    throw GpuMemoryAllocationError{"GPU memory availability did not stabilize during bounded allocation retries"};
}
GpuDiscovery DiscoverGpuDevices()
{
    GpuDiscovery result;
    auto append = [&](GpuDiscovery other) {
        result.devices.insert(result.devices.end(), other.devices.begin(), other.devices.end());
        result.errors.insert(result.errors.end(), other.errors.begin(), other.errors.end());
    };
#ifdef MERCATURA_CUDA
    append(DiscoverCudaDevices());
#else
    result.errors.emplace_back("NVIDIA CUDA backend was not compiled");
#endif
#ifdef MERCATURA_OPENCL
    append(DiscoverOpenClDevices());
#else
    result.errors.emplace_back("AMD OpenCL backend was not compiled");
#endif
    (void)append;
    return result;
}
std::unique_ptr<GpuBackend> CreateGpuBackend(const GpuDevice &device, unsigned int contexts, const std::function<bool()>& cancelled)
{
    if (!device.supported)
        throw std::runtime_error{"Unsupported GPU: " + device.error};
#ifdef MERCATURA_CUDA
    if (device.vendor == GpuVendor::NVIDIA_CUDA)
        return CreateCudaBackend(device, contexts, cancelled);
#endif
#ifdef MERCATURA_OPENCL
    if (device.vendor == GpuVendor::AMD_OPENCL)
        return CreateOpenClBackend(device, contexts, cancelled);
#endif
    (void)contexts;
    (void)cancelled;
    throw std::runtime_error{"Selected GPU backend was not compiled"};
}
void VerifyGpuBackend(GpuBackend &backend, const std::function<bool()> &cancelled)
{
    constexpr const char *vectors[]{"cc97e7cbce1d4ed2b54c9c37a81a000383229341a277a44829484855b9ef79dc",
                                    "2321712af21502878986c0c4f21d79e17e2281619e3958f11e3c634c9f17f7d8",
                                    "9c9a427928875304bb7d6351354b5e50ec8e57c581971f68eab176371c4cd2c2",
                                    "1244c6c9e0365baeb5958cfaa57aff582d81ae1d757769e823085bb00676e170"};
    auto nibble = [](char value) { return value <= '9' ? value - '0' : value - 'a' + 10; };
    // Different inputs in every slot also exercise isolation and reuse.
    for (unsigned int base{0}; base < 4 && !cancelled(); ++base) {
        std::vector<GpuHeader> headers(backend.Contexts());
        for (size_t slot{0}; slot < headers.size(); ++slot) {
            const unsigned int pattern{static_cast<unsigned int>((base + slot) % 4)};
            for (size_t byte{0}; byte < 80; ++byte)
                headers[slot][byte] = pattern == 0   ? 0
                                      : pattern == 1 ? byte
                                      : pattern == 2 ? 255
                                                     : (byte * 37 + 11) & 255;
        }
        const auto results{backend.Hash(headers, cancelled)};
        if (cancelled())
            return;
        if (results.size() != headers.size())
            throw std::runtime_error{"GPU self-test returned an incomplete batch"};
        for (size_t slot{0}; slot < results.size(); ++slot) {
            const char *expected{vectors[(base + slot) % 4]};
            for (size_t byte{0}; byte < 32; ++byte) {
                if (results[slot][byte] !=
                    static_cast<unsigned char>(nibble(expected[byte * 2]) * 16 + nibble(expected[byte * 2 + 1])))
                    throw std::runtime_error{"GPU MercaHash V1 reference-vector mismatch"};
            }
        }
    }
}
} // namespace mining
