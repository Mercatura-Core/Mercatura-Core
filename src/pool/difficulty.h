// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_DIFFICULTY_H
#define BITCOIN_POOL_DIFFICULTY_H

#include <arith_uint256.h>
#include <pool/config.h>
#include <pool/number.h>

#include <optional>

namespace pool {
// One bounded observation per connected session. Only committed accepted work
// enters this estimator. The next target is persisted with the next issued job;
// issued jobs and historical scores are never changed.
class Difficulty
{
    std::optional<int64_t> m_started;
    Number m_work;
    uint64_t m_shares{0};
    bool m_overloaded{false};

public:
    void Reset();
    void Accepted(const Number& score);
    void Overloaded() { m_overloaded = true; }
    arith_uint256 Next(const Config& cfg, const arith_uint256& current,
                       const arith_uint256& network, int64_t monotonic_seconds);
};
arith_uint256 BoundTarget(const Config& cfg, const arith_uint256& target, const arith_uint256& network);
int64_t MonotonicSeconds();
} // namespace pool
#endif // BITCOIN_POOL_DIFFICULTY_H
