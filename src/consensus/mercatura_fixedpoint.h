// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_MERCATURA_FIXEDPOINT_H
#define BITCOIN_CONSENSUS_MERCATURA_FIXEDPOINT_H

#include <arith_uint256.h>
#include <consensus/amount.h>

#include <cstdint>
#include <optional>

namespace Consensus {

/**
 * Mercatura adaptive-emission fixed-point format.
 *
 * Signed Q16.48:
 *   16 integer/sign bits
 *   48 fractional bits
 *
 * Consensus execution must not use floating point.
 */
inline constexpr int Q48_FRACTION_BITS{48};
inline constexpr int64_t Q48_ONE{int64_t{1} << Q48_FRACTION_BITS};

/**
 * Consensus constants.
 *
 * Irrational constants are quantized directly to signed Q16.48 using
 * round-to-nearest with an exact half rounded away from zero.
 */
inline constexpr int64_t MERCATURA_LN2_Q48{
    195'103'586'505'167};

inline constexpr int64_t MERCATURA_LN_EDGE_SUBSIDY_Q48{
    4'132'578'665'435'046};

inline constexpr int64_t MERCATURA_D_PLUS_Q48{
    298'750'599};

inline constexpr int64_t MERCATURA_D_MINUS_Q48{
    -536'170'157};

/**
 * Divide a signed fixed-point integer by a positive scalar using
 * truncation toward zero.
 *
 * The Q16.48 scale is unchanged.
 */
int64_t DivideQ48TowardZero(int64_t value, int64_t positive_divisor);

/**
 * Deterministic natural logarithm of a positive integer, returned as Q16.48.
 *
 * The implementation uses fixed-width integer arithmetic, deterministic
 * range reduction, and a fixed-iteration series. Zero has no logarithm and
 * returns std::nullopt.
 */
std::optional<int64_t> LogUint256Q48(const arith_uint256& value);

/**
 * Deterministic natural logarithm of a positive CAmount in base units.
 */
std::optional<int64_t> LogAmountQ48(CAmount value);

/**
 * Convert an unsigned Q160.96 value to CAmount using positive
 * round-to-nearest, exact-half-up.
 *
 * Returns std::nullopt if the rounded result exceeds MAX_MONEY.
 *
 * This function is exposed so the consensus rounding rule can be tested
 * directly and permanently.
 */
std::optional<CAmount> RoundPositiveQ96ToAmount(
    const arith_uint256& value_q96);

/**
 * Deterministically evaluate exp(x), where x is signed Q16.48, and convert
 * the positive result to integer base units using round-nearest,
 * exact-half-up.
 *
 * If the mathematically rounded result would exceed MAX_MONEY, returns
 * MAX_MONEY. This is an implementation-safety ceiling on one subsidy, not an
 * aggregate supply cap and not a clamp on controller state.
 */
std::optional<CAmount> ExpQ48ToAmount(int64_t x_q48);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_MERCATURA_FIXEDPOINT_H
