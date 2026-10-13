// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_MINING_STATISTICS_H
#define BITCOIN_MINING_STATISTICS_H

#include <chrono>
#include <cstdint>
#include <deque>

namespace mining {
struct SessionStatistics {
    uint64_t hashes{0};
    std::chrono::milliseconds elapsed{0};
    double live{0};
    double average_10s{0};
    double average_60s{0};
    double average_session{0};
};

// Called only while holding the controller mutex, never by a hashing worker.
// Timestamped cumulative counters avoid weighting irregular rate samples equally.
class MiningStatistics {
public:
    using Clock = std::chrono::steady_clock;
    void Reset(Clock::time_point now, uint64_t counter = 0);
    SessionStatistics Observe(Clock::time_point now, uint64_t counter);
    void Finish(Clock::time_point now, uint64_t counter);

private:
    struct Sample { Clock::time_point time; uint64_t hashes; };
    double Rate(Clock::time_point now, std::chrono::seconds window) const;
    std::deque<Sample> m_samples;
    Clock::time_point m_start{};
    Clock::time_point m_last_time{};
    uint64_t m_last_counter{0};
    uint64_t m_total{0};
    bool m_started{false};
    bool m_finished{false};
    SessionStatistics m_result;
};
} // namespace mining
#endif // BITCOIN_MINING_STATISTICS_H
