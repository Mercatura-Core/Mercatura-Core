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
    std::atomic<bool> continuous{true};
    std::atomic<bool> revoked{false}, failing{false}, bad_candidate{false}, target_hits{false}, bad_self_test{false},
        block_batches{false}, entered{false}, release{true};
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
        if (!state->continuous && state->retired_parent == state->parent)
            return std::nullopt;
        mining::MiningJob job;
        job.header.nVersion = state->parent;
        job.target = state->target_hits ? ~arith_uint256{} : arith_uint256{1};
        job.continuous = state->continuous;
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
            if (!job.continuous && !first)
                state->duplicate_solo_submission = true;
            if (state->revoked || job.header.nVersion != state->parent)
                state->stale_submission = true;
        }
        if (!job.continuous)
            state->retired_parent = job.header.nVersion;
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

  public:
    MockBackend(std::shared_ptr<State> value, bool fail, bool delay = false)
        : state{std::move(value)}, failing{fail}, delay_solo_batch{delay}
    {
    }
    unsigned int Contexts() const override
    {
        return 2;
    }
    uint64_t MemoryBytes() const override
    {
        return 256ULL << 20;
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
    BOOST_CHECK_EQUAL(CalculateGpuBudget(2ULL << 30, 1ULL << 30).contexts, 7U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(8ULL << 30, 8ULL << 30).contexts, 16U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(UINT64_MAX, UINT64_MAX).contexts, 16U);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(UINT64_MAX, UINT64_MAX, 256).scratchpad_bytes, 32ULL << 30);
    BOOST_CHECK_THROW(CalculateGpuBudget(UINT64_MAX, UINT64_MAX, 257), std::runtime_error);
    BOOST_CHECK_THROW(CalculateGpuBudget(1ULL << 30, 1ULL << 30, UINT32_MAX), std::runtime_error);
    BOOST_CHECK_EQUAL(CalculateGpuBudget(4ULL << 30, 64ULL << 20).contexts, 0U);
}
BOOST_AUTO_TEST_CASE(mixed_devices_share_cpu_nonce_allocator_and_refresh)
{
    auto state{std::make_shared<State>()};
    mining::MiningController controller{Hashes(state), Limits, {}, Devices, [state](const auto &, unsigned) {
                                            return std::make_unique<MockBackend>(state, false);
                                        }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 2, {true, true, {}, 0, 0}));
    const bool started{Until([&] { return controller.GetStats().cpu_hashes > 20 && state->gpu_batches > 20; })};
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
BOOST_AUTO_TEST_CASE(gpu_only_verifies_and_submits_continuous_shares)
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
    state->continuous = false;
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
BOOST_AUTO_TEST_SUITE_END()
