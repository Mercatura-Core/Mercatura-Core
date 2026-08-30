// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/mercatura_emission.h>

#include <arith_uint256.h>

#include <cassert>
#include <cstdint>

namespace Consensus {
namespace {

constexpr uint64_t BOOTSTRAP_DOMAIN_DENOMINATOR{
    MERCATURA_BOOTSTRAP_HALF_BLOCKS - 1};
constexpr uint64_t BOOTSTRAP_AMPLITUDE_NUMERATOR{1'969'178'276};
constexpr uint64_t BOOTSTRAP_AMPLITUDE_DENOMINATOR{243};

static_assert(MERCATURA_BOOTSTRAP_LAST_HEIGHT ==
              2 * MERCATURA_BOOTSTRAP_HALF_BLOCKS);
static_assert(MERCATURA_BOOTSTRAP_TOTAL_ISSUANCE ==
              2 * MERCATURA_BOOTSTRAP_HALF_ISSUANCE);

/**
 * Convert a native unsigned integer to the deterministic wide-arithmetic type
 * used by Bitcoin Core.
 */
arith_uint256 Wide(uint64_t value)
{
    return arith_uint256{value};
}

/**
 * Exact cumulative issuance for blocks 1 through 155520.
 *
 * Let:
 *
 *   m = block_height - 1
 *   d = 155519
 *
 * The smoothstep term is:
 *
 *   Q(m/d) =
 *     (6*m^5 - 15*d*m^4 + 10*d^2*m^3) / d^5
 *
 * Rather than summing every previous block, the cumulative polynomial is
 * evaluated in O(1) time using exact closed-form integer power sums.
 *
 * The exact amplitude is:
 *
 *   1969178276 / 243 base units
 *
 * Only the final rational cumulative issuance is floored. Individual ideal
 * block rewards are never independently rounded.
 *
 * Maximum intermediates for the valid first-half height range are below
 * 2^135, comfortably inside arith_uint256.
 */
CAmount FirstHalfCumulativeIssuance(int height)
{
    if (height <= 0) return 0;

    assert(height <= MERCATURA_BOOTSTRAP_HALF_BLOCKS);

    // At height 1, m=0 and the smoothstep contribution is exactly zero.
    // Handling it directly avoids an unsigned "-1" term in the power-sum
    // formula below.
    if (height == 1) {
        return MERCATURA_BOOTSTRAP_EDGE_SUBSIDY;
    }

    const arith_uint256 n{static_cast<uint64_t>(height - 1)};
    const arith_uint256 one{1};
    const arith_uint256 d{BOOTSTRAP_DOMAIN_DENOMINATOR};

    const arith_uint256 n2{n * n};
    const arith_uint256 np1{n + one};

    // sum(m^3), m=1..n = [n(n+1)/2]^2
    const arith_uint256 triangular{(n * np1) / Wide(2)};
    const arith_uint256 sum3{triangular * triangular};

    // sum(m^4), m=1..n
    // = n(n+1)(2n+1)(3n^2+3n-1) / 30
    const arith_uint256 sum4{
        (n * np1 *
         (Wide(2) * n + one) *
         (Wide(3) * n2 + Wide(3) * n - one)) /
        Wide(30)};

    // sum(m^5), m=1..n
    // = n^2(n+1)^2(2n^2+2n-1) / 12
    const arith_uint256 sum5{
        (n2 * np1 * np1 *
         (Wide(2) * n2 + Wide(2) * n - one)) /
        Wide(12)};

    const arith_uint256 d2{d * d};
    const arith_uint256 d4{d2 * d2};
    const arith_uint256 d5{d4 * d};

    // Sum of smoothstep numerators:
    //
    //   6*sum(m^5) - 15*d*sum(m^4) + 10*d^2*sum(m^3)
    //
    // Keep the unsigned intermediate nonnegative explicitly.
    const arith_uint256 positive{
        Wide(6) * sum5 + Wide(10) * d2 * sum3};
    const arith_uint256 negative{
        Wide(15) * d * sum4};

    assert(positive >= negative);

    const arith_uint256 smoothstep_numerator{
        positive - negative};

    const arith_uint256 denominator{
        Wide(BOOTSTRAP_AMPLITUDE_DENOMINATOR) * d5};

    const arith_uint256 base_numerator{
        Wide(static_cast<uint64_t>(height)) *
        Wide(static_cast<uint64_t>(MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)) *
        denominator};

    const arith_uint256 curve_numerator{
        Wide(BOOTSTRAP_AMPLITUDE_NUMERATOR) *
        smoothstep_numerator};

    const arith_uint256 cumulative{
        (base_numerator + curve_numerator) / denominator};

    assert(cumulative.bits() <= 63);

    const CAmount result{
        static_cast<CAmount>(cumulative.GetLow64())};

    assert(result >= 0);
    assert(result <= MERCATURA_BOOTSTRAP_HALF_ISSUANCE);

    return result;
}

} // namespace

CAmount GetMercaturaBootstrapCumulativeIssuance(int height)
{
    if (height <= 0) {
        return 0;
    }

    if (height <= MERCATURA_BOOTSTRAP_HALF_BLOCKS) {
        return FirstHalfCumulativeIssuance(height);
    }

    if (height >= MERCATURA_BOOTSTRAP_LAST_HEIGHT) {
        return MERCATURA_BOOTSTRAP_TOTAL_ISSUANCE;
    }

    // Consensus rule: the second half exactly mirrors the already-quantized
    // integer rewards of the first half.
    //
    // If k blocks remain after height h, subtract the cumulative issuance of
    // the first k bootstrap blocks from the exact final bootstrap total.
    return MERCATURA_BOOTSTRAP_TOTAL_ISSUANCE -
           FirstHalfCumulativeIssuance(
               MERCATURA_BOOTSTRAP_LAST_HEIGHT - height);
}

CAmount GetMercaturaBootstrapSubsidy(int height)
{
    if (height < MERCATURA_BOOTSTRAP_FIRST_HEIGHT ||
        height > MERCATURA_BOOTSTRAP_LAST_HEIGHT) {
        return 0;
    }

    return GetMercaturaBootstrapCumulativeIssuance(height) -
           GetMercaturaBootstrapCumulativeIssuance(height - 1);
}

} // namespace Consensus
