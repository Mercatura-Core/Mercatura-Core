// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <mining/statistics.h>

#include <boost/test/unit_test.hpp>

using namespace std::chrono_literals;
BOOST_AUTO_TEST_SUITE(mining_statistics_tests)

BOOST_AUTO_TEST_CASE(irregular_counters_windows_and_startup)
{
    mining::MiningStatistics stats;
    const mining::MiningStatistics::Clock::time_point start{};
    BOOST_CHECK_EQUAL(stats.Observe(start, 100).hashes, 0U);
    stats.Reset(start);
    BOOST_CHECK_EQUAL(stats.Observe(start, 0).average_10s, 0);
    const auto early{stats.Observe(start + 2s, 20)};
    BOOST_CHECK_EQUAL(early.average_10s, 10);
    BOOST_CHECK_EQUAL(early.average_60s, 10);
    stats.Observe(start + 7s, 70);
    stats.Observe(start + 13s, 130);
    const auto irregular{stats.Observe(start + 20s, 270)};
    // Window boundary at 10s is interpolated between actual 7s/13s counters.
    BOOST_CHECK_CLOSE(irregular.average_10s, 17.0, 0.00001);
    BOOST_CHECK_CLOSE(irregular.average_60s, 13.5, 0.00001);
    BOOST_CHECK_CLOSE(irregular.average_session, 13.5, 0.00001);
    BOOST_CHECK_EQUAL(irregular.hashes, 270U);
}

BOOST_AUTO_TEST_CASE(pause_reset_stop_and_restart)
{
    mining::MiningStatistics stats;
    const mining::MiningStatistics::Clock::time_point start{};
    stats.Reset(start);
    stats.Observe(start + 10s, 100);
    stats.Observe(start + 20s, 100);
    const auto paused{stats.Observe(start + 70s, 100)};
    BOOST_CHECK_EQUAL(paused.average_10s, 0);
    BOOST_CHECK_EQUAL(paused.average_60s, 0);
    BOOST_CHECK_CLOSE(paused.average_session, 100.0 / 70, 0.00001);
    const auto reset{stats.Observe(start + 71s, 5)};
    BOOST_CHECK_EQUAL(reset.hashes, 105U);
    BOOST_CHECK_CLOSE(reset.average_10s, 0.5, 0.00001);
    stats.Finish(start + 72s, 10);
    const auto stopped{stats.Observe(start + 1000s, 9999)};
    BOOST_CHECK_EQUAL(stopped.hashes, 110U);
    BOOST_CHECK_EQUAL(stopped.elapsed.count(), 72000);
    BOOST_CHECK_EQUAL(stopped.live, 0);
    stats.Reset(start + 1001s);
    const auto restarted{stats.Observe(start + 1002s, 3)};
    BOOST_CHECK_EQUAL(restarted.hashes, 3U);
    BOOST_CHECK_EQUAL(restarted.elapsed.count(), 1000);
    BOOST_CHECK_EQUAL(restarted.average_session, 3);
}

BOOST_AUTO_TEST_CASE(subsecond_polling_and_long_sessions)
{
    mining::MiningStatistics stats;
    const mining::MiningStatistics::Clock::time_point start{};
    stats.Reset(start, 100);
    for (int64_t i{1}; i <= 1300; ++i) {
        const auto measured{stats.Observe(start + 100ms * i, 100 + i)};
        BOOST_CHECK_CLOSE(measured.average_10s, 10.0, 0.00001);
        BOOST_CHECK_CLOSE(measured.average_60s, 10.0, 0.00001);
    }
    const auto long_session{stats.Observe(start + 49h, 100 + 1764000)};
    BOOST_CHECK_EQUAL(long_session.elapsed.count(), 176400000);
    BOOST_CHECK_CLOSE(long_session.average_session, 10.0, 0.00001);
    BOOST_CHECK_CLOSE(long_session.average_10s, 10.0, 0.00001);
    BOOST_CHECK_CLOSE(long_session.average_60s, 10.0, 0.00001);
}

BOOST_AUTO_TEST_CASE(final_hashes_on_a_repeated_clock_tick)
{
    mining::MiningStatistics stats;
    const mining::MiningStatistics::Clock::time_point start{};
    stats.Reset(start);
    BOOST_CHECK_EQUAL(stats.Observe(start, 1).hashes, 1U);
    stats.Observe(start + 1s, 10);
    stats.Finish(start + 1s, 12);
    const auto finished{stats.Observe(start + 2s, 99)};
    BOOST_CHECK_EQUAL(finished.hashes, 12U);
    BOOST_CHECK_EQUAL(finished.elapsed.count(), 1000);
    BOOST_CHECK_EQUAL(finished.average_session, 12);
    BOOST_CHECK_EQUAL(finished.average_10s, 12);
}
BOOST_AUTO_TEST_SUITE_END()
