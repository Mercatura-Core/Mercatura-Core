// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_MERCATURA_EMISSION_H
#define BITCOIN_CONSENSUS_MERCATURA_EMISSION_H

#include <consensus/amount.h>

namespace Consensus {

inline constexpr int MERCATURA_BOOTSTRAP_FIRST_HEIGHT{1};
inline constexpr int MERCATURA_BOOTSTRAP_HALF_BLOCKS{155'520};
inline constexpr int MERCATURA_BOOTSTRAP_LAST_HEIGHT{311'040};

inline constexpr CAmount MERCATURA_BOOTSTRAP_EDGE_SUBSIDY{2'378'234};
inline constexpr CAmount MERCATURA_BOOTSTRAP_HALF_ISSUANCE{1'000'000'000'000};
inline constexpr CAmount MERCATURA_BOOTSTRAP_TOTAL_ISSUANCE{2'000'000'000'000};

/**
 * Return bootstrap issuance through height, in Mercatura base units.
 *
 * Heights <= 0 return zero. Heights at or beyond the final bootstrap block
 * return the complete bootstrap issuance.
 */
CAmount GetMercaturaBootstrapCumulativeIssuance(int height);

/**
 * Return the bootstrap subsidy for height, in Mercatura base units.
 *
 * Heights outside [1, 311040] return zero. Adaptive emission beginning at
 * height 311041 is intentionally outside this bootstrap-only function.
 */
CAmount GetMercaturaBootstrapSubsidy(int height);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_MERCATURA_EMISSION_H
