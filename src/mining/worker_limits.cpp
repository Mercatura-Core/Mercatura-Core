// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <mining/cpu_miner.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#ifdef WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sched.h>
#endif

namespace mining {
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
    const unsigned int cpu_reserve{std::max(1U, limits.logical_cpus / 8)};
    const unsigned int cpu_workers{limits.logical_cpus > cpu_reserve ? limits.logical_cpus - cpu_reserve : 1};
    limits.maximum = static_cast<unsigned int>(std::min<uint64_t>(cpu_workers, memory_workers));
    limits.recommended = std::min(limits.maximum, std::max(1U, limits.logical_cpus / 2));
    return limits;
}

WorkerLimits DetectWorkerLimits()
{
    unsigned int cpus{std::max(1U, std::thread::hardware_concurrency())};
    std::optional<uint64_t> memory;
#ifdef WIN32
    const DWORD count{GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)};
    if (count) cpus = count;
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) memory = status.ullAvailPhys;
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
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0 && CPU_COUNT(&affinity) > 0) cpus = CPU_COUNT(&affinity);
    std::ifstream meminfo{"/proc/meminfo"};
    std::string line;
    while (std::getline(meminfo, line)) {
        std::istringstream fields{line};
        std::string key;
        uint64_t kib;
        if (fields >> key >> kib && key == "MemAvailable:" && kib <= std::numeric_limits<uint64_t>::max() / 1024) {
            memory = kib * 1024;
            break;
        }
    }
    // cgroup v2 can be mounted at the process group or an ancestor. Include
    // ancestor limits as well as host MemAvailable; never use swap as a budget.
    std::string group;
    std::ifstream cgroups{"/proc/self/cgroup"};
    while (std::getline(cgroups, line)) {
        if (line.starts_with("0::")) group = line.substr(3);
    }
    if (group.find("..") != std::string::npos) group.clear();
    std::string path{"/sys/fs/cgroup" + group};
    while (true) {
        uint64_t maximum, used;
        std::ifstream max_file{path + "/memory.max"}, current_file{path + "/memory.current"};
        if (max_file >> maximum && current_file >> used) {
            const uint64_t remaining{maximum > used ? maximum - used : 0};
            memory = memory ? std::min(*memory, remaining) : remaining;
        }
        uint64_t quota, period;
        std::ifstream cpu_file{path + "/cpu.max"};
        if (cpu_file >> quota >> period && period > 0) {
            cpus = static_cast<unsigned int>(std::min<uint64_t>(cpus, std::max<uint64_t>(1, quota / period)));
        }
        if (path == "/sys/fs/cgroup") break;
        const auto slash{path.find_last_of('/')};
        if (slash < std::string{"/sys/fs/cgroup"}.size()) path = "/sys/fs/cgroup";
        else path.resize(slash);
    }
#endif
    return CalculateWorkerLimits(cpus, memory);
}
} // namespace mining
