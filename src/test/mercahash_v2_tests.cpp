// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <boost/test/unit_test.hpp>
#include <crypto/mercahash.h>
#include <util/strencodings.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

BOOST_AUTO_TEST_SUITE(mercahash_v2_tests)

// Raw output byte order; header patterns and the frozen configuration are
// documented in doc/mercahash-v2.md. Staged device arithmetic independently
// checks these same permanent vectors in mercahash_gpu_tests.
BOOST_AUTO_TEST_CASE(permanent_vectors_reuse_and_independent_allocations)
{
    constexpr const char* vectors[]{
        "eb53364febf3cbe079be98c554f125ee3b78cc64d162fb831f4364f0f8b26dd8",
        "98c21c199742ff142100cc795857156909d73ac8fa81012a2803158dc0927b63",
        "306b4fd4000c5605e57f48395daf1e176c8cc92fd72a14440a9d028f4de93d62",
        "1328a9db2db6212d474953c5c5a5e20d91a04179d77c60c460ede9b61015243c",
        "2c3584c45afe5c1d9da5960519512a72fa8c2b12e2fef818ee4a11d9ce1b212d",
        "61660fe9341245f077278ebbf6ccd9f590848fc7ccd0d542fb546dc4944f575e",
        "921ed70fbd12e3cd51f03e34bfed923cc96e874ad22b05379c6ae14c610c8ede",
    };
    // An unaligned scratchpad with canaries proves byte-oriented access.
    std::vector<unsigned char> storage(mercahash::V2_SCRATCHPAD_BYTES + 2, 0xa5);
    auto scratch{std::span{storage}.subspan(1, mercahash::V2_SCRATCHPAD_BYTES)};
    for (unsigned pattern{0}; pattern < std::size(vectors); ++pattern) {
        std::array<unsigned char, 80> header{};
        for (unsigned i{0}; i < header.size(); ++i)
            header[i] = pattern == 0 ? 0 : pattern == 1 ? i : pattern == 2 ? 255 : (i * 37 + 11) & 255;
        if (pattern >= 4) {
            header[76] = pattern - 3;
            header[77] = header[78] = header[79] = 0;
        }
        std::array<unsigned char, 32> output{}, repeated{}, independent{};
        mercahash::HashV2(header, scratch, output);
        mercahash::HashV2(header, scratch, repeated);
        std::vector<unsigned char> other(mercahash::V2_SCRATCHPAD_BYTES, 0xff);
        mercahash::HashV2(header, other, independent);
        BOOST_CHECK(output == repeated);
        BOOST_CHECK(output == independent);
        BOOST_CHECK(HexStr(output) == vectors[pattern]);
        BOOST_CHECK_EQUAL(storage.front(), 0xa5);
        BOOST_CHECK_EQUAL(storage.back(), 0xa5);
    }
}

BOOST_AUTO_TEST_CASE(buffer_sizes_are_checked_in_release_builds)
{
    std::array<unsigned char, 81> header{};
    std::array<unsigned char, 33> output{};
    std::vector<unsigned char> scratch(mercahash::V2_SCRATCHPAD_BYTES + 1);
    const auto valid_header{std::span{header}.first(80)};
    const auto valid_output{std::span{output}.first(32)};
    const auto valid_scratch{std::span{scratch}.first(mercahash::V2_SCRATCHPAD_BYTES)};
    BOOST_CHECK_THROW(mercahash::HashV2(header, valid_scratch, valid_output), std::invalid_argument);
    BOOST_CHECK_THROW(mercahash::HashV2(valid_header.first(79), valid_scratch, valid_output), std::invalid_argument);
    BOOST_CHECK_THROW(mercahash::HashV2(valid_header, scratch, valid_output), std::invalid_argument);
    BOOST_CHECK_THROW(mercahash::HashV2(valid_header, valid_scratch.first(valid_scratch.size() - 1), valid_output), std::invalid_argument);
    BOOST_CHECK_THROW(mercahash::HashV2(valid_header, valid_scratch, output), std::invalid_argument);
    BOOST_CHECK_THROW(mercahash::HashV2(valid_header, valid_scratch, valid_output.first(31)), std::invalid_argument);
    BOOST_CHECK_THROW(mercahash::HashV1(valid_header, valid_scratch, valid_output), std::invalid_argument);
}
BOOST_AUTO_TEST_SUITE_END()
