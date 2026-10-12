// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/difficulty.h>

#include <pool/job.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace pool {
int64_t MonotonicSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
arith_uint256 BoundTarget(const Config& cfg, const arith_uint256& target, const arith_uint256& network)
{
    if (network == 0) throw std::invalid_argument("zero network target");
    // The network floor overrides operator bounds: every valid block qualifies.
    if (!cfg.vardiff) return std::max(target, network);
    return std::max(network, std::clamp(target, ParseTarget(cfg.share_min_target), ParseTarget(cfg.share_max_target)));
}
void Difficulty::Reset()
{
    m_started.reset();
    m_work = Number{};
    m_shares = 0;
    m_overloaded = false;
}
void Difficulty::Accepted(const Number& score)
{
    if (!m_started) return; // No active observation yet (e.g. resumed old work).
    if (score.Zero() || score > Number::PowerOfTwo(256) || m_shares >= 1000000000) throw std::invalid_argument("invalid difficulty observation");
    m_work += score; // At most 286 bits, including the enforced sample bound.
    ++m_shares;
}
arith_uint256 Difficulty::Next(const Config& cfg, const arith_uint256& current, const arith_uint256& network, int64_t now)
{
    if (now < 0) throw std::invalid_argument("negative monotonic observation time");
    auto target = BoundTarget(cfg, current, network);
    if (!cfg.vardiff) return target;
    if (!m_started || now < *m_started || target != current) {
        Reset();
        m_started = now;
        return target;
    }
    const auto elapsed = now - *m_started;
    if (elapsed < cfg.vardiff_min_seconds || (m_shares < 4 && elapsed < cfg.vardiff_window_seconds)) return target;
    // Stalled reactors must not turn a long pause into a large easing signal.
    if (elapsed > cfg.vardiff_window_seconds * 2) {
        Reset();
        m_started = now;
        return target;
    }
    const auto score = WorkScore(Number::Parse(target.GetHex(), true));
    const auto estimate = m_work * Number{static_cast<uint64_t>(cfg.share_interval_seconds)} / Number{static_cast<uint64_t>(elapsed)};
    const auto smooth = (score + estimate) / Number{2};
    // Rejected verification demand biases the accepted-work sample downward.
    // Never ease an overloaded observation; it would amplify the lost work.
    if ((!m_overloaded && estimate * Number{4} < score * Number{3}) || estimate * Number{4} > score * Number{5}) {
        const auto next_score = std::clamp(smooth, std::max(Number{1}, score / Number{2}), std::min(Number::PowerOfTwo(256), score * Number{2}));
        const auto space = Number::PowerOfTwo(256);
        const auto [quotient, remainder] = Number::DivMod(space, next_score);
        // Round the denominator up: never assign more work than requested.
        auto numeric = quotient + Number{remainder.Zero() ? 0U : 1U} - Number{1};
        const auto denominator = Number::Parse(target.GetHex(), true) + Number{1};
        // Quantized very hard targets need target-space bounds as well as score
        // bounds. E.g. changing target 4 to 1 would increase expected work 2.5x.
        const auto hardest = (denominator + Number{1}) / Number{2} - Number{1};
        const auto easiest = std::min(space, denominator * Number{2}) - Number{1};
        numeric = std::clamp(numeric, hardest, easiest);
        target = BoundTarget(cfg, ParseTarget(numeric.Hex256()), network);
    }
    Reset();
    m_started = now;
    return target;
}
} // namespace pool
