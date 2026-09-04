// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_MERCATURA_PQDERIVE_H
#define BITCOIN_CRYPTO_MERCATURA_PQDERIVE_H

#include <uint256.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

static constexpr size_t MERCATURA_PQ_MASTER_SEED_SIZE = 32;
static constexpr size_t MERCATURA_PQ_CHILD_SEED_SIZE = 32;

/**
 * Derive a deterministic ML-DSA-65 child seed for Mercatura PQ Wallet v1.
 *
 * branch:
 *   0 = external / receive
 *   1 = internal / change
 *
 * Returns std::nullopt for any other branch value.
 */
std::optional<std::array<unsigned char, MERCATURA_PQ_CHILD_SEED_SIZE>>
DeriveMercaturaPQChildSeed(
    std::span<const unsigned char, MERCATURA_PQ_MASTER_SEED_SIZE> master_seed,
    const uint256& genesis_hash,
    uint32_t account,
    uint8_t branch,
    uint32_t index);

#endif // BITCOIN_CRYPTO_MERCATURA_PQDERIVE_H