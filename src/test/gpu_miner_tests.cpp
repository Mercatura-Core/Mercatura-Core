// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <algorithm>
#include <array>
#include <boost/test/unit_test.hpp>
#include <mining/cpu_miner.h>
#include <set>
#include <streams.h>
#include <test/util/setup_common.h>
#include <thread>
#include <util/strencodings.h>
namespace
{
bool Until(const std::function<bool()> &condition)
{
    const auto end{std::chrono::steady_clock::now() + std::chrono::seconds{10}};
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= end)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return true;
}
struct State {
    std::mutex mutex;
    std::set<std::pair<int32_t, uint32_t>> nonces;
    std::set<int32_t> submitted_parents;
    bool duplicate{false};
    bool duplicate_solo_submission{false}, stale_submission{false};
    std::atomic<int32_t> parent{7};
    std::atomic<int32_t> retired_parent{-1};
    std::atomic<unsigned int> submitted{0}, gpu_batches{0}, blocked_hashes{0}, late_batches{0}, authoritative_checks{0};
    std::atomic<bool> advance_on_submission{true};
    std::atomic<bool> revoked{false}, failing{false}, bad_candidate{false}, target_hits{false}, bad_self_test{false},
        block_batches{false}, entered{false}, release{true}, incomplete{false};
    void Record(const CBlockHeader &header)
    {
        std::lock_guard lock{mutex};
        if (!nonces.emplace(header.nVersion, header.nNonce).second)
            duplicate = true;
    }
};
class Provider : public mining::WorkProvider
{
    std::shared_ptr<State> state;

  public:
    explicit Provider(std::shared_ptr<State> value) : state{std::move(value)} {}
    std::optional<mining::MiningJob> GetJob() override
    {
        if (state->retired_parent == state->parent)
            return std::nullopt;
        mining::MiningJob job;
        job.header.nVersion = state->parent;
        job.target = state->target_hits ? ~arith_uint256{} : arith_uint256{1};
        return job;
    }
    bool IsCurrent(const mining::MiningJob &job) override
    {
        return !state->revoked && job.header.nVersion == state->parent;
    }
    mining::SubmissionResult Submit(const mining::MiningJob &job, const CBlockHeader &header) override
    {
        BOOST_CHECK_EQUAL(job.header.nVersion, header.nVersion);
        {
            std::lock_guard lock{state->mutex};
            const bool first{state->submitted_parents.emplace(job.header.nVersion).second};
            if (!first)
                state->duplicate_solo_submission = true;
            if (state->revoked || job.header.nVersion != state->parent)
                state->stale_submission = true;
        }
        state->retired_parent = job.header.nVersion;
        if (state->advance_on_submission) ++state->parent;
        ++state->submitted;
        return {};
    }
    void Interrupt() override
    {
        state->revoked = true;
    }
};
// Explicit controller mock. Returns frozen vectors for initialization, and
// synthetic hashes for scheduling tests. This never proves physical GPU parity.
class MockBackend : public mining::GpuBackend
{
    std::shared_ptr<State> state;
    bool failing;
    bool delay_solo_batch;
    unsigned int context_count, estimated_count;

  public:
    MockBackend(std::shared_ptr<State> value, bool fail, bool delay = false, unsigned int contexts = 2, unsigned int estimated = 0)
        : state{std::move(value)}, failing{fail}, delay_solo_batch{delay}, context_count{contexts}, estimated_count{estimated ? estimated : contexts}
    {
    }
    unsigned int Contexts() const override
    {
        return context_count;
    }
    unsigned int EstimatedContexts() const override { return estimated_count; }
    uint64_t MemoryBytes() const override
    {
        return context_count * (128ULL << 20);
    }
    std::vector<mining::GpuHash> Hash(std::span<const mining::GpuHeader> headers,
                                      const std::function<bool()> &cancelled) override
    {
        if (cancelled())
            return {};
        constexpr const char *vectors[]{"cc97e7cbce1d4ed2b54c9c37a81a000383229341a277a44829484855b9ef79dc",
                                        "2321712af21502878986c0c4f21d79e17e2281619e3958f11e3c634c9f17f7d8",
                                        "9c9a427928875304bb7d6351354b5e50ec8e57c581971f68eab176371c4cd2c2",
                                        "1244c6c9e0365baeb5958cfaa57aff582d81ae1d757769e823085bb00676e170"};
        std::vector<mining::GpuHash> hashes;
        bool mining_batch{false};
        for (const auto &bytes : headers) {
            int vector{-1};
            for (unsigned pattern{0}; pattern < 4; ++pattern) {
                bool match{true};
                for (unsigned i{0}; i < 80; ++i)
                    if (bytes[i] != (pattern == 0 ? 0 : pattern == 1 ? i : pattern == 2 ? 255 : (i * 37 + 11) & 255))
                        match = false;
                if (match)
                    vector = pattern;
            }
            mining::GpuHash result{};
            if (vector >= 0) {
                const auto expected{ParseHex(vectors[vector])};
                std::copy(expected.begin(), expected.end(), result.begin());
                if (state->bad_self_test)
                    result[0] ^= 1;
            } else {
                mining_batch = true;
                if (state->block_batches) {
                    state->entered = true;
                    ++state->blocked_hashes;
                    while (!state->release)
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
                if (failing && state->failing)
                    throw std::runtime_error{"simulated device loss"};
                CBlockHeader header;
                SpanReader reader{std::span<const unsigned char>{bytes}};
                reader >> header;
                state->Record(header);
                ++state->gpu_batches;
                if (!state->target_hits)
                    result.fill(0xff);
                if (state->bad_candidate)
                    result[0] = 1;
                std::this_thread::sleep_for(std::chrono::microseconds{200});
            }
            hashes.push_back(result);
        }
        if (delay_solo_batch && mining_batch) {
            // Deliberately deliver a completed device result after another
            // worker solves this Solo job. The controller must discard it.
            while (!cancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            ++state->late_batches;
            return hashes;
        }
        if (mining_batch && state->incomplete && !hashes.empty()) hashes.pop_back();
        return cancelled() ? std::vector<mining::GpuHash>{} : hashes;
    }
};
mining::GpuDiscovery Devices()
{
    mining::GpuDiscovery result;
    for (unsigned i{0}; i < 2; ++i) {
        mining::GpuDevice device{i == 0 ? mining::GpuVendor::NVIDIA_CUDA : mining::GpuVendor::AMD_OPENCL};
        device.id = i ? "opencl:0" : "cuda:0";
        device.name = i ? "Simulated AMD" : "Simulated NVIDIA";
        device.total_memory = 4ULL << 30;
        device.max_allocation = 2ULL << 30;
        device.supported = true;
        result.devices.push_back(device);
    }
    return result;
}
mining::MiningController::HashFactory Hashes(std::shared_ptr<State> state)
{
    return [state] {
        return [state](const CBlockHeader &header) {
            if (state->target_hits) {
                ++state->authoritative_checks;
                return uint256{}; // GPU verification seam.
            }
            state->Record(header);
            std::this_thread::sleep_for(std::chrono::microseconds{200});
            return uint256::FromHex(std::string(64, 'f')).value();
        };
    };
}
auto Limits()
{
    return mining::CalculateWorkerLimits(8, 16ULL << 30);
}
} // namespace
BOOST_FIXTURE_TEST_SUITE(gpu_miner_tests, BasicTestingSetup)
BOOST_AUTO_TEST_CASE(exhausted_nonce_allocator_never_wraps)
{
    std::atomic<uint64_t> cursor{UINT64_MAX};
    BOOST_CHECK(!mining::AllocateNonceRange(cursor));
    BOOST_CHECK_EQUAL(cursor, UINT64_MAX);
    cursor = mining::NONCE_SPACE - 1;
    const auto final{mining::AllocateNonceRange(cursor)};
    BOOST_REQUIRE(final);
    BOOST_CHECK_EQUAL(final->end, mining::NONCE_SPACE);
    for (unsigned i{0}; i < 1000; ++i)
        BOOST_CHECK(!mining::AllocateNonceRange(cursor));
    BOOST_CHECK_EQUAL(cursor, mining::NONCE_SPACE);
}
BOOST_AUTO_TEST_CASE(memory_budget_limits_and_overflow)
{
    using mining::CalculateGpuBudget;
    BOOST_CHECK_EQUAL(CalculateGpuBudget(512ULL << 20, 512ULL << 20).contexts, 0U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(2ULL << 30, 1ULL << 30).contexts, 8U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(8ULL << 30, 8ULL << 30).contexts, 59U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(6ULL << 30, 6ULL << 30).contexts, 43U);
    // Manual may reduce the automatic count, e.g. to reproduce a hardware tuning choice.
    const auto manual{CalculateGpuBudget(6ULL << 30, 6ULL << 30, 36)};
    BOOST_CHECK_EQUAL(manual.contexts, 36U);
    BOOST_CHECK_EQUAL(manual.scratchpad_bytes, 4608ULL << 20);
    BOOST_CHECK(manual.reserved_bytes >= (512ULL << 20));
    BOOST_CHECK(manual.overhead_bytes > 36 * 240U);
    BOOST_CHECK(manual.headroom_bytes >= manual.reserved_bytes);
    BOOST_CHECK(!manual.low_headroom);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(UINT64_MAX, UINT64_MAX).contexts, mining::MAX_GPU_CONTEXTS);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(UINT64_MAX, UINT64_MAX, 256).scratchpad_bytes, 32ULL << 30);
    BOOST_CHECK_THROW(CalculateGpuBudget(UINT64_MAX, UINT64_MAX, 257), std::runtime_error);
    BOOST_CHECK_THROW(CalculateGpuBudget(1ULL << 30, 1ULL << 30, UINT32_MAX), std::runtime_error);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(4ULL << 30, 64ULL << 20).contexts, 0U);
}
BOOST_AUTO_TEST_CASE(manual_gpu_budget_boundaries_and_backend_reporting)
{
    using mining::CalculateGpuBudget;
    using mining::GpuMemorySource;
    constexpr uint64_t minimum{512ULL << 20}, overhead{64ULL << 20};
    constexpr uint64_t per_context{(128ULL << 20) + 4096};
    const uint64_t exact{minimum + overhead + 36 * per_context};
    const auto tight{CalculateGpuBudget(exact, exact, 36)};
    BOOST_CHECK_EQUAL(tight.headroom_bytes, minimum);
    BOOST_CHECK(tight.low_headroom);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(exact, exact).contexts, 36U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(exact - 1, exact).contexts, 35U);
    BOOST_CHECK_THROW(CalculateGpuBudget(exact - 1, exact, 36), std::runtime_error);
    BOOST_CHECK_THROW(CalculateGpuBudget(5ULL << 30, 6ULL << 30, 36), std::runtime_error);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(12ULL << 30, 12ULL << 30, 72).contexts, 72U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(24ULL << 30, 24ULL << 30, 128).contexts, 128U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(12ULL << 30, 12ULL << 30).contexts, 90U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(24ULL << 30, 24ULL << 30).contexts, 181U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(24ULL << 30, 24ULL << 30).reserved_bytes, (24ULL << 30) / 20);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(80ULL << 30, 80ULL << 30).maximum_contexts, mining::MAX_GPU_CONTEXTS);
    BOOST_CHECK(CalculateGpuBudget(80ULL << 30, 80ULL << 30).scheduler_limited);
    BOOST_CHECK(!CalculateGpuBudget(24ULL << 30, 24ULL << 30).scheduler_limited);
    BOOST_CHECK(!CalculateGpuBudget(80ULL << 30, 1ULL << 30).scheduler_limited); // Allocation, not scheduler, binds.
    BOOST_CHECK_EQUAL(CalculateGpuBudget(24ULL << 30, 1ULL << 30).maximum_contexts, 8U);
    BOOST_CHECK_THROW(CalculateGpuBudget(24ULL << 30, 1ULL << 30, 9), std::runtime_error);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(0, UINT64_MAX).maximum_contexts, 0U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(UINT64_MAX, 0).maximum_contexts, 0U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(minimum + overhead + per_context - 1, UINT64_MAX).maximum_contexts, 0U);
    // OpenCL total memory must never be treated as measured free CUDA memory.
    BOOST_CHECK_THROW(CalculateGpuBudget(6ULL << 30, 6ULL << 30, 36, GpuMemorySource::OPENCL_TOTAL), std::runtime_error);
    mining::GpuDevice amd{mining::GpuVendor::AMD_OPENCL};
    amd.total_memory = 6ULL << 30;
    amd.available_memory = UINT64_MAX; // Irrelevant without a portable free-VRAM query.
    amd.max_allocation = 6ULL << 30;
    BOOST_CHECK_EQUAL(CalculateGpuBudget(amd).contexts, 11U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(amd, 11).reserved_bytes, 9ULL << 29);
    amd.vendor = mining::GpuVendor::NVIDIA_CUDA;
    amd.available_memory = 0;
    BOOST_CHECK_EQUAL(CalculateGpuBudget(amd).maximum_contexts, 0U); // No fallback to total VRAM.
}
BOOST_AUTO_TEST_CASE(automatic_allocation_search_cleanup_and_errors)
{
    // Every threshold exercises the bounded search, including 0 (no viable
    // allocation), 1 and the scheduler maximum. No SDK/hardware is required.
    for (unsigned int threshold{0}; threshold <= mining::MAX_GPU_CONTEXTS; ++threshold) {
        unsigned int live{0}, attempts{0}, queries{0}, cleanups{0};
        auto budget = [&] {
            BOOST_CHECK_EQUAL(live, 0U);
            ++queries;
            mining::GpuMemoryBudget result;
            result.contexts = mining::MAX_GPU_CONTEXTS;
            return result;
        };
        auto allocate = [&](unsigned count, const auto&) {
            BOOST_CHECK_EQUAL(live, 0U);
            live = count; // Even a failed attempt owns partial resources.
            ++attempts;
            if (count > threshold) throw mining::GpuMemoryAllocationError{"device memory exhausted"};
        };
        auto cleanup = [&] { live = 0; ++cleanups; };
        if (!threshold) {
            BOOST_CHECK_THROW(mining::AllocateGpuContexts(0, budget, allocate, cleanup, {}), mining::GpuMemoryAllocationError);
            BOOST_CHECK_EQUAL(live, 0U);
        } else {
            const auto result{mining::AllocateGpuContexts(0, budget, allocate, cleanup, {})};
            BOOST_CHECK_EQUAL(result.estimated, mining::MAX_GPU_CONTEXTS);
            BOOST_CHECK_EQUAL(result.active, threshold);
            BOOST_CHECK_EQUAL(live, threshold);
            BOOST_CHECK_EQUAL(cleanups + 1, attempts);
        }
        BOOST_CHECK_LE(attempts, 20U);
        BOOST_CHECK_EQUAL(queries, attempts);
        if (threshold == mining::MAX_GPU_CONTEXTS) BOOST_CHECK_EQUAL(attempts, 1U);
    }
    for (const bool memory_error : {false, true}) {
        unsigned attempts{0}, live{0};
        auto budget = [] { mining::GpuMemoryBudget result; result.contexts = 43; return result; };
        auto allocate = [&](unsigned count, const auto&) {
            live = count; ++attempts;
            if (memory_error) throw mining::GpuMemoryAllocationError{"out of VRAM"};
            throw std::runtime_error{"driver failure"};
        };
        auto cleanup = [&] { live = 0; };
        // Manual gets one exact attempt, with no silent reduction.
        BOOST_CHECK_THROW(mining::AllocateGpuContexts(36, budget, allocate, cleanup, {}), std::runtime_error);
        BOOST_CHECK_EQUAL(attempts, 1U);
        BOOST_CHECK_EQUAL(live, 0U);
        if (!memory_error) {
            attempts = 0;
            BOOST_CHECK_THROW(mining::AllocateGpuContexts(0, budget, allocate, cleanup, {}), std::runtime_error);
            BOOST_CHECK_EQUAL(attempts, 1U);
        }
    }
}
BOOST_AUTO_TEST_CASE(allocation_cleanup_error_aborts_fallback)
{
    for (const bool allocation_failed : {false, true}) {
        unsigned int attempts{0}, cleanups{0};
        auto budget = [] { mining::GpuMemoryBudget result; result.contexts = 43; return result; };
        // First fail forces a smaller probe; that probe succeeds unless the
        // allocation itself fails again. Neither cleanup error may be retried.
        auto allocate = [&](unsigned int, const auto&) {
            ++attempts;
            if (attempts == 1 || allocation_failed) throw mining::GpuMemoryAllocationError{"out of VRAM"};
        };
        auto cleanup = [&] {
            if (++cleanups == 2) throw std::runtime_error{"driver failed to release buffers"};
        };
        BOOST_CHECK_THROW(mining::AllocateGpuContexts(0, budget, allocate, cleanup, {}), std::runtime_error);
        BOOST_CHECK_EQUAL(attempts, 2U);
        BOOST_CHECK_EQUAL(cleanups, 2U);
    }
}
BOOST_AUTO_TEST_CASE(allocation_fallback_cancellation_shutdown_and_fresh_budget)
{
    for (const bool stop_after_success : {false, true}) {
        std::atomic<bool> stopping{false};
        unsigned int attempts{0}, live{0};
        auto budget = [] { mining::GpuMemoryBudget result; result.contexts = 43; return result; };
        auto allocate = [&](unsigned count, const auto&) {
            live = count; ++attempts;
            stopping = true; // Same predicate handles RequestStop and node shutdown.
            if (!stop_after_success) throw mining::GpuMemoryAllocationError{"out of VRAM"};
        };
        BOOST_CHECK_THROW(mining::AllocateGpuContexts(0, budget, allocate, [&] { live = 0; },
            [&] { return stopping.load(); }), std::runtime_error);
        BOOST_CHECK_EQUAL(attempts, 1U);
        BOOST_CHECK_EQUAL(live, 0U);
    }
    unsigned queries{0}, attempts{0}, live{0};
    auto budget = [&] {
        BOOST_CHECK_EQUAL(live, 0U);
        mining::GpuMemoryBudget result;
        result.contexts = ++queries == 1 ? 43 : 7;
        return result;
    };
    const auto result{mining::AllocateGpuContexts(0, budget, [&](unsigned count, const auto&) {
        live = count;
        if (++attempts == 1) throw mining::GpuMemoryAllocationError{"memory pressure"};
        BOOST_CHECK_LE(count, 7U);
    }, [&] { live = 0; }, {})};
    BOOST_CHECK_EQUAL(result.estimated, 43U);
    BOOST_CHECK_EQUAL(result.active, 7U);
    BOOST_CHECK_EQUAL(attempts, 2U);
}
BOOST_AUTO_TEST_CASE(automatic_devices_size_independently_and_manual_reduces)
{
    for (const bool adjusted : {false, true}) for (const unsigned int requested : {0U, 4U}) {
        auto state{std::make_shared<State>()};
        auto devices = [] {
            mining::GpuDiscovery result;
            for (unsigned i{0}; i < 3; ++i) {
                mining::GpuDevice device{mining::GpuVendor::NVIDIA_CUDA};
                device.id = i == 0 ? "cuda:0" : i == 1 ? "cuda:1" : "cuda:2";
                device.total_memory = device.available_memory = device.max_allocation = (i == 0 ? 6ULL : i == 1 ? 24ULL : 80ULL) << 30;
                device.supported = true;
                result.devices.push_back(device);
            }
            return result;
        };
        mining::MiningController controller{Hashes(state), Limits, {}, devices,
            [&, state](const auto& device, unsigned contexts) {
                BOOST_CHECK_EQUAL(contexts, requested); // Automatic stays 0, never the smallest device's count.
                const auto estimated{mining::CalculateGpuBudget(device, contexts).contexts};
                return std::make_unique<MockBackend>(state, false, false, estimated - (adjusted && !requested ? 4 : 0), estimated);
            }};
        BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, {}, requested, 0}));
        const bool mined{Until([&] {
            const auto stats{controller.GetStats()};
            return stats.devices.size() == 3 && std::all_of(stats.devices.begin(), stats.devices.end(), [](const auto& device) { return device.hashes > 0; });
        })};
        const auto before_stop{controller.GetStats()};
        controller.RequestStop();
        controller.Wait();
        BOOST_CHECK(mined);
        const auto stats{controller.GetStats()};
        BOOST_REQUIRE_EQUAL(stats.devices.size(), 3U);
        BOOST_CHECK_EQUAL(stats.devices[0].contexts, requested ? requested : adjusted ? 39U : 43U);
        BOOST_CHECK_EQUAL(stats.devices[1].contexts, requested ? requested : adjusted ? 177U : 181U);
        BOOST_CHECK_EQUAL(stats.devices[2].contexts, requested ? requested : mining::MAX_GPU_CONTEXTS - (adjusted ? 4 : 0));
        BOOST_REQUIRE_EQUAL(before_stop.devices.size(), 3U);
        BOOST_CHECK_EQUAL(before_stop.devices[0].estimated_contexts, requested ? requested : 43U);
        BOOST_CHECK_EQUAL(before_stop.devices[1].estimated_contexts, requested ? requested : 181U);
        BOOST_CHECK_EQUAL(before_stop.devices[2].estimated_contexts, requested ? requested : 256U);
        BOOST_CHECK_EQUAL(before_stop.devices[0].automatic, !requested);
        if (!requested) BOOST_CHECK_EQUAL(before_stop.devices[2].memory_bytes, (256ULL - (adjusted ? 4 : 0)) * (128ULL << 20));
        BOOST_CHECK_EQUAL(stats.devices[2].memory_bytes, 0U); // Stop clears the live allocation statistic.
        BOOST_CHECK(!state->duplicate); // Different full/partial batch sizes still share non-overlapping leases.
        BOOST_CHECK_EQUAL(stats.active_workers, 0U);
    }
}
BOOST_AUTO_TEST_CASE(mixed_devices_share_cpu_nonce_allocator_and_refresh)
{
    auto state{std::make_shared<State>()};
    mining::MiningController controller{Hashes(state), Limits, {}, Devices, [state](const auto &, unsigned) {
                                            return std::make_unique<MockBackend>(state, false);
                                        }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 2, {true, true, {}, 0, 0}));
    const bool started{Until([&] { return controller.GetStats().cpu_hashes > 20 && state->gpu_batches > 20; })};
    for (int sample{0}; sample < 20; ++sample) {
        const auto live{controller.GetStats()};
        BOOST_REQUIRE_EQUAL(live.devices.size(), 2U);
        BOOST_CHECK_EQUAL(live.hashes, live.cpu_hashes + live.devices[0].hashes + live.devices[1].hashes);
    }
    ++state->parent;
    const bool refreshed{Until([&] {
        std::lock_guard lock{state->mutex};
        return state->nonces.contains({state->parent.load(), 0});
    })};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(started);
    BOOST_CHECK(refreshed);
    BOOST_CHECK(!state->duplicate);
    const auto stats{controller.GetStats()};
    BOOST_REQUIRE_EQUAL(stats.devices.size(), 2U);
    BOOST_CHECK(stats.devices[0].hashes > 0);
    BOOST_CHECK(stats.devices[1].hashes > 0);
    BOOST_CHECK_EQUAL(stats.hashes, stats.cpu_hashes + stats.devices[0].hashes + stats.devices[1].hashes);
    BOOST_CHECK_EQUAL(stats.active_workers, 0U);
    BOOST_CHECK(!stats.busy);
}

BOOST_AUTO_TEST_CASE(combined_cpu_and_gpu_host_memory_boundary)
{
    auto state{std::make_shared<State>()};
    std::atomic<unsigned int> allocations{0};
    mining::MiningController controller{Hashes(state), [] { return mining::CalculateWorkerLimits(12, uint64_t{1} << 30); }, {}, Devices,
        [&](const auto&, unsigned) { ++allocations; return std::make_unique<MockBackend>(state, false); }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 3, {true, true, {"opencl:0"}, 0, 0}));
    controller.Wait();
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::FAILED);
    BOOST_CHECK_EQUAL(allocations, 0U); // Three CPU contexts plus one verifier exceed the host budget.
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 2, {true, true, {"opencl:0"}, 0, 0}));
    const bool started{Until([&] {
        const auto stats{controller.GetStats()};
        return stats.cpu_hashes > 0 && stats.devices.size() == 1 && stats.devices.front().hashes > 0;
    })};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(started);
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::STOPPED);
}

BOOST_AUTO_TEST_CASE(gpu_only_verifies_and_submits_solo_jobs)
{
    auto state{std::make_shared<State>()};
    state->target_hits = true;
    mining::MiningController controller{Hashes(state), Limits, {}, Devices, [state](const auto &, unsigned) {
                                            return std::make_unique<MockBackend>(state, false);
                                        }};
    BOOST_REQUIRE(
        controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, {"opencl:0"}, 0, 0}));
    const bool submitted{Until([&] { return state->submitted > 5; })};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(submitted);
    BOOST_CHECK(!state->duplicate);
    BOOST_CHECK_EQUAL(controller.GetStats().cpu_hashes, 0U);
    BOOST_REQUIRE_EQUAL(controller.GetStats().devices.size(), 1U);
    BOOST_CHECK(controller.GetStats().devices.front().device.vendor == mining::GpuVendor::AMD_OPENCL);
}
BOOST_AUTO_TEST_CASE(solo_gpu_workers_submit_one_verified_candidate_and_discard_late_batches)
{
    auto state{std::make_shared<State>()};
    state->advance_on_submission = false;
    state->target_hits = true;
    state->block_batches = true;
    state->release = false;
    mining::MiningController controller{Hashes(state), Limits, {}, Devices, [state](const auto &device, unsigned) {
                                            return std::make_unique<MockBackend>(
                                                state, false, device.vendor == mining::GpuVendor::AMD_OPENCL);
                                        }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, {}, 0, 0}));
    std::array<bool, 2> competing{}, submitted{};
    for (unsigned job{0}; job < 2; ++job) {
        const auto blocked_before{job == 0 ? 0U : state->blocked_hashes.load()};
        if (job != 0) {
            state->release = false;
            ++state->parent;
        }
        // Each device blocks on its first header, so two blocked hashers prove
        // both workers hold this job before either can produce a candidate.
        competing[job] = Until([&] { return state->blocked_hashes >= blocked_before + 2; });
        state->release = true;
        submitted[job] = Until([&] {
            return state->submitted >= job + 1 && state->late_batches >= job + 1 &&
                   controller.GetStats().active_workers == 0;
        });
        if (!competing[job] || !submitted[job])
            break;
    }
    controller.RequestStop();
    state->release = true;
    controller.Wait();
    for (unsigned job{0}; job < 2; ++job) {
        BOOST_CHECK(competing[job]);
        BOOST_CHECK(submitted[job]);
    }
    BOOST_CHECK_EQUAL(state->submitted, 2U);
    BOOST_CHECK_EQUAL(state->authoritative_checks, 2U);
    BOOST_CHECK_EQUAL(state->late_batches, 2U);
    BOOST_CHECK_EQUAL(state->submitted_parents.size(), 2U);
    BOOST_CHECK(!state->duplicate);
    BOOST_CHECK(!state->duplicate_solo_submission);
    BOOST_CHECK(!state->stale_submission);
    const auto stats{controller.GetStats()};
    BOOST_CHECK_EQUAL(stats.cpu_hashes, 0U);
    BOOST_CHECK_EQUAL(stats.solutions, 2U);
    BOOST_CHECK_EQUAL(stats.submitted, 2U);
    BOOST_CHECK_EQUAL(stats.active_workers, 0U);
    BOOST_CHECK(!stats.busy);
}
BOOST_AUTO_TEST_CASE(faulty_candidate_is_never_submitted)
{
    auto state{std::make_shared<State>()};
    state->target_hits = true;
    state->bad_candidate = true;
    mining::MiningController controller{Hashes(state), Limits, {}, Devices, [state](const auto &, unsigned) {
                                            return std::make_unique<MockBackend>(state, false);
                                        }};
    BOOST_REQUIRE(
        controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, {"cuda:0"}, 0, 0}));
    const bool failed{Until([&] { return !controller.GetStats().busy; })};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(failed);
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::FAILED);
    BOOST_CHECK_EQUAL(state->submitted, 0U);
    BOOST_CHECK(controller.GetStats().devices.front().failed);
}
BOOST_AUTO_TEST_CASE(one_device_failure_preserves_other_gpu_and_cpu)
{
    auto state{std::make_shared<State>()};
    mining::MiningController controller{Hashes(state), Limits, {}, Devices, [state](const auto &device, unsigned) {
                                            return std::make_unique<MockBackend>(
                                                state, device.vendor == mining::GpuVendor::NVIDIA_CUDA);
                                        }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 1, {true, true, {}, 0, 0}));
    const bool started{Until([&] { return state->gpu_batches > 10; })};
    state->failing = true;
    const bool isolated{Until([&] {
        const auto stats{controller.GetStats()};
        return stats.devices.size() == 2 && stats.devices[0].failed;
    })};
    const auto before{controller.GetStats().devices[1].hashes};
    const bool continued{Until([&] { return controller.GetStats().devices[1].hashes > before + 5; })};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(started);
    BOOST_CHECK(isolated);
    BOOST_CHECK(continued);
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::STOPPED);
}
BOOST_AUTO_TEST_CASE(reference_mismatch_disables_device_before_mining)
{
    auto state{std::make_shared<State>()};
    state->bad_self_test = true;
    mining::MiningController controller{Hashes(state), Limits, {}, Devices, [state](const auto &, unsigned) {
                                            return std::make_unique<MockBackend>(state, false);
                                        }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, {}, 0, 0}));
    const bool failed{Until([&] { return !controller.GetStats().busy; })};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(failed);
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::FAILED);
    BOOST_CHECK_EQUAL(state->gpu_batches, 0U);
    BOOST_CHECK_EQUAL(state->submitted, 0U);
}
BOOST_AUTO_TEST_CASE(partial_gpu_initialization_failure_preserves_cpu)
{
    auto state{std::make_shared<State>()};
    mining::MiningController controller{
        Hashes(state), Limits, {}, Devices, [](const auto &, unsigned) -> std::unique_ptr<mining::GpuBackend> {
            throw std::bad_alloc{};
        }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 1, {true, true, {}, 0, 0}));
    const bool mining{Until([&] { return controller.GetStats().cpu_hashes > 10; })};
    const bool isolated{Until([&] {
        const auto stats{controller.GetStats()};
        return stats.devices.size() == 2 && stats.devices[0].failed && stats.devices[1].failed;
    })};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(mining);
    BOOST_CHECK(isolated);
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::STOPPED);
}
BOOST_AUTO_TEST_CASE(stop_is_async_during_inflight_gpu_batch_and_discards_candidates)
{
    auto state{std::make_shared<State>()};
    state->target_hits = true;
    state->block_batches = true;
    state->release = false;
    mining::MiningController controller{Hashes(state), Limits, {}, Devices, [state](const auto &, unsigned) {
                                            return std::make_unique<MockBackend>(state, false);
                                        }};
    BOOST_REQUIRE(
        controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, {"cuda:0"}, 0, 0}));
    const bool entered{Until([&] { return state->entered.load(); })};
    controller.RequestStop();
    const bool busy{controller.GetStats().busy};
    state->release = true;
    controller.Wait();
    BOOST_CHECK(entered);
    BOOST_CHECK(busy);
    BOOST_CHECK_EQUAL(state->submitted, 0U);
    BOOST_CHECK(!controller.GetStats().busy);
}
BOOST_AUTO_TEST_CASE(no_device_missing_selection_and_duplicate_selection)
{
    for (const auto &ids : std::vector<std::vector<std::string>>{{}, {"missing"}, {"cuda:0", "cuda:0"}}) {
        auto state{std::make_shared<State>()};
        mining::MiningController controller{
            Hashes(state),
            Limits,
            {},
            ids.empty() ? mining::MiningController::DeviceDetector{[] { return mining::GpuDiscovery{}; }}
                        : mining::MiningController::DeviceDetector{Devices}};
        BOOST_REQUIRE(
            controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, ids, 0, 0}));
        controller.Wait();
        BOOST_CHECK(controller.GetStats().state == mining::MiningState::FAILED);
        BOOST_CHECK_EQUAL(state->submitted, 0U);
    }
}
BOOST_AUTO_TEST_CASE(cancel_and_restart_gpu_sessions)
{
    auto state{std::make_shared<State>()};
    mining::MiningController controller{Hashes(state), Limits, {}, Devices, [state](const auto &, unsigned) {
                                            return std::make_unique<MockBackend>(state, false);
                                        }};
    for (unsigned cycle{0}; cycle < 3; ++cycle) {
        state->revoked = false;
        ++state->parent;
        const auto previous{state->gpu_batches.load()};
        BOOST_REQUIRE(
            controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, {}, 0, 10}));
        const bool started{Until([&] { return state->gpu_batches > previous + 5; })};
        controller.RequestStop();
        controller.Wait();
        BOOST_CHECK(started);
        BOOST_CHECK(!controller.GetStats().busy);
        BOOST_CHECK_EQUAL(controller.GetStats().active_workers, 0U);
    }
    BOOST_CHECK(!state->duplicate);
}
BOOST_AUTO_TEST_CASE(unknown_host_memory_bounds_gpu_authoritative_contexts)
{
    for (const bool cpu : {false, true}) {
        auto state{std::make_shared<State>()};
        std::atomic<unsigned int> allocations{0};
        mining::MiningController controller{Hashes(state), [] { return mining::CalculateWorkerLimits(8, {}); }, {}, Devices, [&](const auto&, unsigned) { ++allocations; return std::make_unique<MockBackend>(state, false); }};
        BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 1, {cpu, true, {}, 0, 0}));
        controller.Wait();
        BOOST_CHECK(controller.GetStats().state == mining::MiningState::FAILED);
        BOOST_CHECK_EQUAL(allocations, 0U);
        BOOST_CHECK_EQUAL(state->submitted, 0U);
    }
    auto state{std::make_shared<State>()};
    mining::MiningController controller{Hashes(state), [] { return mining::CalculateWorkerLimits(8, {}); }, {}, Devices, [state](const auto&, unsigned) { return std::make_unique<MockBackend>(state, false); }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, {"cuda:0"}, 0, 0}));
    const bool started{Until([&] { return state->gpu_batches > 5; })};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(started);
}
BOOST_AUTO_TEST_CASE(incomplete_device_batch_is_rejected_and_restart_recovers)
{
    auto state{std::make_shared<State>()};
    state->target_hits = true;
    state->incomplete = true;
    mining::MiningController controller{Hashes(state), Limits, {}, Devices, [state](const auto&, unsigned) { return std::make_unique<MockBackend>(state, false); }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, {"cuda:0"}, 0, 0}));
    const bool failed{Until([&] { return !controller.GetStats().busy; })};
    controller.Wait();
    BOOST_CHECK(failed);
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::FAILED);
    BOOST_CHECK_EQUAL(state->submitted, 0U);
    state->incomplete = false;
    state->revoked = false;
    ++state->parent;
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 0, {false, true, {"cuda:0"}, 0, 0}));
    const bool recovered{Until([&] { return state->submitted > 5; })};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(recovered);
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::STOPPED);
}
BOOST_AUTO_TEST_CASE(v2_gpu_memory_budgets_preserve_reserves)
{
    using mining::CalculateGpuBudget;
    using mining::GpuMemorySource;
    using mercahash::Version;
    const uint64_t exact{(512ULL << 20) + (64ULL << 20) + (16ULL << 20) + 4096};
    BOOST_CHECK_EQUAL(CalculateGpuBudget(exact - 1, exact, 0, GpuMemorySource::CUDA_FREE, Version::V2).contexts, 0U);
    const auto one{CalculateGpuBudget(exact, exact, 1, GpuMemorySource::CUDA_FREE, Version::V2)};
    BOOST_CHECK_EQUAL(one.contexts, 1U);
    BOOST_CHECK_EQUAL(one.scratchpad_bytes, 16ULL << 20);
    BOOST_CHECK_EQUAL(one.headroom_bytes, 512ULL << 20);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(4ULL << 30, 16ULL << 20, 0, GpuMemorySource::CUDA_FREE, Version::V2).contexts, 1U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(4ULL << 30, (16ULL << 20) - 1, 0, GpuMemorySource::CUDA_FREE, Version::V2).contexts, 0U);
    const auto opencl{CalculateGpuBudget(8ULL << 30, 2ULL << 30, 0, GpuMemorySource::OPENCL_TOTAL, Version::V2)};
    BOOST_CHECK(opencl.reserved_bytes >= (6ULL << 30));
    BOOST_CHECK(opencl.scratchpad_bytes + opencl.overhead_bytes <= (2ULL << 30));
    BOOST_CHECK_THROW(CalculateGpuBudget(exact, exact, 2, GpuMemorySource::CUDA_FREE, Version::V2), std::runtime_error);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(UINT64_MAX, UINT64_MAX, 256, GpuMemorySource::CUDA_FREE, Version::V2).scratchpad_bytes, 4ULL << 30);
}
BOOST_AUTO_TEST_SUITE_END()
