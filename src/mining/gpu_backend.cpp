// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <algorithm>
#include <crypto/mercahash.h>
#include <limits>
#include <mining/gpu_backend.h>
#include <stdexcept>
namespace mining
{
GpuMemoryBudget CalculateGpuBudget(uint64_t available, uint64_t maximum_allocation, unsigned int requested)
{
    // Leave at least 512 MiB and half of the reported budget for the desktop.
    const uint64_t reserve{std::max<uint64_t>(512ULL << 20, available / 2)};
    const uint64_t context_bytes{mercahash::SCRATCHPAD_BYTES + 4096};
    const uint64_t usable{available > reserve ? available - reserve : 0};
    uint64_t count{std::min(usable / context_bytes, maximum_allocation / mercahash::SCRATCHPAD_BYTES)};
    count = std::min<uint64_t>(count, 256);
    if (requested > count)
        throw std::runtime_error{"GPU context count exceeds the safe memory budget"};
    if (requested)
        count = requested;
    // Automatic mode favors responsiveness. Explicit counts remain bounded by
    // actual memory, allocation limits and the 256-nonce shared range.
    else
        count = std::min<uint64_t>(count, 16);
    count = std::min<uint64_t>(count, 256);
    return {static_cast<unsigned int>(count), count * mercahash::SCRATCHPAD_BYTES, reserve};
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
std::unique_ptr<GpuBackend> CreateGpuBackend(const GpuDevice &device, unsigned int contexts)
{
    if (!device.supported)
        throw std::runtime_error{"Unsupported GPU: " + device.error};
#ifdef MERCATURA_CUDA
    if (device.vendor == GpuVendor::NVIDIA_CUDA)
        return CreateCudaBackend(device, contexts);
#endif
#ifdef MERCATURA_OPENCL
    if (device.vendor == GpuVendor::AMD_OPENCL)
        return CreateOpenClBackend(device, contexts);
#endif
    (void)contexts;
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
