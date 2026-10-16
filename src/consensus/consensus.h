// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

#include <consensus/params.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

/** The maximum allowed size for a serialized block, in bytes (only for buffer size limits) */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 4000000;
/** The maximum allowed weight for a block, see BIP 141 (network rule) */
static const unsigned int MAX_BLOCK_WEIGHT = 4000000;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const int64_t MAX_BLOCK_SIGOPS_COST = 80000;

// Mercatura PQ Authorization v1 verification cost.
//
// Benchmarked on the portable ML-DSA-65 backend at approximately
// 3.2x the cost of one secp256k1 ECDSA/Schnorr verification.
// Round upward to four inherited sigop-cost units so the existing
// block verification-work budget remains conservative.
static constexpr int64_t MERCATURA_PQ_SIGOPS_COST = 4;
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;

static const int WITNESS_SCALE_FACTOR = 4;

/** Mercatura full-serialized-byte capacity, without a witness discount.
 * Mainnet/regtest start at 4 MiB and reach 1024 MiB after eight doublings.
 * Public-testnet/signet retain 1 MiB and ten doublings. The absolute ceiling
 * is independent of the initial capacity, preventing an accidental 4 GiB cap.
 */
static constexpr uint64_t MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES{4ULL << 20};
static constexpr uint64_t MERCATURA_LEGACY_INITIAL_BLOCK_CAPACITY_BYTES{1ULL << 20};
static constexpr uint64_t MERCATURA_BLOCK_CAPACITY_DOUBLING_INTERVAL{1'051'200};
static constexpr uint64_t MERCATURA_BLOCK_CAPACITY_MAX_DOUBLINGS{8};
static constexpr uint64_t MERCATURA_LEGACY_BLOCK_CAPACITY_MAX_DOUBLINGS{10};
static constexpr uint64_t MERCATURA_MAX_BLOCK_CAPACITY_BYTES{1ULL << 30};
static_assert((MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES << MERCATURA_BLOCK_CAPACITY_MAX_DOUBLINGS) == MERCATURA_MAX_BLOCK_CAPACITY_BYTES);

constexpr uint64_t GetMaxBlockCapacityBytes(const Consensus::Params& params, int64_t height)
{
    uint64_t capacity{std::min(params.nInitialBlockCapacityBytes, MERCATURA_MAX_BLOCK_CAPACITY_BYTES)};
    // At most ten iterations for either configured schedule, even at INT64_MAX.
    const uint64_t doublings{height > 0 ? std::min<uint64_t>(
        static_cast<uint64_t>(height) / MERCATURA_BLOCK_CAPACITY_DOUBLING_INTERVAL,
        MERCATURA_LEGACY_BLOCK_CAPACITY_MAX_DOUBLINGS) : 0};
    for (uint64_t i{0}; i < doublings && capacity < MERCATURA_MAX_BLOCK_CAPACITY_BYTES; ++i) {
        capacity = std::min(capacity * 2, MERCATURA_MAX_BLOCK_CAPACITY_BYTES);
    }
    return capacity;
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
