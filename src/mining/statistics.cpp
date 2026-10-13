// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <mining/statistics.h>

#include <algorithm>

namespace mining {
void MiningStatistics::Reset(Clock::time_point now, uint64_t counter)
{
    m_samples.clear();
    m_samples.push_back({now, 0});
    m_start = m_last_time = now;
    m_last_counter = counter;
    m_total = 0;
    m_started = true;
    m_finished = false;
    m_result = {};
}

double MiningStatistics::Rate(Clock::time_point now, std::chrono::seconds window) const
{
    const auto begin{std::max(m_start, now - window)};
    const double seconds{std::chrono::duration<double>(now - begin).count()};
    if (seconds <= 0) return 0;
    Sample previous{m_samples.front()};
    for (const auto& sample : m_samples) {
        if (sample.time > begin) {
            const double fraction{std::chrono::duration<double>(begin - previous.time).count() /
                                  std::chrono::duration<double>(sample.time - previous.time).count()};
            // Estimate only the fractional boundary interval between actual
            // counter observations. The remaining window uses exact differences.
            return ((m_total - previous.hashes) - fraction * (sample.hashes - previous.hashes)) / seconds;
        }
        previous = sample;
    }
    const double interval{std::chrono::duration<double>(now - previous.time).count()};
    const double fraction{interval > 0 ? std::chrono::duration<double>(begin - previous.time).count() / interval : 0};
    return (m_total - previous.hashes) * (1 - fraction) / seconds;
}

SessionStatistics MiningStatistics::Observe(Clock::time_point now, uint64_t counter)
{
    if (!m_started || m_finished) return m_result;
    // A reset starts a fresh counter epoch, not a huge delta. Repeated clock
    // ticks can still contain completed work, including the final observation.
    if (now < m_last_time) return m_result;
    m_total += counter >= m_last_counter ? counter - m_last_counter : counter;
    m_last_counter = counter;
    m_last_time = now;
    // At most two stored samples per second, retaining the boundary predecessor.
    // UI polling continues to sample even while a provider call blocks.
    while (m_samples.size() > 1 && m_samples[1].time <= now - std::chrono::seconds{60}) m_samples.pop_front();
    m_result.hashes = m_total;
    m_result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start);
    if (now == m_start) return m_result;
    m_result.average_session = m_total / std::chrono::duration<double>(now - m_start).count();
    m_result.average_10s = Rate(now, std::chrono::seconds{10});
    m_result.average_60s = Rate(now, std::chrono::seconds{60});
    const auto& previous{m_samples.back()};
    if (now > previous.time) m_result.live = (m_total - previous.hashes) / std::chrono::duration<double>(now - previous.time).count();
    if (now - previous.time >= std::chrono::milliseconds{500}) m_samples.push_back({now, m_total});
    return m_result;
}

void MiningStatistics::Finish(Clock::time_point now, uint64_t counter)
{
    Observe(now, counter);
    m_result.live = 0;
    m_finished = true;
}
} // namespace mining
