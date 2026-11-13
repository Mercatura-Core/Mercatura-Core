// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Fast telemetry ABI and sampler tests; no physical GPU or vendor SDK required.
#include <mining/gpu_power.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace mining;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error{message}; }
int status{0}, initializations{0}, shutdowns{0};
unsigned power{67750};
int Init() { ++initializations; return status; }
int Shutdown() { ++shutdowns; return 0; }
int Handle(const char* uuid, void** handle)
{
    if (std::strcmp(uuid, "GPU-second") == 0) *handle = reinterpret_cast<void*>(2);
    else if (std::strcmp(uuid, "GPU-first") == 0) *handle = reinterpret_cast<void*>(1);
    else return 1;
    return status;
}
int Power(void* handle, unsigned* watts) { *watts = handle == reinterpret_cast<void*>(2) ? power * 2 : power; return status; }
class FakeNvml : public GpuTelemetryLibrary {
public:
    void* Symbol(const char* name) override
    {
#define EXPORT(n, f) if (std::strcmp(name, n) == 0) return reinterpret_cast<void*>(&f)
        EXPORT("nvmlInit_v2", Init);
        EXPORT("nvmlShutdown", Shutdown);
        EXPORT("nvmlDeviceGetHandleByUUID", Handle);
        EXPORT("nvmlDeviceGetPowerUsage", Power);
#undef EXPORT
        return nullptr;
    }
};
int adl_status{0}, adl_fixed_watts{256 * 75}, adl_destroyed{0};
int AdlCreate(void* (*allocate)(int), int, void** context)
{
    std::free(allocate(64));
    if (adl_status) return adl_status;
    *context = reinterpret_cast<void*>(1); return 0;
}
int AdlDestroy(void*) { ++adl_destroyed; return 0; }
int AdlCount(void*, int* count) { *count = 2; return adl_status; }
int AdlInfo(void*, void* data, int bytes)
{
    Require(bytes == 2 * 1572, "ADL AdapterInfo Windows ABI");
    auto* buffer{static_cast<unsigned char*>(data)};
    for (int i{0}; i < 2; ++i) {
        auto field = [&](size_t offset, int value) { std::memcpy(buffer + i * 1572 + offset, &value, sizeof(value)); };
        field(4, 17 + i); // Adapter index is independent of BDF/miner ordinal.
        field(264, i + 3); field(268, 0); field(272, 0); field(792, 1);
    }
    return adl_status;
}
int AdlPower(void*, int adapter, int type, int* watts)
{
    Require(type == 0, "ADL ASIC total power, not TDP or a power limit");
    *watts = adl_fixed_watts + (adapter == 18 ? 256 * 25 : 0); return adl_status;
}
class FakeAdl : public GpuTelemetryLibrary {
public:
    void* Symbol(const char* name) override
    {
#define EXPORT(n, f) if (std::strcmp(name, n) == 0) return reinterpret_cast<void*>(&f)
        EXPORT("ADL2_Main_Control_Create", AdlCreate);
        EXPORT("ADL2_Main_Control_Destroy", AdlDestroy);
        EXPORT("ADL2_Adapter_NumberOfAdapters_Get", AdlCount);
        EXPORT("ADL2_Adapter_AdapterInfo_Get", AdlInfo);
        EXPORT("ADL2_Overdrive6_CurrentPower_Get", AdlPower);
#undef EXPORT
        return nullptr;
    }
};
class MissingLibrary : public GpuTelemetryLibrary {
    void* Symbol(const char*) override { return nullptr; }
};
template<typename F> void Eventually(F predicate)
{
    const auto end{std::chrono::steady_clock::now() + std::chrono::seconds{5}};
    while (!predicate()) {
        Require(std::chrono::steady_clock::now() < end, "Sampler deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}
}
int main()
{
    try {
        GpuDevice first{GpuVendor::NVIDIA_CUDA}; first.id = "cuda:8"; first.uuid = "GPU-first";
        GpuDevice second{first}; second.id = "cuda:0"; second.uuid = "GPU-second";
        {
            GpuPowerReader reader{std::make_unique<FakeNvml>(), {}};
            Require(reader.Read(first)->watts == 67.75, "NVML milliwatts");
            Require(reader.Read(second)->watts == 135.5, "NVML UUID, not ordinal association");
            status = 1;
            Require(!reader.Read(first), "NVML error/device loss");
            status = 0; power = 0;
            Require(!reader.Read(first), "Zero NVML power");
            power = 67750;
            first.uuid = "GPU-missing";
            Require(!reader.Read(first), "Missing GPU");
            first.uuid.clear();
            Require(!reader.Read(first), "Missing hardware identity");
            first.uuid = "GPU-first";
        }
        Require(initializations == 1 && shutdowns == 1, "Balanced NVML lifetime");
        status = 1;
        { GpuPowerReader reader{std::make_unique<FakeNvml>(), {}}; Require(!reader.Read(first), "NVML init failure"); }
        status = 0;
        { GpuPowerReader reader{std::make_unique<MissingLibrary>(), {}}; Require(!reader.Read(first), "Missing NVML symbols"); }
        {
            GpuPowerReader reader{{}, std::make_unique<FakeAdl>()};
            GpuDevice amd{GpuVendor::AMD_OPENCL}; amd.pci_id = GpuPciId(0, 3, 0, 0);
            Require(reader.Read(amd)->watts == 75, "ADL fixed point watts");
            Require(reader.Read(amd)->source.find("ASIC") != std::string::npos, "ADL chip scope");
            amd.pci_id = GpuPciId(0, 4, 0, 0);
            Require(reader.Read(amd)->watts == 100, "ADL multiple GPUs, PCI association");
            adl_status = 1;
            Require(!reader.Read(amd), "ADL driver failure");
            adl_status = 0; adl_fixed_watts = 0;
            amd.pci_id = GpuPciId(0, 3, 0, 0);
            Require(!reader.Read(amd), "ADL zero power");
            amd.pci_id = GpuPciId(0, 5, 0, 0);
            Require(!reader.Read(amd), "ADL disappeared GPU");
        }
        Require(adl_destroyed == 1, "Balanced ADL context");
        adl_status = 1;
        { GpuPowerReader reader{{}, std::make_unique<FakeAdl>()};
          GpuDevice amd{GpuVendor::AMD_OPENCL}; amd.pci_id = GpuPciId(0, 3, 0, 0);
          Require(!reader.Read(amd), "ADL init failure"); }
        adl_status = 0;
        Require(std::abs(*GpuEfficiency(1606, 67.75) - 23.704797) < 0.00001, "H/s/W");
        for (double value : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            Require(!GpuEfficiency(1606, value), "Invalid watts");
            Require(!GpuEfficiency(value, 67.75), "Invalid/startup hashrate");
        }
#ifdef __linux__
        const auto root{std::filesystem::temp_directory_path() / ("mercatura-power-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
        struct Cleanup { std::filesystem::path root; ~Cleanup() { std::filesystem::remove_all(root); } } cleanup{root};
        GpuDevice amd{GpuVendor::AMD_OPENCL}; amd.id = "opencl:9"; amd.pci_id = GpuPciId(1, 3, 0, 0);
        GpuDevice amd2{amd}; amd2.id = "opencl:0"; amd2.pci_id = GpuPciId(0, 3, 0, 0);
        for (const auto& device : {amd, amd2}) {
            auto dir{root / device.pci_id};
            std::filesystem::create_directories(dir / "hwmon/hwmon7");
            std::ofstream{dir / "vendor"} << "0x1002\n";
            std::ofstream{dir / "hwmon/hwmon7/name"} << "amdgpu\n";
            std::ofstream{dir / "hwmon/hwmon7/power1_average"} << (device.id == amd.id ? "120500000" : "90000000");
        }
        Require(GpuPciTopologyId(3, 0, 0, root.string()).empty(), "Ambiguous AMD PCI domain rejected");
        GpuPowerReader amd_reader{{}, {}, root.string()};
        Require(amd_reader.Read(amd)->watts == 120.5, "AMD microwatts/full PCI domain association");
        Require(amd_reader.Read(amd2)->watts == 90.0, "AMD multi GPU");
        Require(amd_reader.Read(amd)->source.find("SoC") != std::string::npos, "Power scope disclosed");
        const auto sensor{root / amd.pci_id / "hwmon/hwmon7/power1_average"};
        std::ofstream{sensor} << 0;
        Require(!amd_reader.Read(amd), "Zero AMD power");
        std::ofstream{sensor} << "invalid";
        Require(!amd_reader.Read(amd), "Invalid AMD power");
        std::ofstream{sensor} << "-1";
        Require(!amd_reader.Read(amd), "Invalid unsigned AMD sentinel");
        std::filesystem::remove(sensor);
        std::ofstream{sensor.parent_path() / "power1_input"} << "67500000";
        Require(amd_reader.Read(amd)->watts == 67.5, "AMD instantaneous fallback");
        std::filesystem::remove_all(root / amd.pci_id);
        Require(!amd_reader.Read(amd), "AMD disappeared");
        Require(GpuPciTopologyId(3, 0, 0, root.string()) == amd2.pci_id, "Unique AMD topology domain resolved");
        Require(amd_reader.Read(amd2).has_value(), "Another AMD device unaffected");
        amd2.dedicated = false;
        Require(!amd_reader.Read(amd2), "APU CPU-inclusive power excluded");
#endif
        std::atomic<int> calls{0};
        std::atomic<bool> fail{false};
        const auto ui_thread{std::this_thread::get_id()};
        GpuPowerMonitor monitor{[&](const GpuDevice& device) -> std::optional<GpuPower> {
            Require(std::this_thread::get_id() != ui_thread, "Driver sampled off UI thread");
            ++calls;
            if (fail) throw std::runtime_error{"Driver error"};
            return GpuPower{device.uuid == first.uuid ? 67.75 : 135.5, "mock"};
        }};
        monitor.SetDevices({second, first});
        Eventually([&] { return monitor.Get(first) && monitor.Get(second); });
        Require(monitor.Get(first)->watts == 67.75 && monitor.Get(second)->watts == 135.5, "Cached multi GPU association");
        GpuDevice replaced{first}; replaced.uuid = "replacement";
        Require(!monitor.Get(replaced), "Never reuse readings for a replaced GPU ordinal");
        fail = true;
        Eventually([&] { return !monitor.Get(first); });
        Require(!monitor.Get(second), "Failure invalidates old cache");
        fail = false;
        Eventually([&] { return monitor.Get(second).has_value(); });
        Require(calls >= 6, "Polling continues after exceptions");
        monitor.Stop();
        std::cout << "GPU power: NVML, AMD ADL/hwmon, identity, multi GPU, invalid readings, efficiency, background failure/recovery passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
