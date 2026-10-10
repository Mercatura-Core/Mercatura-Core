// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <mining/cpu_miner.h>

#include <pow.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <vector>

namespace {
bool Until(const std::function<bool()>& condition)
{
    const auto end{std::chrono::steady_clock::now() + std::chrono::seconds{10}};
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return true;
}
struct FakeState {
    std::atomic<int32_t> parent{1};
    std::atomic<unsigned int> jobs{0};
    std::atomic<unsigned int> submissions{0};
    std::atomic<bool> interrupted{false};
    std::atomic<bool> cancelled{false};
};
class FakeProvider : public mining::WorkProvider {
public:
    explicit FakeProvider(std::shared_ptr<FakeState> state) : m_state{std::move(state)} {}
    std::optional<mining::MiningJob> GetJob() override
    {
        ++m_state->jobs;
        mining::MiningJob job;
        job.header.nVersion = m_state->parent;
        job.target = arith_uint256{1};
        return job;
    }
    bool IsCurrent(const mining::MiningJob& job) override { return job.header.nVersion == m_state->parent; }
    mining::SubmissionResult Submit(const mining::MiningJob&, const CBlockHeader&) override
    {
        ++m_state->submissions;
        // Exercise the documented difference between processing and acceptance.
        return {true, false, "Processed only"};
    }
    void Interrupt() override { m_state->interrupted = true; }
    bool IsCancelled() const override { return m_state->cancelled; }
private:
    const std::shared_ptr<FakeState> m_state;
};
}

BOOST_FIXTURE_TEST_SUITE(cpu_miner_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(nonce_space_boundary)
{
    std::atomic<uint64_t> cursor{mining::NONCE_SPACE - 1000};
    std::mutex mutex;
    std::vector<mining::NonceRange> ranges;
    std::vector<std::thread> workers;
    for (int i{0}; i < 16; ++i) {
        workers.emplace_back([&] {
            const auto range{mining::AllocateNonceRange(cursor)};
            if (range) { std::lock_guard lock{mutex}; ranges.push_back(*range); }
        });
    }
    for (auto& worker : workers) worker.join();
    std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) { return a.begin < b.begin; });
    BOOST_REQUIRE_EQUAL(ranges.size(), 4U);
    BOOST_CHECK_EQUAL(ranges.front().begin, mining::NONCE_SPACE - 1000);
    BOOST_CHECK_EQUAL(ranges.back().end, mining::NONCE_SPACE);
    for (size_t i{1}; i < ranges.size(); ++i) BOOST_CHECK_EQUAL(ranges[i - 1].end, ranges[i].begin);
    BOOST_CHECK(!mining::AllocateNonceRange(cursor));
}

BOOST_AUTO_TEST_CASE(hardware_limits)
{
    using mining::CalculateWorkerLimits;
    const auto large{CalculateWorkerLimits(256, uint64_t{128} << 30)};
    BOOST_CHECK_EQUAL(large.maximum, 224U);
    BOOST_CHECK_EQUAL(large.recommended, 128U);
    BOOST_CHECK_EQUAL(CalculateWorkerLimits(16, uint64_t{512} << 20).maximum, 0U);
    BOOST_CHECK_EQUAL(CalculateWorkerLimits(16, uint64_t{1024} << 20).maximum, 3U);
    BOOST_CHECK_EQUAL(CalculateWorkerLimits(64, {}).maximum, 1U);
    BOOST_CHECK_EQUAL(CalculateWorkerLimits(0, uint64_t{8} << 30).recommended, 1U);
}

BOOST_AUTO_TEST_CASE(partition_contexts_counters_cancellation_and_repetition)
{
    const unsigned int maximum{4};
    BOOST_REQUIRE_MESSAGE(maximum > 0, "Test host needs memory for at least one worker");
    for (const unsigned int count : std::set<unsigned int>{1, std::min(2U, maximum), std::min(4U, maximum)}) {
        std::mutex mutex;
        std::set<std::pair<int32_t, uint32_t>> nonces;
        std::set<unsigned int> used_contexts;
        std::atomic<unsigned int> contexts{0};
        std::atomic<uint64_t> calls{0};
        std::atomic<bool> duplicate{false};
        auto state{std::make_shared<FakeState>()};
        mining::MiningController controller{[&] {
            const unsigned int id{contexts++};
            return [&, id](const CBlockHeader& header) {
                {
                    std::lock_guard lock{mutex};
                    used_contexts.insert(id);
                    if (!nonces.emplace(header.nVersion, header.nNonce).second) duplicate = true;
                }
                ++calls;
                std::this_thread::sleep_for(std::chrono::microseconds{100});
                return uint256::FromHex(std::string(64, 'f')).value();
            };
        }, [] { return mining::CalculateWorkerLimits(8, uint64_t{8} << 30); }};
        BOOST_CHECK(controller.GetStats().state == mining::MiningState::STOPPED);
        BOOST_CHECK_EQUAL(controller.GetStats().hashes, 0U); // No auto mining.
        for (int32_t cycle{0}; cycle < 3; ++cycle) {
            state->parent = cycle * 2 + 1;
            const uint64_t start_calls{calls.load()};
            BOOST_REQUIRE(controller.Start([state] { return std::make_unique<FakeProvider>(state); }, count));
            BOOST_CHECK(!controller.Start([state] { return std::make_unique<FakeProvider>(state); }, count));
            BOOST_REQUIRE(Until([&] { return calls >= start_calls + count * 10; }));
            ++state->parent;
            BOOST_REQUIRE(Until([&] {
                std::lock_guard lock{mutex};
                return nonces.contains({state->parent.load(), 0});
            }));
            controller.RequestStop();
            controller.Wait();
            const auto stats{controller.GetStats()};
            BOOST_CHECK(stats.state == mining::MiningState::STOPPED);
            BOOST_CHECK(!stats.busy);
            BOOST_CHECK_EQUAL(stats.hashes, calls - start_calls);
            BOOST_CHECK_EQUAL(stats.active_workers, 0U);
            BOOST_CHECK_EQUAL(stats.hashes_per_second, 0);
            const uint64_t stopped_calls{calls.load()};
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            BOOST_CHECK_EQUAL(calls.load(), stopped_calls);
        }
        BOOST_CHECK_EQUAL(contexts, count * 3);
        BOOST_CHECK_EQUAL(used_contexts.size(), contexts.load());
        BOOST_CHECK(!duplicate);
        BOOST_CHECK(state->interrupted);
    }
}

BOOST_AUTO_TEST_CASE(provider_cancellation_stops_the_session)
{
    auto state{std::make_shared<FakeState>()};
    mining::MiningController controller{[] {
        return [](const CBlockHeader&) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            return uint256::FromHex(std::string(64, 'f')).value();
        };
    }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<FakeProvider>(state); }, 1));
    BOOST_REQUIRE(Until([&] { return controller.GetStats().hashes > 0; }));
    state->cancelled = true;
    BOOST_REQUIRE(Until([&] { return !controller.GetStats().busy; }));
    controller.Wait();
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::STOPPED);
    BOOST_CHECK(state->interrupted);
}

BOOST_AUTO_TEST_CASE(worker_and_provider_failure_cleanup)
{
    for (bool fail_worker : {false, true}) {
        auto state{std::make_shared<FakeState>()};
        mining::MiningController controller{[]() -> mining::MiningController::HashFunction { throw std::bad_alloc{}; }};
        BOOST_REQUIRE(controller.Start([state, fail_worker]() -> std::unique_ptr<mining::WorkProvider> {
            if (!fail_worker) throw std::runtime_error{"Provider startup failed"};
            return std::make_unique<FakeProvider>(state);
        }, 1));
        BOOST_REQUIRE(Until([&] { return !controller.GetStats().busy; }));
        controller.Wait();
        BOOST_CHECK(controller.GetStats().state == mining::MiningState::FAILED);
        BOOST_CHECK_EQUAL(controller.GetStats().active_workers, 0U);
        BOOST_CHECK_EQUAL(controller.GetStats().hashes, 0U);
    }
}

BOOST_AUTO_TEST_CASE(stop_does_not_wait_for_an_inflight_hash)
{
    std::mutex mutex;
    std::condition_variable cv;
    bool release{false};
    std::atomic<bool> entered{false};
    auto state{std::make_shared<FakeState>()};
    mining::MiningController controller{[&] {
        return [&](const CBlockHeader&) {
            entered = true;
            std::unique_lock lock{mutex};
            cv.wait(lock, [&] { return release; });
            return uint256::FromHex(std::string(64, 'f')).value();
        };
    }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<FakeProvider>(state); }, 1));
    const bool hashing{Until([&] { return entered.load(); })};
    controller.RequestStop();
    const bool still_busy{controller.GetStats().busy};
    {
        std::lock_guard lock{mutex};
        release = true;
    }
    cv.notify_all();
    controller.Wait();
    BOOST_CHECK(hashing);
    BOOST_CHECK(still_busy); // Stop returns while the current attempt finishes.
    BOOST_CHECK_EQUAL(controller.GetStats().hashes, 1U);
    BOOST_CHECK(!controller.GetStats().busy);
}

BOOST_AUTO_TEST_CASE(worker_failure_joins_started_workers_before_restart)
{
    std::mutex mutex;
    std::condition_variable cv;
    bool release{false};
    std::atomic<bool> entered{false};
    std::atomic<bool> fail{true};
    std::atomic<unsigned int> factories{0};
    std::atomic<unsigned int> destroyed{0};
    auto state{std::make_shared<FakeState>()};
    mining::MiningController controller{[&]() -> mining::MiningController::HashFunction {
        if (factories++ == 1 && fail) throw std::runtime_error{"Second worker startup failed"};
        auto context{std::shared_ptr<int>{new int{0}, [&](int* value) { delete value; ++destroyed; }}};
        entered = true;
        {
            std::unique_lock lock{mutex};
            cv.wait(lock, [&] { return release; });
        }
        return [context](const CBlockHeader&) { return uint256::FromHex(std::string(64, 'f')).value(); };
    }, [] { return mining::CalculateWorkerLimits(8, uint64_t{8} << 30); }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<FakeProvider>(state); }, 2));
    const bool failed{Until([&] { return entered && controller.GetStats().state == mining::MiningState::FAILED; })};
    controller.RequestStop();
    const bool waiting_for_worker{controller.GetStats().busy};
    const unsigned int destroyed_before_release{destroyed.load()};
    {
        std::lock_guard lock{mutex};
        release = true;
    }
    cv.notify_all();
    controller.Wait();
    BOOST_REQUIRE(failed);
    BOOST_CHECK(waiting_for_worker);
    BOOST_CHECK_EQUAL(destroyed_before_release, 0U);
    BOOST_CHECK_EQUAL(destroyed, 1U);
    BOOST_CHECK(!controller.GetStats().busy);
    BOOST_CHECK_EQUAL(controller.GetStats().active_workers, 0U);
    fail = false;
    factories = 0;
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<FakeProvider>(state); }, 2));
    const bool hashing{Until([&] { return controller.GetStats().hashes > 0 && factories == 2; })};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(hashing);
    BOOST_CHECK_EQUAL(destroyed, 3U);
    BOOST_CHECK(!controller.GetStats().busy);
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::STOPPED);
}

BOOST_AUTO_TEST_CASE(submission_does_not_imply_acceptance)
{
    auto state{std::make_shared<FakeState>()};
    mining::MiningController controller{[] { return [](const CBlockHeader&) { return uint256::ZERO; }; }};
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<FakeProvider>(state); }, 1));
    BOOST_REQUIRE(Until([&] { return controller.GetStats().submitted > 0; }));
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(controller.GetStats().solutions > 0);
    BOOST_CHECK(controller.GetStats().submitted > 0);
    BOOST_CHECK_EQUAL(controller.GetStats().accepted, 0U);
}

BOOST_AUTO_TEST_CASE(excessive_worker_count_rejected_before_allocation)
{
    std::atomic<unsigned int> allocations{0};
    mining::MiningController controller{[&] {
        ++allocations;
        return [](const CBlockHeader&) { return uint256::ZERO; };
    }};
    BOOST_REQUIRE(controller.Start([] { return std::unique_ptr<mining::WorkProvider>{}; }, std::numeric_limits<unsigned int>::max()));
    controller.Wait();
    BOOST_CHECK(controller.GetStats().state == mining::MiningState::FAILED);
    BOOST_CHECK_EQUAL(allocations, 0U);
}

BOOST_AUTO_TEST_CASE(authoritative_contexts_are_independent)
{
    std::array<unsigned char, 80> bytes;
    for (size_t i{0}; i < bytes.size(); ++i) bytes[i] = i;
    SpanReader reader{std::span<const unsigned char>{bytes}};
    CBlockHeader header;
    reader >> header;
    const auto expected{ParseHex("2321712af21502878986c0c4f21d79e17e2281619e3958f11e3c634c9f17f7d8")};
    std::array<uint256, 2> results;
    std::array<std::thread, 2> workers;
    for (size_t i{0}; i < workers.size(); ++i) {
        workers[i] = std::thread{[&, i] {
            PoWHashContext context;
            results[i] = context.GetHash(header);
        }};
    }
    for (auto& worker : workers) worker.join();
    for (const auto& result : results) {
        BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
    }
}
BOOST_AUTO_TEST_SUITE_END()
