// Copyright (c) 2026 The Mercatura developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blockchunk.h>
#include <consensus/consensus.h>
#include <net.h>
#include <streams.h>

#include <boost/test/unit_test.hpp>
#include <test/util/setup_common.h>

#include <cstdint>
#include <limits>

BOOST_FIXTURE_TEST_SUITE(blockchunk_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(block_chunk_constants)
{
    BOOST_CHECK_EQUAL(MERCATURA_BLOCK_CHUNK_SIZE, 1ULL << 20);
    BOOST_CHECK_EQUAL(MERCATURA_MAX_BLOCK_CHUNKS, 1024);

    BOOST_CHECK_EQUAL(
        MERCATURA_MAX_BLOCK_CHUNKS * MERCATURA_BLOCK_CHUNK_SIZE,
        MERCATURA_MAX_BLOCK_CAPACITY_BYTES);
}

BOOST_AUTO_TEST_CASE(block_chunk_count_boundaries)
{
    BOOST_CHECK_EQUAL(GetMercaturaBlockChunkCount(0), 0);
    BOOST_CHECK_EQUAL(GetMercaturaBlockChunkCount(1), 1);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkCount(MERCATURA_BLOCK_CHUNK_SIZE - 1),
        1);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkCount(MERCATURA_BLOCK_CHUNK_SIZE),
        1);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkCount(MERCATURA_BLOCK_CHUNK_SIZE + 1),
        2);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkCount(4 * MERCATURA_BLOCK_CHUNK_SIZE),
        4);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkCount(16 * MERCATURA_BLOCK_CHUNK_SIZE),
        16);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkCount(128 * MERCATURA_BLOCK_CHUNK_SIZE),
        128);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkCount(MERCATURA_MAX_BLOCK_CAPACITY_BYTES),
        MERCATURA_MAX_BLOCK_CHUNKS);
}

BOOST_AUTO_TEST_CASE(block_chunk_payload_sizes)
{
    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkPayloadSize(1, 0),
        1);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkPayloadSize(
            MERCATURA_BLOCK_CHUNK_SIZE,
            0),
        MERCATURA_BLOCK_CHUNK_SIZE);

    const uint64_t two_chunk_size{
        MERCATURA_BLOCK_CHUNK_SIZE + 123
    };

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkPayloadSize(two_chunk_size, 0),
        MERCATURA_BLOCK_CHUNK_SIZE);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkPayloadSize(two_chunk_size, 1),
        123);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkPayloadSize(two_chunk_size, 2),
        0);

    const uint64_t exact_two_chunks{
        2 * MERCATURA_BLOCK_CHUNK_SIZE
    };

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkPayloadSize(exact_two_chunks, 0),
        MERCATURA_BLOCK_CHUNK_SIZE);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkPayloadSize(exact_two_chunks, 1),
        MERCATURA_BLOCK_CHUNK_SIZE);

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkPayloadSize(exact_two_chunks, 2),
        0);
}

BOOST_AUTO_TEST_CASE(block_meta_structural_validation)
{
    MercaturaBlockMeta meta;

    meta.total_size = MERCATURA_BLOCK_CHUNK_SIZE;
    meta.chunk_size = MERCATURA_BLOCK_CHUNK_SIZE;
    meta.chunk_count = 1;

    BOOST_CHECK(IsMercaturaBlockMetaStructurallyValid(meta));

    meta.total_size = MERCATURA_BLOCK_CHUNK_SIZE + 1;
    meta.chunk_count = 2;

    BOOST_CHECK(IsMercaturaBlockMetaStructurallyValid(meta));

    meta.total_size = MERCATURA_MAX_BLOCK_CAPACITY_BYTES;
    meta.chunk_count = MERCATURA_MAX_BLOCK_CHUNKS;

    BOOST_CHECK(IsMercaturaBlockMetaStructurallyValid(meta));

    meta.total_size = 0;
    BOOST_CHECK(!IsMercaturaBlockMetaStructurallyValid(meta));

    meta.total_size = MERCATURA_MAX_BLOCK_CAPACITY_BYTES + 1;
    BOOST_CHECK(!IsMercaturaBlockMetaStructurallyValid(meta));

    meta.total_size = MERCATURA_BLOCK_CHUNK_SIZE;
    meta.chunk_count = 1;
    meta.chunk_size = MERCATURA_BLOCK_CHUNK_SIZE / 2;

    BOOST_CHECK(!IsMercaturaBlockMetaStructurallyValid(meta));

    meta.chunk_size = MERCATURA_BLOCK_CHUNK_SIZE;
    meta.chunk_count = 2;

    BOOST_CHECK(!IsMercaturaBlockMetaStructurallyValid(meta));
}


BOOST_AUTO_TEST_CASE(block_chunk_count_overflow_safety)
{
    const uint64_t maximum{
        std::numeric_limits<uint64_t>::max()
    };

    const uint64_t expected{
        maximum / MERCATURA_BLOCK_CHUNK_SIZE +
        (maximum % MERCATURA_BLOCK_CHUNK_SIZE != 0)
    };

    BOOST_CHECK_EQUAL(
        GetMercaturaBlockChunkCount(maximum),
        expected);

    BOOST_CHECK_GT(
        GetMercaturaBlockChunkCount(maximum),
        MERCATURA_MAX_BLOCK_CHUNKS);
}

BOOST_AUTO_TEST_CASE(block_chunk_wire_roundtrip)
{
    {
        MercaturaBlockMeta original;
        original.total_size =
            MERCATURA_BLOCK_CHUNK_SIZE + 123;
        original.chunk_size =
            static_cast<uint32_t>(
                MERCATURA_BLOCK_CHUNK_SIZE);
        original.chunk_count = 2;

        DataStream stream{};
        stream << original;

        BOOST_CHECK_EQUAL(stream.size(), 48);

        MercaturaBlockMeta decoded;
        stream >> decoded;

        BOOST_CHECK(
            decoded.block_hash ==
            original.block_hash);
        BOOST_CHECK_EQUAL(
            decoded.total_size,
            original.total_size);
        BOOST_CHECK_EQUAL(
            decoded.chunk_size,
            original.chunk_size);
        BOOST_CHECK_EQUAL(
            decoded.chunk_count,
            original.chunk_count);
        BOOST_CHECK(stream.empty());
    }

    {
        MercaturaBlockChunkRequest original;
        original.chunk_index = 17;

        DataStream stream{};
        stream << original;

        BOOST_CHECK_EQUAL(stream.size(), 36);

        MercaturaBlockChunkRequest decoded;
        stream >> decoded;

        BOOST_CHECK(
            decoded.block_hash ==
            original.block_hash);
        BOOST_CHECK_EQUAL(
            decoded.chunk_index,
            original.chunk_index);
        BOOST_CHECK(stream.empty());
    }

    {
        MercaturaBlockChunk original;
        original.chunk_index = 3;
        original.data = {
            0x00,
            0x01,
            0xfe,
            0xff,
        };

        DataStream stream{};
        stream << original;

        // 32-byte hash + 4-byte index +
        // 1-byte CompactSize + 4 data bytes.
        BOOST_CHECK_EQUAL(stream.size(), 41);

        MercaturaBlockChunk decoded;
        stream >> decoded;

        BOOST_CHECK(
            decoded.block_hash ==
            original.block_hash);
        BOOST_CHECK_EQUAL(
            decoded.chunk_index,
            original.chunk_index);
        BOOST_CHECK_EQUAL_COLLECTIONS(
            decoded.data.begin(),
            decoded.data.end(),
            original.data.begin(),
            original.data.end());
        BOOST_CHECK(stream.empty());
    }
}

BOOST_AUTO_TEST_CASE(block_chunk_wire_payload_is_bounded)
{
    MercaturaBlockChunk chunk;
    chunk.chunk_index = 0;
    chunk.data.resize(
        static_cast<size_t>(
            MERCATURA_BLOCK_CHUNK_SIZE),
        0x5a);

    DataStream stream{};
    stream << chunk;

    // 32-byte hash + 4-byte index +
    // 5-byte CompactSize encoding for a 1 MiB vector +
    // exactly 1 MiB of chunk payload.
    const uint64_t expected_wire_size{
        MERCATURA_BLOCK_CHUNK_SIZE + 41
    };

    BOOST_CHECK_EQUAL(
        stream.size(),
        expected_wire_size);

    // The Mercatura chunk message must remain bounded by Bitcoin Core's
    // ordinary single-message transport ceiling. We deliberately do not
    // increase that generic limit for large Mercatura blocks.
    BOOST_CHECK_LT(
        stream.size(),
        MAX_PROTOCOL_MESSAGE_LENGTH);
}

BOOST_AUTO_TEST_SUITE_END()
