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

/** Consensus-critical monetary value safety bound.
 *
 * This is NOT Mercatura's total or eventual supply cap. Mercatura has no
 * fixed maximum aggregate supply. MAX_MONEY exists solely as a finite
 * sanity bound for monetary values and consensus arithmetic.
 *
 * This value matches the inherited fixed-point parser's positive upper
 * bound and remains safely representable by CAmount and CompressAmount.
 */
static constexpr CAmount MAX_MONEY = 999'999'999'999'999'999;
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif // BITCOIN_CONSENSUS_AMOUNT_H
