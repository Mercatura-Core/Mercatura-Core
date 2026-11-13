// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_MINING_GPU_POWER_H
#define BITCOIN_MINING_GPU_POWER_H
#include <mining/gpu_backend.h>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace mining {
struct GpuPower {
    double watts{0};
    std::string source; // Includes scope: board versus ASIC/SoC, when known.
    std::chrono::steady_clock::time_point sampled{};
};
std::optional<double> GpuEfficiency(double hashrate, double watts);
std::string GpuPciId(uint32_t domain, unsigned bus, unsigned device, unsigned function);
// Older AMD OpenCL omits the domain. Resolve only an unambiguous PCI match.
std::string GpuPciTopologyId(unsigned bus, unsigned device, unsigned function,
                             const std::string& pci_root = "/sys/bus/pci/devices");
// Injectable runtime library boundary; tests exercise the real readers without GPUs.
class GpuTelemetryLibrary {
public:
    virtual ~GpuTelemetryLibrary() = default;
    virtual void* Symbol(const char* name) = 0;
};
class GpuPowerReader {
public:
    GpuPowerReader();
    GpuPowerReader(std::unique_ptr<GpuTelemetryLibrary> nvml, std::unique_ptr<GpuTelemetryLibrary> adl,
                   std::string pci_root = "/sys/bus/pci/devices");
    ~GpuPowerReader();
    std::optional<GpuPower> Read(const GpuDevice& device) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
class GpuPowerMonitor {
public:
    using Reader = std::function<std::optional<GpuPower>(const GpuDevice&)>;
    explicit GpuPowerMonitor(Reader reader = {});
    ~GpuPowerMonitor();
    void SetDevices(std::vector<GpuDevice> devices);
    std::optional<GpuPower> Get(const GpuDevice& device) const;
    void Stop();
private:
    void Run();
    Reader m_reader;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop{false};
    std::vector<GpuDevice> m_devices;
    struct Sample { GpuDevice device; GpuPower power; };
    std::map<std::string, Sample> m_samples;
    std::thread m_thread;
};
} // namespace mining
#endif
