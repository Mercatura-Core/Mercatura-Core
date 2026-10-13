// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <mining/cpu_miner.h>

#include <crypto/mercahash.h>

#include <algorithm>
#include <bit>
#include <cerrno>
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sched.h>
#endif

namespace mining {
static_assert(WORKER_MEMORY_BYTES == mercahash::SCRATCHPAD_BYTES + 32ULL * 1024 * 1024);

namespace {
std::optional<uint64_t> Number(std::string_view text)
{
    uint64_t value;
    const auto [end, error]{std::from_chars(text.data(), text.data() + text.size(), value)};
    if (error != std::errc{} || end != text.data() + text.size()) return {};
    return value;
}

std::optional<uint64_t> ReadNumber(const ResourceFileReader& read, const std::string& path)
{
    std::istringstream file{read(path)};
    std::string token;
    file >> token;
    return Number(token);
}

std::optional<uint64_t> KiBField(const std::string& content, const std::string& name)
{
    std::istringstream file{content};
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields{line};
        std::string key, token, unit;
        if (fields >> key >> token >> unit && key == name && unit == "kB") {
            const auto kib{Number(token)};
            if (kib && *kib <= std::numeric_limits<uint64_t>::max() / 1024) return *kib * 1024;
        }
    }
    return {};
}

std::optional<unsigned int> CpuListCount(std::string_view list)
{
    uint64_t total{0}, previous{0};
    bool first{true};
    while (!list.empty()) {
        const auto comma{list.find(',')};
        const auto item{list.substr(0, comma)};
        const auto dash{item.find('-')};
        const auto begin{Number(item.substr(0, dash))};
        const auto end{dash == std::string_view::npos ? begin : Number(item.substr(dash + 1))};
        if (!begin || !end || *begin > *end || *end > std::numeric_limits<unsigned int>::max() || (!first && *begin <= previous)) return {};
        total = std::min<uint64_t>(std::numeric_limits<unsigned int>::max(), total + *end - *begin + 1);
        previous = *end;
        first = false;
        if (comma == std::string_view::npos) break;
        list.remove_prefix(comma + 1);
        if (list.empty()) return {};
    }
    return total > 0 ? std::optional<unsigned int>{static_cast<unsigned int>(total)} : std::nullopt;
}

bool HasController(const std::string& list, const std::string& controller)
{
    return ("," + list + ",").find("," + controller + ",") != std::string::npos;
}

std::string MountPath(const std::string& encoded)
{
    std::string decoded;
    for (size_t i{0}; i < encoded.size(); ++i) {
        if (encoded[i] == '\\' && i + 3 < encoded.size() &&
            encoded[i + 1] >= '0' && encoded[i + 1] <= '7' &&
            encoded[i + 2] >= '0' && encoded[i + 2] <= '7' &&
            encoded[i + 3] >= '0' && encoded[i + 3] <= '7') {
            decoded += static_cast<char>((encoded[i + 1] - '0') * 64 + (encoded[i + 2] - '0') * 8 + encoded[i + 3] - '0');
            i += 3;
        } else decoded += encoded[i];
    }
    return decoded;
}

bool SafeGroup(const std::string& group)
{
    if (group.empty() || group.front() != '/') return false;
    std::istringstream parts{group};
    std::string part;
    while (std::getline(parts, part, '/')) if (part == ".." || part == ".") return false;
    return true;
}
} // namespace

WorkerLimits CalculateWorkerLimits(unsigned int cpus, std::optional<uint64_t> memory)
{
    WorkerLimits limits;
    limits.logical_cpus = std::max(1U, cpus);
    limits.available_memory = memory;
    // Unknown memory: allow at most one worker; allocation failures still stop
    // the session. Known low memory: disable mining rather than overcommit.
    uint64_t memory_workers{1};
    if (memory) {
        const uint64_t reserve{std::max<uint64_t>(512ULL * 1024 * 1024, *memory / 4)};
        memory_workers = *memory > reserve ? (*memory - reserve) / WORKER_MEMORY_BYTES : 0;
    }
    limits.maximum = static_cast<unsigned int>(std::min<uint64_t>(limits.logical_cpus, memory_workers));
    limits.recommended = std::min(limits.maximum, std::max(1U, limits.logical_cpus / 2));
    return limits;
}

unsigned int MaximumCpuWorkers(const WorkerLimits& limits, size_t gpu_verifiers)
{
    const auto host{CalculateWorkerLimits(std::numeric_limits<unsigned int>::max(), limits.available_memory)};
    return gpu_verifiers < host.maximum ? std::min(limits.maximum, static_cast<unsigned int>(host.maximum - gpu_verifiers)) : 0;
}

WorkerLimits ReadLinuxWorkerLimits(unsigned int cpus, std::optional<unsigned int> affinity_cpus, const ResourceFileReader& read)
{
    cpus = std::max(1U, cpus);
    if (affinity_cpus && *affinity_cpus > 0) cpus = std::min(cpus, *affinity_cpus);
    auto memory{KiBField(read("/proc/meminfo"), "MemAvailable:")};
    const auto status{read("/proc/self/status")};
    std::istringstream status_file{status};
    std::string line;
    while (std::getline(status_file, line)) {
        std::istringstream fields{line};
        std::string key, list;
        if (fields >> key >> list && key == "Cpus_allowed_list:") {
            if (const auto count{CpuListCount(list)}) cpus = std::min(cpus, *count);
        }
    }
    const auto restrict_memory{[&](uint64_t maximum, uint64_t used) {
        const uint64_t remaining{maximum > used ? maximum - used : 0};
        memory = memory ? std::min(*memory, remaining) : remaining;
    }};
    // Address-space limits can prevent scratchpad allocation even with free RAM.
    std::istringstream process_limits{read("/proc/self/limits")};
    while (std::getline(process_limits, line)) {
        if (!line.starts_with("Max address space")) continue;
        std::istringstream fields{line.substr(std::string{"Max address space"}.size())};
        std::string soft;
        fields >> soft;
        const auto maximum{Number(soft)};
        const auto used{KiBField(status, "VmSize:")};
        if (maximum && used) restrict_memory(*maximum, *used);
    }
    struct Group { std::string controllers; std::string path; };
    std::vector<Group> groups;
    std::istringstream cgroups{read("/proc/self/cgroup")};
    while (std::getline(cgroups, line)) {
        const auto first{line.find(':')};
        const auto second{first == std::string::npos ? first : line.find(':', first + 1)};
        if (second == std::string::npos) continue;
        const auto path{line.substr(second + 1)};
        if (SafeGroup(path)) groups.push_back({line.substr(first + 1, second - first - 1), path});
    }
    std::istringstream mounts{read("/proc/self/mountinfo")};
    while (std::getline(mounts, line)) {
        const auto separator{line.find(" - ")};
        if (separator == std::string::npos) continue;
        std::istringstream before{line.substr(0, separator)}, after{line.substr(separator + 3)};
        std::string id, parent, device, root, mount, type, source, controllers;
        if (!(before >> id >> parent >> device >> root >> mount) || !(after >> type >> source >> controllers)) continue;
        if (type != "cgroup2" && type != "cgroup") continue;
        root = MountPath(root);
        mount = MountPath(mount);
        if (!SafeGroup(mount) || mount == "/") continue;
        for (const auto& group : groups) {
            const bool v2{type == "cgroup2"};
            const bool cpu{v2 ? group.controllers.empty() : HasController(group.controllers, "cpu") && HasController(controllers, "cpu")};
            const bool mem{v2 ? group.controllers.empty() : HasController(group.controllers, "memory") && HasController(controllers, "memory")};
            const bool cpuset{v2 ? group.controllers.empty() : HasController(group.controllers, "cpuset") && HasController(controllers, "cpuset")};
            if (!cpu && !mem && !cpuset) continue;
            // A bind mount can expose only a subtree. In a cgroup namespace the
            // membership path is already relative to the visible mount root.
            std::string relative{group.path};
            if (root != "/" && (relative == root || relative.starts_with(root + "/"))) relative.erase(0, root.size());
            std::string path{mount + (relative == "/" ? "" : relative)};
            while (true) {
                if (mem) {
                    const auto maximum{ReadNumber(read, path + (v2 ? "/memory.max" : "/memory.limit_in_bytes"))};
                    const auto used{ReadNumber(read, path + (v2 ? "/memory.current" : "/memory.usage_in_bytes"))};
                    if (maximum && used) restrict_memory(*maximum, *used);
                }
                if (cpu) {
                    std::optional<uint64_t> quota, period;
                    if (v2) {
                        std::istringstream file{read(path + "/cpu.max")};
                        std::string q, p;
                        file >> q >> p;
                        quota = Number(q); period = Number(p);
                    } else {
                        quota = ReadNumber(read, path + "/cpu.cfs_quota_us");
                        period = ReadNumber(read, path + "/cpu.cfs_period_us");
                    }
                    if (quota && period && *quota > 0 && *period > 0) cpus = static_cast<unsigned int>(std::min<uint64_t>(cpus, std::max<uint64_t>(1, *quota / *period)));
                }
                if (cpuset) {
                    std::istringstream file{read(path + (v2 ? "/cpuset.cpus.effective" : "/cpuset.effective_cpus"))};
                    std::string list;
                    file >> list;
                    if (list.empty()) { std::istringstream configured{read(path + "/cpuset.cpus")}; configured >> list; }
                    if (const auto count{CpuListCount(list)}) cpus = std::min(cpus, *count);
                }
                if (path == mount) break;
                const auto slash{path.find_last_of('/')};
                if (slash <= mount.size()) path = mount;
                else path.resize(slash);
            }
        }
    }
    return CalculateWorkerLimits(cpus, memory);
}

WorkerLimits DetectWorkerLimits()
{
    unsigned int cpus{std::max(1U, std::thread::hardware_concurrency())};
    std::optional<uint64_t> memory;
#ifdef WIN32
    const DWORD count{GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)};
    if (count) cpus = count;
    USHORT group_count{0};
    GetProcessGroupAffinity(GetCurrentProcess(), &group_count, nullptr);
    std::vector<USHORT> groups(group_count);
    if (group_count && GetProcessGroupAffinity(GetCurrentProcess(), &group_count, groups.data())) {
        uint64_t permitted{0};
        for (USHORT i{0}; i < group_count; ++i) permitted += GetActiveProcessorCount(groups[i]);
        if (permitted) cpus = static_cast<unsigned int>(std::min<uint64_t>(cpus, permitted));
        // A process affinity mask is meaningful only within one processor group.
        DWORD_PTR process_mask{0}, system_mask{0};
        if (group_count == 1 && GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) && process_mask)
            cpus = std::min(cpus, static_cast<unsigned int>(std::popcount(process_mask)));
    }
    ULONG set_count{0};
    GetProcessDefaultCpuSets(GetCurrentProcess(), nullptr, 0, &set_count);
    if (set_count) cpus = std::min(cpus, static_cast<unsigned int>(set_count));
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate{};
    if (QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation, &rate, sizeof(rate), nullptr) &&
        (rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE)) {
        // MinRate/MaxRate occupy the low/high WORD of the CpuRate union. Some
        // MinGW SDKs omit the named WORD fields while preserving that ABI.
        const DWORD quota{rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_MIN_MAX_RATE ? rate.CpuRate >> 16 :
            rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP ? rate.CpuRate : 0};
        if (quota && count) cpus = static_cast<unsigned int>(std::min<uint64_t>(cpus, std::max<uint64_t>(1, uint64_t{count} * quota / 10000)));
    }
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) memory = status.ullAvailPhys;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job{};
    PROCESS_MEMORY_COUNTERS_EX process{};
    process.cb = sizeof(process);
    if (QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &job, sizeof(job), nullptr) &&
        (job.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) &&
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process), sizeof(process))) {
        const uint64_t remaining{job.ProcessMemoryLimit > process.PrivateUsage ? job.ProcessMemoryLimit - process.PrivateUsage : 0};
        memory = memory ? std::min(*memory, remaining) : remaining;
    }
#elif defined(__APPLE__)
    int count{0};
    size_t size{sizeof(count)};
    if (sysctlbyname("hw.logicalcpu", &count, &size, nullptr, 0) == 0 && count > 0) cpus = count;
    vm_statistics64_data_t stats{};
    mach_msg_type_number_t length{HOST_VM_INFO64_COUNT};
    vm_size_t page_size{0};
    const auto host{mach_host_self()};
    if (host_page_size(host, &page_size) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &length) == KERN_SUCCESS) {
        memory = (uint64_t{stats.free_count} + stats.inactive_count) * page_size;
    }
    mach_port_deallocate(mach_task_self(), host);
#elif defined(__linux__)
    std::optional<unsigned int> affinity_cpus;
    // CPU_SETSIZE is only the initial buffer size, never a worker ceiling.
    size_t capacity{std::max<size_t>(cpus, CPU_SETSIZE)};
    while (capacity <= static_cast<size_t>(std::numeric_limits<int>::max())) {
        cpu_set_t* affinity{CPU_ALLOC(capacity)};
        if (!affinity) break;
        const auto bytes{CPU_ALLOC_SIZE(capacity)};
        CPU_ZERO_S(bytes, affinity);
        const int result{sched_getaffinity(0, bytes, affinity)};
        const int error{errno};
        const int count{result == 0 ? CPU_COUNT_S(bytes, affinity) : 0};
        CPU_FREE(affinity);
        if (count > 0) affinity_cpus = count;
        if (result == 0 || error != EINVAL) break;
        capacity *= 2;
    }
    return ReadLinuxWorkerLimits(cpus, affinity_cpus, [](const std::string& path) {
        std::ifstream file{path};
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    });
#endif
    return CalculateWorkerLimits(cpus, memory);
}
} // namespace mining
