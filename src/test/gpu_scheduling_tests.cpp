// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <mining/cpu_miner.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <thread>

namespace {
constexpr unsigned COUNTS[]{1, 128, 192, 255, 256, 257, 325, 512, 1024, 8193};
bool Wait(const std::function<bool()>& predicate)
{
    const auto end{std::chrono::steady_clock::now() + std::chrono::seconds{10}};
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return true;
}
struct Simulation {
    std::mutex mutex;
    std::set<std::pair<int32_t, uint32_t>> seen;
    bool duplicate{false}, stale{false};
    std::atomic<unsigned> batches{0}, largest{0}, submits{0};
    std::atomic<int32_t> parent{7};
    std::atomic<mercahash::Version> version{mercahash::Version::V2};
    std::atomic<bool> block{false}, entered{false}, release{true}, hits{false}, bad{false}, late{false};
    std::atomic<unsigned> lifetime_ms{30000};
    void Record(const CBlockHeader& header)
    {
        std::lock_guard lock{mutex};
        duplicate |= !seen.emplace(header.nVersion, header.nNonce).second;
    }
};
class Provider : public mining::WorkProvider {
    std::shared_ptr<Simulation> state;
public:
    explicit Provider(std::shared_ptr<Simulation> s) : state{std::move(s)} {}
    std::optional<mining::MiningJob> GetJob() override
    {
        mining::MiningJob job;
        job.header.nVersion = ++state->parent;
        job.pow_version = state->version;
        job.target = state->hits ? ~arith_uint256{} : arith_uint256{1};
        job.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{state->lifetime_ms.load()};
        return job;
    }
    bool IsCurrent(const mining::MiningJob& job) override { return job.header.nVersion == state->parent && job.pow_version == state->version; }
    mining::SubmissionResult Submit(const mining::MiningJob& job, const CBlockHeader&) override
    {
        std::lock_guard lock{state->mutex};
        state->stale |= !IsCurrent(job);
        ++state->submits;
        return {};
    }
    void Interrupt() override {}
};
class Backend : public mining::GpuBackend {
    std::shared_ptr<Simulation> state;
    unsigned contexts;
    mercahash::Version version;
public:
    Backend(std::shared_ptr<Simulation> s, unsigned n, mercahash::Version v) : state{std::move(s)}, contexts{n}, version{v} {}
    unsigned Contexts() const override { return contexts; }
    mercahash::Version PoWVersion() const override { return version; }
    uint64_t MemoryBytes() const override { return uint64_t{contexts} * mercahash::ScratchpadBytes(version); }
    std::vector<mining::GpuHash> Hash(std::span<const mining::GpuHeader> headers, const std::function<bool()>& cancelled) override
    {
        constexpr const char* v1[]{"cc97e7cbce1d4ed2b54c9c37a81a000383229341a277a44829484855b9ef79dc", "2321712af21502878986c0c4f21d79e17e2281619e3958f11e3c634c9f17f7d8", "9c9a427928875304bb7d6351354b5e50ec8e57c581971f68eab176371c4cd2c2", "1244c6c9e0365baeb5958cfaa57aff582d81ae1d757769e823085bb00676e170"};
        constexpr const char* v2[]{"eb53364febf3cbe079be98c554f125ee3b78cc64d162fb831f4364f0f8b26dd8", "98c21c199742ff142100cc795857156909d73ac8fa81012a2803158dc0927b63", "306b4fd4000c5605e57f48395daf1e176c8cc92fd72a14440a9d028f4de93d62", "1328a9db2db6212d474953c5c5a5e20d91a04179d77c60c460ede9b61015243c"};
        std::vector<mining::GpuHash> results;
        bool mining{false};
        for (const auto& bytes : headers) {
            if (cancelled()) return {};
            int pattern{-1};
            for (unsigned p{0}; p < 4; ++p) {
                bool match{true};
                for (unsigned i{0}; i < 80; ++i)
                    match &= bytes[i] == (p == 0 ? 0 : p == 1 ? i : p == 2 ? 255 : (i * 37 + 11) & 255);
                if (match) pattern = p;
            }
            mining::GpuHash hash{};
            if (pattern >= 0) {
                const auto expected{ParseHex((version == mercahash::Version::V2 ? v2 : v1)[pattern])};
                std::copy(expected.begin(), expected.end(), hash.begin());
            } else {
                mining = true;
                if (state->block) {
                    state->entered = true;
                    while (!state->release && !cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    if (cancelled()) return {};
                }
                CBlockHeader header;
                SpanReader reader{std::span<const unsigned char>{bytes}}; reader >> header;
                state->Record(header);
                if (!state->hits) hash.fill(255);
                else if (state->bad) hash[0] = 1;
            }
            results.push_back(hash);
        }
        if (mining) {
            ++state->batches;
            unsigned previous{state->largest};
            while (previous < headers.size() && !state->largest.compare_exchange_weak(previous, headers.size())) {}
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            if (state->late) {
                state->entered = true;
                while (!state->release) std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }
        return results;
    }
};
}
BOOST_FIXTURE_TEST_SUITE(gpu_scheduling_tests, BasicTestingSetup)
BOOST_AUTO_TEST_CASE(lease_tails_and_final_nonce_batches)
{
    for (const auto count : COUNTS) {
        std::atomic<uint64_t> cursor{0};
        mining::V2NonceBatch batch;
        uint64_t expected{0};
        for (unsigned i{0}; i < 5; ++i) {
            const auto nonces{batch.Next(cursor, count, mining::AllocateNonceRange, [] { return false; })};
            BOOST_REQUIRE_EQUAL(nonces.size(), count);
            for (const auto nonce : nonces) BOOST_REQUIRE_EQUAL(nonce, expected++);
        }
        cursor = mining::NONCE_SPACE - count - 13;
        mining::V2NonceBatch final;
        const auto first{final.Next(cursor, count, mining::AllocateNonceRange, [] { return false; })};
        const auto tail{final.Next(cursor, count + 13, mining::AllocateNonceRange, [] { return false; })};
        BOOST_REQUIRE_EQUAL(first.size(), count);
        BOOST_REQUIRE_EQUAL(tail.size(), 13U);
        BOOST_CHECK_EQUAL(tail.back(), UINT32_MAX);
        BOOST_CHECK(final.Next(cursor, count, mining::AllocateNonceRange, [] { return false; }).empty());
        BOOST_CHECK_EQUAL(cursor, mining::NONCE_SPACE);
        const auto before{cursor.load()};
        BOOST_CHECK(batch.Next(cursor, count, mining::AllocateNonceRange, [] { return true; }).empty());
        BOOST_CHECK_EQUAL(cursor, before);
    }
}
BOOST_AUTO_TEST_CASE(mixed_lease_allocation_never_overlaps_or_wraps)
{
    constexpr uint64_t total{100003};
    std::atomic<uint64_t> cursor{mining::NONCE_SPACE - total};
    std::mutex mutex;
    std::vector<uint32_t> seen;
    std::vector<std::thread> workers;
    for (const unsigned size : {0U, 257U, 325U, 1024U}) workers.emplace_back([&, size] {
        mining::V2NonceBatch batch;
        while (true) {
            std::vector<uint32_t> nonces;
            if (size) nonces = batch.Next(cursor, size, mining::AllocateNonceRange, [] { return false; });
            else if (const auto range{mining::AllocateNonceRange(cursor)})
                for (auto n{range->begin}; n < range->end; ++n) nonces.push_back(static_cast<uint32_t>(n));
            if (nonces.empty()) break;
            std::lock_guard lock{mutex}; seen.insert(seen.end(), nonces.begin(), nonces.end());
        }
    });
    for (auto& worker : workers) worker.join();
    std::sort(seen.begin(), seen.end());
    BOOST_REQUIRE_EQUAL(seen.size(), total);
    BOOST_CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
    BOOST_CHECK_EQUAL(seen.front(), mining::NONCE_SPACE - total);
    BOOST_CHECK_EQUAL(seen.back(), UINT32_MAX);
}
BOOST_AUTO_TEST_CASE(controller_final_partial_batch_uses_the_lease_tail)
{
    auto state{std::make_shared<Simulation>()};
    constexpr uint64_t total{338};
    mining::MiningController controller{{}, [] { return mining::CalculateWorkerLimits(8, 16ULL << 30); },
        [](std::atomic<uint64_t>& cursor) {
            uint64_t zero{0}; cursor.compare_exchange_strong(zero, mining::NONCE_SPACE - total);
            return mining::AllocateNonceRange(cursor);
        }, [] {
            mining::GpuDiscovery discovery; mining::GpuDevice device;
            device.id = "cuda:0"; device.supported = true; discovery.devices.push_back(device); return discovery;
        }, [state](const auto& device, unsigned requested) {
            return std::make_unique<Backend>(state, requested, device.pow_version);
        }};
    mining::GpuConfig config; config.cpu = false; config.gpu = true; config.contexts = 325; config.yield_ms = 0;
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 0, config));
    const bool complete{Wait([&] { std::lock_guard lock{state->mutex}; return state->seen.size() >= total; })};
    controller.RequestStop(); controller.Wait();
    BOOST_CHECK(complete && !state->duplicate);
    BOOST_CHECK_EQUAL(state->largest, 325U);
    std::map<int32_t, unsigned> jobs;
    for (const auto& [job, nonce] : state->seen) {
        BOOST_CHECK_GE(nonce, mining::NONCE_SPACE - total);
        ++jobs[job];
    }
    BOOST_CHECK(std::any_of(jobs.begin(), jobs.end(), [](const auto& job) { return job.second == total; }));
}
BOOST_AUTO_TEST_CASE(cancelled_and_invalid_leases_fail_closed)
{
    mining::V2NonceBatch batch;
    std::atomic<uint64_t> cursor{0};
    bool cancelled{false};
    const auto stopped{batch.Next(cursor, 325, [&](auto& value) {
        cancelled = true; return mining::AllocateNonceRange(value);
    }, [&] { return cancelled; })};
    BOOST_CHECK(stopped.empty());
    mining::V2NonceBatch new_job;
    std::atomic<uint64_t> new_cursor{0};
    const auto fresh{new_job.Next(new_cursor, 325, mining::AllocateNonceRange, [] { return false; })};
    BOOST_REQUIRE_EQUAL(fresh.size(), 325U);
    BOOST_CHECK_EQUAL(fresh.front(), 0U);
    BOOST_CHECK_THROW(new_job.Next(new_cursor, 0, mining::AllocateNonceRange, [] { return false; }), std::runtime_error);
    mining::V2NonceBatch malformed;
    BOOST_CHECK_THROW(malformed.Next(cursor, 1, [](auto&) { return std::optional<mining::NonceRange>{{0, mining::NONCE_SPACE + 1}}; }, [] { return false; }), std::runtime_error);
}
BOOST_AUTO_TEST_CASE(completed_v2_candidates_are_retired_on_reorganization)
{
    auto state{std::make_shared<Simulation>()};
    state->hits = true; state->late = true; state->release = false;
    mining::MiningController controller{[] { return [](const CBlockHeader&) { return uint256{}; }; },
        [] { return mining::CalculateWorkerLimits(8, 16ULL << 30); }, {}, [] {
            mining::GpuDiscovery discovery; mining::GpuDevice device;
            device.id = "cuda:0"; device.supported = true; discovery.devices.push_back(device); return discovery;
        }, [state](const auto& device, unsigned) {
            return std::make_unique<Backend>(state, device.pow_version == mercahash::Version::V2 ? 325 : 256, device.pow_version);
        }};
    mining::GpuConfig config; config.cpu = false; config.gpu = true; config.yield_ms = 0;
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 0, config));
    const bool entered{Wait([&] { return state->entered.load(); })};
    state->hits = false; state->version = mercahash::Version::V1; ++state->parent;
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    state->release = true;
    const bool reorganized{Wait([&] { const auto stats{controller.GetStats()}; return stats.devices.size() == 1 && stats.devices[0].device.pow_version == mercahash::Version::V1 && stats.devices[0].contexts == 256; })};
    controller.RequestStop(); controller.Wait();
    BOOST_CHECK(entered && reorganized && !state->stale);
    BOOST_CHECK_EQUAL(state->submits, 0U);
    BOOST_CHECK_GE(controller.GetStats().devices[0].hashes, 325U);
}
BOOST_AUTO_TEST_CASE(large_manual_v2_selection_cannot_expand_the_v1_path)
{
    auto state{std::make_shared<Simulation>()};
    mining::MiningController controller{{}, [] { return mining::CalculateWorkerLimits(8, 16ULL << 30); }, {}, [] {
        mining::GpuDiscovery discovery; mining::GpuDevice device;
        device.id = "cuda:0"; device.supported = true; discovery.devices.push_back(device); return discovery;
    }, [state](const auto& device, unsigned requested) {
        return std::make_unique<Backend>(state, requested, device.pow_version);
    }};
    mining::GpuConfig config; config.cpu = false; config.gpu = true; config.contexts = 325; config.yield_ms = 0;
    BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 0, config));
    const bool running{Wait([&] { return state->batches > 0; })};
    state->version = mercahash::Version::V1; ++state->parent;
    const bool rejected{Wait([&] { return controller.GetStats().state == mining::MiningState::FAILED; })};
    controller.RequestStop(); controller.Wait();
    BOOST_CHECK(running && rejected);
    BOOST_CHECK_EQUAL(controller.GetStats().submitted, 0U);
    BOOST_CHECK_EQUAL(controller.GetStats().active_workers, 0U);
}
BOOST_AUTO_TEST_CASE(controller_large_batches_restarts_transitions_and_stale_work)
{
    for (const unsigned count : COUNTS) {
        auto state{std::make_shared<Simulation>()};
        mining::MiningController controller{[state] { return [state](const CBlockHeader& header) {
            if (state->hits) return uint256{};
            state->Record(header);
            std::this_thread::sleep_for(std::chrono::microseconds{50});
            return uint256::FromHex(std::string(64, 'f')).value();
        }; }, [] { return mining::CalculateWorkerLimits(8, 16ULL << 30); }, {}, [] {
            mining::GpuDiscovery discovery;
            for (unsigned i{0}; i < 2; ++i) {
                mining::GpuDevice device;
                device.id = "cuda:" + util::ToString(i); device.supported = true;
                discovery.devices.push_back(device);
            }
            return discovery;
        }, [state, count](const auto& device, unsigned) {
            return std::make_unique<Backend>(state, device.pow_version == mercahash::Version::V2 ? count : 256, device.pow_version);
        }};
        mining::GpuConfig config; config.gpu = true; config.yield_ms = 0;
        BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 1, config));
        const bool ran{Wait([&] { return state->batches >= 4 && state->largest >= count; })};
        state->block = true; state->release = false;
        const bool entered{Wait([&] { return state->entered.load(); })};
        ++state->parent; // Reorganization replaces the immutable job.
        state->version = mercahash::Version::V1;
        state->release = true; state->block = false;
        const bool downgraded{Wait([&] { const auto s{controller.GetStats()}; return s.devices.size() == 2 && s.devices[0].device.pow_version == mercahash::Version::V1 && s.devices[0].contexts == 256; })};
        state->version = mercahash::Version::V2; ++state->parent;
        const bool upgraded{Wait([&] { const auto s{controller.GetStats()}; return s.devices.size() == 2 && s.devices[0].device.pow_version == mercahash::Version::V2 && s.devices[0].contexts == count; })};
        state->lifetime_ms = 1; ++state->parent; // Expiration retires work without a submission.
        std::this_thread::sleep_for(std::chrono::milliseconds{120});
        controller.RequestStop(); controller.Wait();
        BOOST_CHECK(ran && entered && downgraded && upgraded);
        BOOST_CHECK(!state->duplicate && !state->stale);
        BOOST_CHECK_EQUAL(state->submits, 0U);
        BOOST_CHECK_GT(controller.GetStats().cpu_hashes, 0U);
        BOOST_REQUIRE_EQUAL(controller.GetStats().devices.size(), 2U);
        BOOST_CHECK_GT(controller.GetStats().devices[0].hashes, 0U);
        BOOST_CHECK_GT(controller.GetStats().devices[1].hashes, 0U);
        BOOST_CHECK_EQUAL(controller.GetStats().active_workers, 0U);
        state->lifetime_ms = 30000; state->hits = true; ++state->parent;
        BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 1, config));
        const bool submitted{Wait([&] { return state->submits > 0; })};
        controller.RequestStop(); controller.Wait();
        BOOST_CHECK(submitted && !state->stale);
        state->bad = true; ++state->parent; config.cpu = false;
        BOOST_REQUIRE(controller.Start([state] { return std::make_unique<Provider>(state); }, 0, config));
        const bool rejected{Wait([&] { return controller.GetStats().state == mining::MiningState::FAILED; })};
        controller.RequestStop(); controller.Wait();
        BOOST_CHECK(rejected);
        BOOST_CHECK_EQUAL(controller.GetStats().submitted, 0U);
    }
}
BOOST_AUTO_TEST_SUITE_END()
