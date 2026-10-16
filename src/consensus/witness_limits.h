// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_CONSENSUS_WITNESS_LIMITS_H
#define BITCOIN_CONSENSUS_WITNESS_LIMITS_H

#include <consensus/consensus.h>
#include <consensus/params.h>
#include <serialize.h>

#include <optional>

namespace Consensus {
/** Protocol constants, independent of interpreter limits and host object sizes. */
inline constexpr uint64_t MAX_WITNESS_STACK_ITEMS{1003};
inline constexpr uint64_t MAX_WITNESS_ITEM_BYTES{33'554'432};
inline constexpr uint64_t WITNESS_ITEM_CAPACITY_DIVISOR{256};
static_assert(MAX_WITNESS_ITEM_BYTES == MAX_SIZE);

constexpr uint64_t GetMaxBlockWitnessItems(const Params& params, int64_t height)
{
    return GetMaxBlockCapacityBytes(params, height) / WITNESS_ITEM_CAPACITY_DIVISOR;
}
inline constexpr uint64_t MAX_BLOCK_WITNESS_ITEMS{
    MERCATURA_MAX_BLOCK_CAPACITY_BYTES / WITNESS_ITEM_CAPACITY_DIVISOR};

inline bool WitnessLimitsActive(const Params& params, int64_t height)
{
    return params.nWitnessResourceHeight >= 0 && height >= params.nWitnessResourceHeight;
}

template <typename Stack>
const char* WitnessStackViolation(const Stack& stack)
{
    if (stack.size() > MAX_WITNESS_STACK_ITEMS) return "bad-witness-stack-count";
    for (const auto& item : stack) {
        if (item.size() > MAX_WITNESS_ITEM_BYTES) return "bad-witness-item-size";
    }
    return nullptr;
}
} // namespace Consensus

/** One owner per parse attempt. Streams share a pointer, never a counter copy. */
struct WitnessParseBudget {
    bool enabled;
    uint64_t remaining;

    static WitnessParseBudget ForBlock(const Consensus::Params& params, int64_t height)
    {
        return {Consensus::WitnessLimitsActive(params, height), Consensus::GetMaxBlockWitnessItems(params, height)};
    }
    static WitnessParseBudget ForInspection(const Consensus::Params& params)
    {
        // A future activation must not retroactively restrict historical inspection.
        return {params.nWitnessResourceHeight == 0, Consensus::MAX_BLOCK_WITNESS_ITEMS};
    }
};

struct WitnessParseParams {
    WitnessParseBudget* budget;
    SER_PARAMS_OPFUNC
};

template <typename Stream>
WitnessParseBudget* GetWitnessParseBudget(const Stream& stream)
{
    if constexpr (requires { stream.template GetParamsPtr<WitnessParseParams>(); }) {
        if (const auto* params = stream.template GetParamsPtr<WitnessParseParams>()) return params->budget;
    }
    return nullptr;
}

/** Roll back counters on failed/retried nested parses, without refunding successful reads. */
class WitnessParseGuard {
    WitnessParseBudget* const m_budget;
    const uint64_t m_remaining;
    bool m_committed{false};
public:
    explicit WitnessParseGuard(WitnessParseBudget* budget)
        : m_budget{budget}, m_remaining{budget ? budget->remaining : 0} {}
    WitnessParseGuard(const WitnessParseGuard&) = delete;
    ~WitnessParseGuard() { if (m_budget && !m_committed) m_budget->remaining = m_remaining; }
    void Commit() { m_committed = true; }
};

struct WitnessStackFormatter {
    template <typename Stream, typename Stack>
    void Ser(Stream& stream, const Stack& stack) { ::Serialize(stream, stack); }

    template <typename Stream, typename Stack>
    void Unser(Stream& stream, Stack& stack)
    {
        auto* budget = GetWitnessParseBudget(stream);
        if (!budget || !budget->enabled) {
            ::Unserialize(stream, stack);
            return;
        }
        WitnessParseGuard guard{budget};
        const uint64_t count = ReadCompactSize(stream);
        if (count > Consensus::MAX_WITNESS_STACK_ITEMS) throw std::ios_base::failure("bad-witness-stack-count");
        if (count > budget->remaining) throw std::ios_base::failure("bad-blk-witness-items");
        budget->remaining -= count;
        // Neither reserve nor emplace is reachable before both count checks.
        stack.clear();
        stack.reserve(count);
        for (uint64_t i{0}; i < count; ++i) {
            stack.emplace_back();
            ::Unserialize(stream, stack.back()); // Existing canonical 32 MiB byte-vector bound.
        }
        guard.Commit();
    }
};

#endif // BITCOIN_CONSENSUS_WITNESS_LIMITS_H
