// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_MERCATURA_CONTROLLER_H
#define BITCOIN_CONSENSUS_MERCATURA_CONTROLLER_H

#include <arith_uint256.h>
#include <consensus/amount.h>

#include <cstdint>
#include <optional>

class CBlockIndex;

namespace Consensus {

inline constexpr int MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT{311'041};

inline constexpr int64_t MERCATURA_SHORT_EMA_DIVISOR{4'033};
inline constexpr int64_t MERCATURA_LONG_EMA_DIVISOR{103'681};
inline constexpr int64_t MERCATURA_CONTROLLER_DIVISOR{840'960};

inline constexpr CAmount MERCATURA_PERPETUAL_SUBSIDY_FLOOR{23'782};

/**
 * Emission command for a block before that block's work signal is observed.
 *
 * subsidy is the protocol subsidy for this height. Fees are never included.
 *
 * During bootstrap, controller_initialized is false and q/r are zero.
 * Beginning at height 311041, q/r are consensus controller state.
 */
struct McaEmissionCommand
{
    CAmount subsidy{0};
    int64_t q_q48{0};
    int64_t r_q48{0};
    bool controller_initialized{false};
};

/**
 * Branch-local adaptive-emission state after accepting one block.
 *
 * s_q48 and l_q48 include this block's work/subsidy observation.
 * q_q48 and r_q48 are the command state used for this block's subsidy.
 */
struct McaEmissionState
{
    int height{0};

    int64_t s_q48{0};
    int64_t l_q48{0};

    int64_t q_q48{0};
    int64_t r_q48{0};

    CAmount subsidy{0};

    bool controller_initialized{false};
};

/**
 * Derive the protocol emission command for height solely from its parent.
 *
 * For height 1, parent must be nullptr.
 * For every later height, parent->height must equal height - 1.
 *
 * This function does not mutate any state.
 */
std::optional<McaEmissionCommand> GetMcaEmissionCommand(
    const McaEmissionState* parent,
    int height);

/**
 * Accept one block into the pure emission state machine.
 *
 * block_work is Bitcoin Core GetBlockProof(B_h).
 * protocol_subsidy is the protocol subsidy R_h in base units; fees and any
 * miner underclaim are deliberately excluded from the controller signal.
 *
 * The supplied subsidy must exactly match GetMcaEmissionCommand().
 */
std::optional<McaEmissionState> AdvanceMcaEmissionState(
    const McaEmissionState* parent,
    int height,
    const arith_uint256& block_work,
    CAmount protocol_subsidy);

/**
 * Derive the complete state for one block solely from ancestry, height, and
 * GetBlockProof-compatible work.
 *
 * The protocol subsidy is obtained internally from GetMcaEmissionCommand().
 * This is the shared deterministic path used by normal header admission and
 * startup block-index reconstruction.
 */
std::optional<McaEmissionState> DeriveMcaEmissionState(
    const McaEmissionState* parent,
    int height,
    const arith_uint256& block_work);

/**
 * Return the protocol subsidy belonging to an already-indexed Mercatura
 * block.
 *
 * Genesis height 0 is intentionally outside the Mercatura emission state
 * machine and therefore has no protocol subsidy here.
 */
std::optional<CAmount> GetMcaBlockSubsidy(
    const CBlockIndex& block);

/**
 * Return the protocol subsidy commanded for the child of parent.
 *
 * The result depends on the parent's branch-local emission state, not merely
 * on height. Genesis may be supplied as parent for block 1 even though
 * genesis itself has no Mercatura emission state.
 */
std::optional<CAmount> GetNextMcaBlockSubsidy(
    const CBlockIndex& parent);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_MERCATURA_CONTROLLER_H
