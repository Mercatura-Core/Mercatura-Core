// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/mercatura_fixedpoint.h>

#include <cassert>
#include <cstdint>

namespace Consensus {
namespace {

/**
 * Internal Q160.96 precision.
 *
 * Q96 is used only as deterministic intermediate precision. Public adaptive
 * controller state remains signed Q16.48.
 */
constexpr unsigned INTERNAL_FRACTION_BITS{96};
constexpr unsigned LN_SERIES_LAST_ODD{95};
constexpr unsigned EXP_SERIES_TERMS{40};

arith_uint256 Wide(uint64_t value)
{
    return arith_uint256{value};
}

arith_uint256 OneQ96()
{
    arith_uint256 value{1};
    value <<= INTERNAL_FRACTION_BITS;
    return value;
}

/**
 * ln(2) in Q160.96:
 *
 * round_nearest_away_from_zero(ln(2) * 2^96)
 *
 * decimal:
 *   54916777467707473351141471128
 *
 * hex:
 *   b17217f7d1cf79abc9e3b398
 */
arith_uint256 Ln2Q96()
{
    arith_uint256 value{0xb17217f7d1cf79abULL};
    value <<= 32;
    value += Wide(0xc9e3b398ULL);
    return value;
}

/**
 * Multiply two nonnegative Q160.96 values.
 *
 * All callers maintain values small enough that the pre-shift product fits
 * within arith_uint256. This is part of the fixed numerical algorithm.
 */
arith_uint256 MultiplyQ96(
    const arith_uint256& a,
    const arith_uint256& b)
{
    return (a * b) >> INTERNAL_FRACTION_BITS;
}

/**
 * Convert positive Q160.96 to positive Q16.48 using round-nearest,
 * exact-half-up.
 */
std::optional<int64_t> RoundPositiveQ96ToQ48(
    const arith_uint256& value_q96)
{
    arith_uint256 rounded{value_q96 >> Q48_FRACTION_BITS};

    const arith_uint256 remainder{
        value_q96 - (rounded << Q48_FRACTION_BITS)};

    arith_uint256 half{1};
    half <<= Q48_FRACTION_BITS - 1;

    if (remainder >= half) {
        rounded += Wide(1);
    }

    if (rounded.bits() > 63) {
        return std::nullopt;
    }

    return static_cast<int64_t>(rounded.GetLow64());
}

/**
 * Absolute magnitude of a signed int64_t without overflowing on INT64_MIN.
 */
uint64_t UnsignedMagnitude(int64_t value)
{
    if (value >= 0) {
        return static_cast<uint64_t>(value);
    }

    return static_cast<uint64_t>(-(value + 1)) + 1;
}

} // namespace

int64_t DivideQ48TowardZero(
    int64_t value,
    int64_t positive_divisor)
{
    assert(positive_divisor > 0);

    // Since the divisor is positive, C++ signed integer division has exactly
    // the required truncate-toward-zero behavior.
    return value / positive_divisor;
}

std::optional<int64_t> LogUint256Q48(
    const arith_uint256& value)
{
    if (value == 0) {
        return std::nullopt;
    }

    const unsigned bits{value.bits()};
    const unsigned exponent{bits - 1};

    /**
     * Preserve the leading 64 significant bits and normalize them into:
     *
     *   mantissa / 2^63  in [1, 2)
     *
     * Discarded low bits are intentionally not consulted. This makes the
     * normalization fixed, deterministic, and independent of host floating
     * point behavior.
     */
    uint64_t mantissa;

    if (bits > 64) {
        mantissa =
            (value >> (bits - 64)).GetLow64();
    } else {
        mantissa =
            value.GetLow64() << (64 - bits);
    }

    constexpr uint64_t ONE_Q63{
        uint64_t{1} << 63};

    assert(mantissa >= ONE_Q63);

    /**
     * atanh form:
     *
     *   ln(x) = 2 * (
     *       y + y^3/3 + y^5/5 + ...
     *   )
     *
     * where:
     *
     *   y = (x - 1) / (x + 1)
     *
     * Since normalized x is in [1,2), y is in [0,1/3), giving rapid and
     * deterministic convergence.
     */
    const arith_uint256 numerator{
        Wide(mantissa - ONE_Q63)};

    const arith_uint256 denominator{
        Wide(mantissa) + Wide(ONE_Q63)};

    const arith_uint256 y{
        (numerator << INTERNAL_FRACTION_BITS) /
        denominator};

    const arith_uint256 y_squared{
        MultiplyQ96(y, y)};

    arith_uint256 term{y};
    arith_uint256 sum{y};

    // Fixed iteration count: odd powers 3 through 95 inclusive.
    for (unsigned odd = 3;
         odd <= LN_SERIES_LAST_ODD;
         odd += 2) {
        term = MultiplyQ96(term, y_squared);
        sum += term / Wide(odd);
    }

    const arith_uint256 normalized_log{
        sum << 1};

    const arith_uint256 exponent_log{
        Ln2Q96() * Wide(exponent)};

    const arith_uint256 total_log{
        exponent_log + normalized_log};

    return RoundPositiveQ96ToQ48(total_log);
}

std::optional<int64_t> LogAmountQ48(CAmount value)
{
    if (value <= 0) {
        return std::nullopt;
    }

    return LogUint256Q48(
        Wide(static_cast<uint64_t>(value)));
}

std::optional<CAmount> RoundPositiveQ96ToAmount(
    const arith_uint256& value_q96)
{
    arith_uint256 whole{
        value_q96 >> INTERNAL_FRACTION_BITS};

    if (whole.bits() > 63) {
        return std::nullopt;
    }

    uint64_t amount{whole.GetLow64()};

    if (amount > static_cast<uint64_t>(MAX_MONEY)) {
        return std::nullopt;
    }

    const arith_uint256 remainder{
        value_q96 -
        (whole << INTERNAL_FRACTION_BITS)};

    arith_uint256 half{1};
    half <<= INTERNAL_FRACTION_BITS - 1;

    // Positive round-nearest, exact half upward.
    if (remainder >= half) {
        if (amount >= static_cast<uint64_t>(MAX_MONEY)) {
            return std::nullopt;
        }
        ++amount;
    }

    return static_cast<CAmount>(amount);
}

std::optional<CAmount> ExpQ48ToAmount(int64_t x_q48)
{
    const bool negative{x_q48 < 0};
    const uint64_t magnitude{
        UnsignedMagnitude(x_q48)};

    arith_uint256 magnitude_q96{
        Wide(magnitude)};

    magnitude_q96 <<= Q48_FRACTION_BITS;

    const arith_uint256 ln2_q96{
        Ln2Q96()};

    int64_t binary_exponent{0};
    arith_uint256 remainder_q96{0};

    if (!negative) {
        const arith_uint256 quotient{
            magnitude_q96 / ln2_q96};

        assert(quotient.bits() <= 63);

        binary_exponent =
            static_cast<int64_t>(
                quotient.GetLow64());

        remainder_q96 =
            magnitude_q96 -
            ln2_q96 *
                Wide(static_cast<uint64_t>(
                    binary_exponent));
    } else {
        /**
         * For negative x choose:
         *
         *   binary_exponent = -ceil(|x| / ln(2))
         *
         * leaving a nonnegative remainder in [0, ln(2)).
         */
        const arith_uint256 count{
            (magnitude_q96 +
             ln2_q96 -
             Wide(1)) /
            ln2_q96};

        assert(count.bits() <= 63);

        const uint64_t count_u64{
            count.GetLow64()};

        assert(count_u64 <=
               static_cast<uint64_t>(
                   INT64_MAX));

        binary_exponent =
            -static_cast<int64_t>(count_u64);

        remainder_q96 =
            ln2_q96 * Wide(count_u64) -
            magnitude_q96;
    }

    assert(remainder_q96 < ln2_q96);

    /**
     * Evaluate exp(remainder) using:
     *
     *   1 + r + r^2/2! + ... + r^40/40!
     *
     * remainder is always in [0, ln(2)), so 40 fixed terms provide ample
     * precision relative to the Q16.48 public state.
     */
    arith_uint256 term{OneQ96()};
    arith_uint256 sum{term};

    for (unsigned n = 1;
         n <= EXP_SERIES_TERMS;
         ++n) {
        term =
            MultiplyQ96(term, remainder_q96);
        term /= Wide(n);
        sum += term;
    }

    /**
     * exp(x) = exp(remainder) * 2^binary_exponent
     *
     * MAX_MONEY is below 2^60 base units. Therefore any nonnegative binary
     * exponent >= 60 is necessarily outside the representable monetary
     * domain and can be rejected before shifting.
     */
    if (binary_exponent >= 60) {
        return MAX_MONEY;
    }

    if (binary_exponent >= 0) {
        arith_uint256 scaled{sum};
        scaled <<= static_cast<unsigned>(
            binary_exponent);

        const auto rounded{
            RoundPositiveQ96ToAmount(scaled)};

        // A finite exp() result above the individual monetary safety ceiling
        // becomes MAX_MONEY. q/r remain untouched by this ceiling.
        if (!rounded) {
            return MAX_MONEY;
        }

        return rounded;
    }

    /**
     * exp(remainder) is in [1,2).
     *
     * With exponent -1, the result is in [0.5,1), which rounds to 1 using
     * the required positive half-up rule.
     *
     * With exponent <= -2, the result is strictly below 0.5 and rounds to 0.
     */
    if (binary_exponent == -1) {
        return CAmount{1};
    }

    return CAmount{0};
}

} // namespace Consensus
