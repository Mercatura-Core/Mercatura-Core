// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

#include <cstdint>
#include <cstdlib>

/** The maximum allowed size for a serialized block, in bytes (only for buffer size limits) */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 4000000;
/** The maximum allowed weight for a block, see BIP 141 (network rule) */
static const unsigned int MAX_BLOCK_WEIGHT = 4000000;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const int64_t MAX_BLOCK_SIGOPS_COST = 80000;
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;

static const int WITNESS_SCALE_FACTOR = 4;

/**
 * Mercatura consensus block-capacity schedule.
 *
 * Capacity begins at 1 MiB and doubles every 1,051,200 block heights.
 * Height 1,051,200 is therefore the first block with a 2 MiB capacity.
 * After ten doublings the capacity is permanently capped at 1024 MiB.
 *
 * These values are serialized bytes, not BIP141 weight units.
 */
static constexpr uint64_t MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES{1ULL << 20};
static constexpr uint64_t MERCATURA_BLOCK_CAPACITY_DOUBLING_INTERVAL{1'051'200};
static constexpr uint64_t MERCATURA_BLOCK_CAPACITY_MAX_DOUBLINGS{10};
static constexpr uint64_t MERCATURA_MAX_BLOCK_CAPACITY_BYTES{
    MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES << MERCATURA_BLOCK_CAPACITY_MAX_DOUBLINGS};

constexpr uint64_t GetMaxBlockCapacityBytes(int64_t height)
{
    if (height <= 0) {
        return MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES;
    }

    uint64_t doublings{
        static_cast<uint64_t>(height) / MERCATURA_BLOCK_CAPACITY_DOUBLING_INTERVAL};

    if (doublings > MERCATURA_BLOCK_CAPACITY_MAX_DOUBLINGS) {
        doublings = MERCATURA_BLOCK_CAPACITY_MAX_DOUBLINGS;
    }

    return MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES << doublings;
}

static const size_t MIN_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 60; // 60 is the lower bound for the size of a valid serialized CTransaction
static const size_t MIN_SERIALIZABLE_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 10; // 10 is the lower bound for the size of a serialized CTransaction

/** Flags for nSequence and nLockTime locks */
/** Interpret sequence numbers as relative lock-time constraints. */
static constexpr unsigned int LOCKTIME_VERIFY_SEQUENCE = (1 << 0);

/**
 * Maximum number of seconds that a block at a configured BIP94 timewarp
 * boundary may be earlier than the preceding block.
 */
static constexpr int64_t MAX_TIMEWARP = 600;

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
