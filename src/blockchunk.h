// Copyright (c) 2026 The Mercatura developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCKCHUNK_H
#define BITCOIN_BLOCKCHUNK_H

#include <consensus/consensus.h>
#include <serialize.h>
#include <uint256.h>

#include <cstdint>
#include <vector>

/**
 * Maximum payload carried by one Mercatura large-block chunk message.
 *
 * Keeping chunks at 1 MiB allows both V1 and BIP324/V2 transports to retain
 * their existing bounded-message protections while Mercatura's consensus
 * block capacity can grow far beyond a single transport message.
 */
static constexpr uint64_t MERCATURA_BLOCK_CHUNK_SIZE{1ULL << 20};

static_assert(
    MERCATURA_MAX_BLOCK_CAPACITY_BYTES % MERCATURA_BLOCK_CHUNK_SIZE == 0,
    "Mercatura maximum block capacity must divide evenly into chunk size");

static constexpr uint64_t MERCATURA_MAX_BLOCK_CHUNKS{
    MERCATURA_MAX_BLOCK_CAPACITY_BYTES / MERCATURA_BLOCK_CHUNK_SIZE
};

static_assert(MERCATURA_MAX_BLOCK_CHUNKS == 1024);

/**
 * Return the number of 1 MiB chunks required for total_size serialized bytes.
 *
 * This formulation avoids total_size + chunk_size - 1 overflow.
 */
constexpr uint64_t GetMercaturaBlockChunkCount(uint64_t total_size)
{
    if (total_size == 0) return 0;

    return total_size / MERCATURA_BLOCK_CHUNK_SIZE +
           (total_size % MERCATURA_BLOCK_CHUNK_SIZE != 0);
}

/**
 * Return the expected payload size of chunk_index.
 *
 * Returns zero when chunk_index is outside the transfer.
 */
constexpr uint64_t GetMercaturaBlockChunkPayloadSize(
    uint64_t total_size,
    uint64_t chunk_index)
{
    const uint64_t chunk_count{GetMercaturaBlockChunkCount(total_size)};

    if (chunk_index >= chunk_count) return 0;

    if (chunk_index + 1 < chunk_count) {
        return MERCATURA_BLOCK_CHUNK_SIZE;
    }

    const uint64_t remainder{total_size % MERCATURA_BLOCK_CHUNK_SIZE};
    return remainder == 0 ? MERCATURA_BLOCK_CHUNK_SIZE : remainder;
}

/**
 * Metadata announcing how a requested serialized block will be transferred.
 *
 * Height-dependent consensus capacity is checked by net_processing once the
 * receiver resolves block_hash to the already-known requested block header.
 */
struct MercaturaBlockMeta {
    uint256 block_hash;
    uint64_t total_size{0};
    uint32_t chunk_size{0};
    uint32_t chunk_count{0};

    SERIALIZE_METHODS(MercaturaBlockMeta, obj)
    {
        READWRITE(
            obj.block_hash,
            obj.total_size,
            obj.chunk_size,
            obj.chunk_count);
    }
};

/** Request exactly one chunk of a previously announced large block. */
struct MercaturaBlockChunkRequest {
    uint256 block_hash;
    uint32_t chunk_index{0};

    SERIALIZE_METHODS(MercaturaBlockChunkRequest, obj)
    {
        READWRITE(obj.block_hash, obj.chunk_index);
    }
};

/** One bounded piece of a serialized witness-inclusive block. */
struct MercaturaBlockChunk {
    uint256 block_hash;
    uint32_t chunk_index{0};
    std::vector<unsigned char> data;

    SERIALIZE_METHODS(MercaturaBlockChunk, obj)
    {
        READWRITE(obj.block_hash, obj.chunk_index, obj.data);
    }
};

/**
 * Perform transport-independent structural validation of block metadata.
 *
 * The stricter height-dependent limit is deliberately not checked here.
 */
constexpr bool IsMercaturaBlockMetaStructurallyValid(
    const MercaturaBlockMeta& meta)
{
    if (meta.total_size == 0 ||
        meta.total_size > MERCATURA_MAX_BLOCK_CAPACITY_BYTES) {
        return false;
    }

    if (meta.chunk_size != MERCATURA_BLOCK_CHUNK_SIZE) {
        return false;
    }

    const uint64_t expected_chunks{
        GetMercaturaBlockChunkCount(meta.total_size)
    };

    return expected_chunks > 0 &&
           expected_chunks <= MERCATURA_MAX_BLOCK_CHUNKS &&
           meta.chunk_count == expected_chunks;
}

#endif // BITCOIN_BLOCKCHUNK_H
