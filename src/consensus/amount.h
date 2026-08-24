// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_AMOUNT_H
#define BITCOIN_CONSENSUS_AMOUNT_H

#include <cstdint>

/** Amount in Mercatura base units (can be negative). */
typedef int64_t CAmount;

/** Number of base units in one MCA. */
static constexpr CAmount COIN = 100;

/** One indivisible Mercatura Cent: 0.01 MCA. */
static constexpr CAmount CENT = 1;

/** No monetary value larger than this is valid.
 *
 * This is a consensus-critical sanity bound, not a total supply cap.
 * Mercatura preserves Bitcoin Core's existing raw integer safety ceiling
 * while using 100 base units per MCA.
 */
static constexpr CAmount MAX_MONEY = 2'100'000'000'000'000;
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif // BITCOIN_CONSENSUS_AMOUNT_H
