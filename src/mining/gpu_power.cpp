// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <mining/gpu_power.h>
#include <tinyformat.h>
#include <util/fs.h>
#include <cmath>
#include <charconv>
#include <cstdlib>
#include <fstream>
#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace mining {
namespace {
#if defined(WIN32) || defined(__linux__)
class Library final : public GpuTelemetryLibrary {
#ifdef WIN32
    HMODULE m_handle{nullptr};
#else
    void* m_handle{nullptr};
#endif
public:
    explicit Library(std::initializer_list<const char*> paths)
    {
        for (const auto* path : paths) {
#ifdef WIN32
            // Driver libraries only; never search the working directory.
            m_handle = LoadLibraryExA(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
            m_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
            if (m_handle) break;
        }
    }
    ~Library() override
    {
#ifdef WIN32
        if (m_handle) FreeLibrary(m_handle);
#else
        if (m_handle) dlclose(m_handle);
#endif
    }
    void* Symbol(const char* name) override
    {
#ifdef WIN32
        return m_handle ? reinterpret_cast<void*>(GetProcAddress(m_handle, name)) : nullptr;
#else
        return m_handle ? dlsym(m_handle, name) : nullptr;
#endif
    }
};
#endif
template<typename T> T Symbol(const std::unique_ptr<GpuTelemetryLibrary>& library, const char* name)
{
    return library ? reinterpret_cast<T>(library->Symbol(name)) : nullptr;
}
std::unique_ptr<GpuTelemetryLibrary> NvmlLibrary()
{
#ifdef WIN32
    return std::make_unique<Library>(std::initializer_list<const char*>{"nvml.dll"});
#elif defined(__linux__)
    return std::make_unique<Library>(std::initializer_list<const char*>{"libnvidia-ml.so.1", "/usr/lib/wsl/lib/libnvidia-ml.so.1"});
#else
    return {};
#endif
}
std::unique_ptr<GpuTelemetryLibrary> AdlLibrary()
{
#ifdef WIN32
    return std::make_unique<Library>(std::initializer_list<const char*>{"atiadlxx.dll"});
#else
    return {};
#endif
}
// Windows ADL AdapterInfo ABI (256-byte strings), kept private to telemetry.
// Layout documented by AMD's display-library include/adl_structures.h.
struct AdlAdapter {
    int size, index;
    char udid[256];
    int bus, device, function, vendor;
    char name[256], display[256];
    int present, exist;
    char path[256], path_ext[256], pnp[256];
    int os_display;
};
static_assert(sizeof(AdlAdapter) == 1572);
void* AdlAllocate(int bytes) { return bytes > 0 ? std::malloc(bytes) : nullptr; }
#ifdef __linux__
std::string ReadText(const fs::path& path)
{
    std::ifstream file{path.std_path()};
    std::string result;
    std::getline(file, result);
    return result;
}
#endif
bool SameHardware(const GpuDevice& a, const GpuDevice& b)
{
    return a.vendor == b.vendor && a.uuid == b.uuid && a.pci_id == b.pci_id;
}
} // namespace
std::string GpuPciId(uint32_t domain, unsigned bus, unsigned device, unsigned function)
{
    if (bus > 255 || device > 31 || function > 7) return {};
    return strprintf("%04x:%02x:%02x.%x", domain, bus, device, function);
}
std::string GpuPciTopologyId(unsigned bus, unsigned device, unsigned function, [[maybe_unused]] const std::string& pci_root)
{
    const auto zero{GpuPciId(0, bus, device, function)};
    if (zero.empty()) return {};
#ifdef WIN32
    return zero; // ADL and OpenCL use the same Windows BDF domain convention.
#elif defined(__linux__)
    try {
        std::string match;
        std::error_code ec;
        for (const auto& pci : fs::directory_iterator{fs::u8path(pci_root), ec}) {
            const fs::path path{pci.path()};
            const auto id{fs::path{path.filename()}.utf8string()};
            if (id.size() < zero.size() || !id.ends_with(zero.substr(4)) || ReadText(path / "vendor") != "0x1002") continue;
            if (!match.empty()) return {}; // Multiple domains: never guess.
            match = id;
        }
        return match;
    } catch (...) { return {}; }
#else
    return {};
#endif
}
std::optional<double> GpuEfficiency(double hashrate, double watts)
{
    if (!std::isfinite(hashrate) || !std::isfinite(watts) || hashrate <= 0 || watts <= 0) return {};
    const double result{hashrate / watts};
    return std::isfinite(result) ? std::optional<double>{result} : std::nullopt;
}
struct GpuPowerReader::Impl {
    std::unique_ptr<GpuTelemetryLibrary> nvml, adl;
    std::string pci_root;
    bool nvml_ready{false};
    void* adl_context{nullptr};
    // NVML's stable C ABI uses integer status codes and opaque handles.
    int (*nvml_init)(){nullptr};
    int (*nvml_shutdown)(){nullptr};
    int (*nvml_uuid)(const char*, void**){nullptr};
    int (*nvml_power)(void*, unsigned*){nullptr};
    int (*adl_create)(void* (*)(int), int, void**){nullptr};
    int (*adl_destroy)(void*){nullptr};
    int (*adl_count)(void*, int*){nullptr};
    int (*adl_info)(void*, AdlAdapter*, int){nullptr};
    int (*adl_power)(void*, int, int, int*){nullptr};
    Impl(std::unique_ptr<GpuTelemetryLibrary> n, std::unique_ptr<GpuTelemetryLibrary> a, std::string root)
        : nvml{std::move(n)}, adl{std::move(a)}, pci_root{std::move(root)}
    {
#define LOAD(member, library, name) member = Symbol<decltype(member)>(library, name)
        LOAD(nvml_init, nvml, "nvmlInit_v2");
        LOAD(nvml_shutdown, nvml, "nvmlShutdown");
        LOAD(nvml_uuid, nvml, "nvmlDeviceGetHandleByUUID");
        LOAD(nvml_power, nvml, "nvmlDeviceGetPowerUsage");
        LOAD(adl_create, adl, "ADL2_Main_Control_Create");
        LOAD(adl_destroy, adl, "ADL2_Main_Control_Destroy");
        LOAD(adl_count, adl, "ADL2_Adapter_NumberOfAdapters_Get");
        LOAD(adl_info, adl, "ADL2_Adapter_AdapterInfo_Get");
        LOAD(adl_power, adl, "ADL2_Overdrive6_CurrentPower_Get");
#undef LOAD
    }
    ~Impl()
    {
        if (nvml_ready) nvml_shutdown();
        if (adl_context) adl_destroy(adl_context);
    }
    std::optional<GpuPower> Read(const GpuDevice& device)
    {
        if (!device.dedicated) return {};
        if (device.vendor == GpuVendor::NVIDIA_CUDA) {
            if (device.uuid.empty() || !nvml_init || !nvml_shutdown || !nvml_uuid || !nvml_power) return {};
            if (!nvml_ready) nvml_ready = nvml_init() == 0;
            if (!nvml_ready) return {};
            void* handle{nullptr};
            unsigned milliwatts{0};
            // Resolve on every sample: no stale handles after driver/device loss.
            if (nvml_uuid(device.uuid.c_str(), &handle) != 0 || !handle || nvml_power(handle, &milliwatts) != 0 || !milliwatts) return {};
            return GpuPower{milliwatts / 1000.0, "NVIDIA NVML: device/board power (driver averaging)", std::chrono::steady_clock::now()};
        }
        if (device.pci_id.empty()) return {};
        if (adl_create && adl_destroy && adl_count && adl_info && adl_power) {
            if (!adl_context && adl_create(AdlAllocate, 1, &adl_context) != 0) { adl_context = nullptr; return {}; }
            int count{0};
            if (!adl_context || adl_count(adl_context, &count) != 0 || count <= 0 || count > 250) return {};
            std::vector<AdlAdapter> adapters(count);
            for (auto& a : adapters) a.size = sizeof(AdlAdapter);
            if (adl_info(adl_context, adapters.data(), count * sizeof(AdlAdapter)) != 0) return {};
            for (const auto& a : adapters) {
                // ADL exposes BDF without domain on Windows (domain zero).
                if (!a.present || a.bus < 0 || a.device < 0 || a.function < 0 ||
                    GpuPciId(0, a.bus, a.device, a.function) != device.pci_id) continue;
                int fixed_watts{0};
                if (adl_power(adl_context, a.index, 0, &fixed_watts) == 0 && fixed_watts > 0)
                    return GpuPower{fixed_watts / 256.0, "AMD ADL: ASIC total power (GPU chip, excludes board components)", std::chrono::steady_clock::now()};
            }
            return {};
        }
#ifdef __linux__
        // The supported amdgpu hwmon ABI needs no ROCm installation. Paths are
        // resolved by full PCI BDF, never by display/driver enumeration order.
        if (device.pci_id.find_first_not_of("0123456789abcdef:.") != std::string::npos) return {};
        const auto base{fs::u8path(pci_root) / fs::u8path(device.pci_id)};
        if (ReadText(base / "vendor") != "0x1002") return {};
        std::error_code ec;
        for (const auto& hwmon : fs::directory_iterator{base / "hwmon", ec}) {
            const fs::path path{hwmon.path()};
            if (ReadText(path / "name") != "amdgpu") continue;
            for (const auto* sensor : {"power1_average", "power1_input"}) {
                const auto text{ReadText(path / sensor)};
                uint64_t microwatts{0};
                const auto parsed{std::from_chars(text.data(), text.data() + text.size(), microwatts)};
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !microwatts || microwatts == UINT64_MAX) continue;
                auto label{ReadText(path / "power1_label")};
                if (label.size() > 80) label.resize(80);
                return GpuPower{microwatts / 1000000.0,
                    std::string{"AMD amdgpu hwmon: SoC power (not whole-board power); "} + sensor + (label.empty() ? "" : "; " + label),
                    std::chrono::steady_clock::now()};
            }
        }
#endif
        return {};
    }
};
GpuPowerReader::GpuPowerReader() : GpuPowerReader{NvmlLibrary(), AdlLibrary()} {}
GpuPowerReader::GpuPowerReader(std::unique_ptr<GpuTelemetryLibrary> nvml, std::unique_ptr<GpuTelemetryLibrary> adl, std::string pci_root)
    : m_impl{std::make_unique<Impl>(std::move(nvml), std::move(adl), std::move(pci_root))} {}
GpuPowerReader::~GpuPowerReader() = default;
std::optional<GpuPower> GpuPowerReader::Read(const GpuDevice& device) noexcept
{
    try { return m_impl->Read(device); } catch (...) { return {}; }
}
GpuPowerMonitor::GpuPowerMonitor(Reader reader) : m_reader{std::move(reader)}
{
    // Thread creation/resource failures disable optional telemetry only.
    try { m_thread = std::thread{[this] { Run(); }}; } catch (...) {}
}
GpuPowerMonitor::~GpuPowerMonitor() { Stop(); }
void GpuPowerMonitor::Stop()
{
    { std::lock_guard lock{m_mutex}; m_stop = true; }
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}
void GpuPowerMonitor::SetDevices(std::vector<GpuDevice> devices)
{
    std::lock_guard lock{m_mutex};
    m_devices = std::move(devices);
    m_samples.clear();
}
std::optional<GpuPower> GpuPowerMonitor::Get(const GpuDevice& device) const
{
    std::lock_guard lock{m_mutex};
    const auto it{m_samples.find(device.id)};
    if (it == m_samples.end() || !SameHardware(it->second.device, device) ||
        std::chrono::steady_clock::now() - it->second.power.sampled > std::chrono::seconds{3}) return {};
    return it->second.power;
}
void GpuPowerMonitor::Run()
{
    try {
        std::unique_ptr<GpuPowerReader> native;
        if (!m_reader) native = std::make_unique<GpuPowerReader>();
        std::unique_lock lock{m_mutex};
        while (!m_stop) {
            const auto devices{m_devices};
            lock.unlock();
            std::map<std::string, Sample> samples;
            for (const auto& device : devices) {
                try {
                    auto power{m_reader ? m_reader(device) : native->Read(device)};
                    if (power && std::isfinite(power->watts) && power->watts > 0) {
                        power->sampled = std::chrono::steady_clock::now();
                        samples.emplace(device.id, Sample{device, std::move(*power)});
                    }
                } catch (...) {} // One bad sensor must not affect other GPUs.
            }
            lock.lock();
            m_samples = std::move(samples); // Failure removes the previous reading.
            m_wake.wait_for(lock, std::chrono::seconds{1}, [this] { return m_stop; });
        }
    } catch (...) {} // Optional telemetry never enters the mining failure path.
}
} // namespace mining
