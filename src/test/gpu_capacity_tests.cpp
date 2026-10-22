// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <mining/gpu_backend.h>
#include <test/util/setup_common.h>
#include <boost/test/unit_test.hpp>
#include <climits>
#include <limits>

BOOST_FIXTURE_TEST_SUITE(gpu_capacity_tests, BasicTestingSetup)
BOOST_AUTO_TEST_CASE(resource_capacity_without_a_v2_scheduler_ceiling)
{
    using namespace mining;
    using mercahash::Version;
    for (const unsigned n : {1U, 128U, 192U, 255U, 256U, 257U, 325U, 512U, 1024U, 8193U, 65537U}) {
        const uint64_t allocation{uint64_t{n} * mercahash::V2_SCRATCHPAD_BYTES};
        const uint64_t memory{2 * (allocation + (576ULL << 20) + uint64_t{n} * 4096)};
        const auto budget{CalculateGpuBudget(memory, allocation, 0, GpuMemorySource::CUDA_FREE, Version::V2)};
        BOOST_CHECK_EQUAL(budget.maximum_contexts, n);
        BOOST_CHECK_EQUAL(CalculateGpuBudget(memory, allocation, n, GpuMemorySource::CUDA_FREE, Version::V2).contexts, n);
        BOOST_CHECK(!budget.scheduler_limited);
        BOOST_CHECK(budget.headroom_bytes >= budget.reserved_bytes);
        BOOST_CHECK_EQUAL(budget.scratchpad_bytes, allocation);
        BOOST_CHECK_THROW(CalculateGpuBudget(memory, allocation, n + 1, GpuMemorySource::CUDA_FREE, Version::V2), std::runtime_error);
    }
    BOOST_CHECK_EQUAL(CalculateGpuBudget(UINT64_MAX, UINT64_MAX).contexts, MAX_GPU_CONTEXTS);
    const auto huge{CalculateGpuBudget(UINT64_MAX, UINT64_MAX, 0, GpuMemorySource::CUDA_FREE, Version::V2)};
    BOOST_CHECK_EQUAL(huge.contexts, std::numeric_limits<unsigned int>::max());
    BOOST_CHECK(huge.scratchpad_bytes + huge.overhead_bytes < UINT64_MAX);
    GpuDevice amd{GpuVendor::AMD_OPENCL};
    amd.pow_version = Version::V2;
    amd.total_memory = 64ULL << 30; amd.available_memory = 8ULL << 30; amd.max_allocation = 32ULL << 30;
    amd.available_memory_known = true;
    BOOST_CHECK_EQUAL(CalculateGpuBudget(amd).contexts, CalculateGpuBudget(8ULL << 30, 32ULL << 30, 0, GpuMemorySource::OPENCL_FREE, Version::V2).contexts);
    amd.available_memory = 0;
    BOOST_CHECK_EQUAL(CalculateGpuBudget(amd).contexts, 0U);
    amd.pow_version = Version::V1;
    BOOST_CHECK(CalculateGpuBudget(amd).contexts > 0); // V1 retains its total-pool policy.
}
BOOST_AUTO_TEST_CASE(dynamic_allocation_fallback_and_driver_geometry)
{
    using namespace mining;
    using mercahash::Version;
    for (const unsigned threshold : {1U, 128U, 192U, 255U, 256U, 257U, 325U, 512U, 1024U, 65537U}) {
        unsigned live{0}, attempts{0};
        const auto budget = [] { GpuMemoryBudget b; b.contexts = b.maximum_contexts = 100003; return b; };
        const auto allocate = [&](unsigned n, const GpuMemoryBudget&) {
            BOOST_CHECK_EQUAL(live, 0U); ++attempts; live = n;
            if (n > threshold) throw GpuMemoryAllocationError{"simulated device allocation limit"};
        };
        const auto result{AllocateGpuContexts(0, budget, allocate, [&] { live = 0; }, [] { return false; }, Version::V2)};
        BOOST_CHECK_EQUAL(result.estimated, 100003U);
        BOOST_CHECK_EQUAL(result.active, threshold);
        BOOST_CHECK_EQUAL(live, threshold);
        BOOST_CHECK_LE(attempts, 66U); // Binary search in the count type, not a context ceiling.
        live = 0;
        BOOST_CHECK_THROW(AllocateGpuContexts(threshold + 1, budget, allocate, [&] { live = 0; }, [] { return false; }, Version::V2), GpuMemoryAllocationError);
        BOOST_CHECK_EQUAL(live, 0U);
    }
    BOOST_CHECK_EQUAL(GpuExecutionContexts(8193, 325, 4096), 325U);
    BOOST_CHECK_EQUAL(GpuExecutionContexts(8193, 1024, 1000), 1000U);
    BOOST_CHECK_EQUAL(GpuExecutionContexts(UINT_MAX, UINT64_MAX, UINT64_MAX), UINT_MAX / 80);
    BOOST_CHECK_THROW(GpuExecutionContexts(1, 0, 1), std::runtime_error);
    BOOST_CHECK_THROW(GpuExecutionContexts(1, 1, 0), std::runtime_error);
}
BOOST_AUTO_TEST_SUITE_END()
