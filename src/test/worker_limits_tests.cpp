// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <mining/cpu_miner.h>

#include <boost/test/unit_test.hpp>

#include <limits>
#include <map>
#include <string>

namespace {
struct LinuxResources {
    std::map<std::string, std::string> files{{"/proc/meminfo", "MemAvailable: 268435456 kB\n"}};
    mining::WorkerLimits Limits(unsigned int cpus = 192, std::optional<unsigned int> affinity = {}) const
    {
        return mining::ReadLinuxWorkerLimits(cpus, affinity, [this](const auto& path) {
            const auto it{files.find(path)};
            return it == files.end() ? std::string{} : it->second;
        });
    }
};
} // namespace

BOOST_AUTO_TEST_SUITE(worker_limits_tests)

BOOST_AUTO_TEST_CASE(logical_processor_scaling_without_fixed_ceiling)
{
    // No threads or scratchpads are allocated for these simulated machines.
    for (const unsigned int cpus : {8U, 12U, 16U, 32U, 64U, 192U}) {
        BOOST_TEST_CONTEXT("logical CPUs = " << cpus) {
            const auto limits{mining::CalculateWorkerLimits(cpus, uint64_t{256} << 30)};
            BOOST_CHECK_EQUAL(limits.logical_cpus, cpus);
            BOOST_CHECK_EQUAL(limits.maximum, cpus);
            BOOST_CHECK_EQUAL(mining::MaximumCpuWorkers(limits, 2), cpus);
        }
    }
    const auto extreme{mining::CalculateWorkerLimits(std::numeric_limits<unsigned int>::max(), std::numeric_limits<uint64_t>::max())};
    BOOST_CHECK_EQUAL(extreme.maximum, std::numeric_limits<unsigned int>::max());
    BOOST_CHECK_EQUAL(mining::MaximumCpuWorkers(extreme, 1), extreme.maximum - 1);
    BOOST_CHECK_EQUAL(mining::MaximumCpuWorkers(extreme, std::numeric_limits<size_t>::max()), 0U);
}

BOOST_AUTO_TEST_CASE(memory_reserve_and_gpu_verifier_boundaries)
{
    using mining::CalculateWorkerLimits;
    constexpr uint64_t reserve{512ULL << 20};
    BOOST_CHECK_EQUAL(CalculateWorkerLimits(192, {}).maximum, 1U);
    BOOST_CHECK_EQUAL(CalculateWorkerLimits(192, 0).maximum, 0U);
    BOOST_CHECK_EQUAL(CalculateWorkerLimits(192, reserve + mining::WORKER_MEMORY_BYTES - 1).maximum, 0U);
    BOOST_CHECK_EQUAL(CalculateWorkerLimits(192, reserve + mining::WORKER_MEMORY_BYTES).maximum, 1U);
    const auto limited{CalculateWorkerLimits(192, uint64_t{1} << 30)};
    BOOST_CHECK_EQUAL(limited.maximum, 3U);
    BOOST_CHECK_EQUAL(mining::MaximumCpuWorkers(limited, 1), 2U);
    BOOST_CHECK_EQUAL(mining::MaximumCpuWorkers(limited, 2), 1U);
    BOOST_CHECK_EQUAL(mining::MaximumCpuWorkers(limited, 3), 0U);
    BOOST_CHECK_EQUAL(CalculateWorkerLimits(192, uint64_t{8} << 30).maximum, 38U);
}

BOOST_AUTO_TEST_CASE(linux_affinity_cpuset_and_fractional_quota)
{
    LinuxResources resources;
    resources.files["/proc/self/cgroup"] = "0::/tenant/session\n";
    resources.files["/proc/self/mountinfo"] = "1 0 0:1 / /sys/fs/cgroup ro - cgroup2 cgroup rw\n";
    resources.files["/sys/fs/cgroup/tenant/session/cpu.max"] = "max 100000\n";
    resources.files["/sys/fs/cgroup/tenant/session/cpuset.cpus.effective"] = "0-7,4096-4099\n";
    BOOST_CHECK_EQUAL(resources.Limits().maximum, 12U);
    BOOST_CHECK_EQUAL(resources.Limits(192, 4).maximum, 4U);
    resources.files["/proc/self/status"] = "Cpus_allowed_list:\t4096-4097\n";
    BOOST_CHECK_EQUAL(resources.Limits().maximum, 2U);
    resources.files.erase("/proc/self/status");
    resources.files["/sys/fs/cgroup/tenant/cpu.max"] = "350000 100000\n";
    BOOST_CHECK_EQUAL(resources.Limits().maximum, 3U);
    resources.files["/sys/fs/cgroup/tenant/cpu.max"] = "50000 100000\n";
    BOOST_CHECK_EQUAL(resources.Limits().maximum, 1U);
}

BOOST_AUTO_TEST_CASE(linux_cgroup_v2_bind_mount_and_ancestor_memory)
{
    LinuxResources resources;
    resources.files["/proc/self/cgroup"] = "0::/tenant/session\n";
    resources.files["/proc/self/mountinfo"] = "1 0 0:1 /tenant /custom/cgroup\\040mount ro - cgroup2 cgroup rw\n";
    resources.files["/custom/cgroup mount/session/memory.max"] = "max\n";
    resources.files["/custom/cgroup mount/memory.max"] = "2147483648\n";
    resources.files["/custom/cgroup mount/memory.current"] = "1073741824\n";
    resources.files["/custom/cgroup mount/cpu.max"] = "800000 100000\n";
    const auto limited{resources.Limits()};
    BOOST_REQUIRE(limited.available_memory);
    BOOST_CHECK_EQUAL(*limited.available_memory, uint64_t{1} << 30);
    BOOST_CHECK_EQUAL(limited.logical_cpus, 8U);
    BOOST_CHECK_EQUAL(limited.maximum, 3U);
    resources.files["/custom/cgroup mount/memory.current"] = "2147483649\n";
    BOOST_CHECK_EQUAL(resources.Limits().maximum, 0U);
}

BOOST_AUTO_TEST_CASE(linux_namespaced_cgroup_root)
{
    LinuxResources resources;
    resources.files["/proc/self/cgroup"] = "0::/\n";
    resources.files["/proc/self/mountinfo"] = "1 0 0:1 /host/container /sys/fs/cgroup ro - cgroup2 cgroup rw\n";
    resources.files["/sys/fs/cgroup/cpu.max"] = "200000 100000\n";
    BOOST_CHECK_EQUAL(resources.Limits(192, 3).maximum, 2U);
    // Some cloud namespaces report /.. as the underlying mount root.
    resources.files["/proc/self/mountinfo"] = "1 0 0:1 /.. /sys/fs/cgroup ro - cgroup2 cgroup rw\n";
    BOOST_CHECK_EQUAL(resources.Limits(192, 3).maximum, 2U);
}

BOOST_AUTO_TEST_CASE(linux_cgroup_v1_separate_and_comounted_controllers)
{
    LinuxResources resources;
    resources.files["/proc/self/cgroup"] = "2:cpu,cpuacct:/team/session\n3:memory:/team/session\n4:cpuset:/team/session\n";
    resources.files["/proc/self/mountinfo"] =
        "1 0 0:1 / /cg/cpu rw - cgroup cgroup rw,cpu,cpuacct\n"
        "2 0 0:2 / /cg/memory rw - cgroup cgroup rw,memory\n"
        "3 0 0:3 / /cg/cpuset rw - cgroup cgroup rw,cpuset\n";
    resources.files["/cg/cpu/team/session/cpu.cfs_quota_us"] = "-1\n";
    resources.files["/cg/cpu/team/cpu.cfs_quota_us"] = "600000\n";
    resources.files["/cg/cpu/team/cpu.cfs_period_us"] = "100000\n";
    resources.files["/cg/cpuset/team/cpuset.cpus"] = "4-7\n";
    resources.files["/cg/memory/team/memory.limit_in_bytes"] = "2147483648\n";
    resources.files["/cg/memory/team/memory.usage_in_bytes"] = "1073741824\n";
    BOOST_CHECK_EQUAL(resources.Limits().logical_cpus, 4U);
    BOOST_CHECK_EQUAL(resources.Limits().maximum, 3U);
}

BOOST_AUTO_TEST_CASE(linux_process_address_space_and_malformed_resource_files)
{
    LinuxResources resources;
    resources.files["/proc/self/status"] = "VmSize: 1048576 kB\nCpus_allowed_list: 0-4294967295\n";
    resources.files["/proc/self/limits"] = "Max address space         2147483648 2147483648 bytes\n";
    BOOST_CHECK_EQUAL(resources.Limits().maximum, 3U);
    resources.files["/proc/self/limits"] = "Max address space         unlimited unlimited bytes\n";
    BOOST_CHECK_EQUAL(resources.Limits().maximum, 192U);
    resources.files["/proc/self/status"] = "Cpus_allowed_list: 9-2\n";
    resources.files["/proc/meminfo"] = "MemAvailable: 18446744073709551615 kB\n";
    resources.files["/proc/self/cgroup"] = "0::/../../outside\n";
    resources.files["/proc/self/mountinfo"] = "malformed\n";
    BOOST_CHECK(!resources.Limits().available_memory);
    BOOST_CHECK_EQUAL(resources.Limits().maximum, 1U);
}

BOOST_AUTO_TEST_SUITE_END()
