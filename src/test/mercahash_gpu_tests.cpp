// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <algorithm>
#include <array>
#include <boost/test/unit_test.hpp>
#include <crypto/mercahash.h>
#include <crypto/sha3.h>
#include <mining/mercahash_gpu.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <vector>
// Compile the actual portable device source with independently frozen V2
// geometry and domains. Its SHA3 implementation is separate from Core SHA3.
#undef BITCOIN_MINING_MERCAHASH_GPU_H
#undef MH_VERSION
#define MH_VERSION 2
namespace staged_v2 {
#include <mining/mercahash_gpu.h>
}
#undef MH_VERSION

BOOST_FIXTURE_TEST_SUITE(mercahash_gpu_tests, BasicTestingSetup)
BOOST_AUTO_TEST_CASE(staged_device_arithmetic_matches_frozen_vectors_and_core)
{
    constexpr const char *vectors[]{"cc97e7cbce1d4ed2b54c9c37a81a000383229341a277a44829484855b9ef79dc",
                                    "2321712af21502878986c0c4f21d79e17e2281619e3958f11e3c634c9f17f7d8",
                                    "9c9a427928875304bb7d6351354b5e50ec8e57c581971f68eab176371c4cd2c2",
                                    "1244c6c9e0365baeb5958cfaa57aff582d81ae1d757769e823085bb00676e170"};
    constexpr unsigned contexts{2};
    std::array<unsigned char, contexts * 80> headers{};
    std::array<mh_u64, contexts * 16> states{};
    std::array<unsigned char, contexts * 32> outputs{};
    // Guard words catch the highest scratchpad indices and private context offsets.
    std::vector<mh_u64> scratch(contexts * mercahash::SCRATCHPAD_BYTES / 8 + 2, 0xfedcba9876543210ULL);
    std::vector<unsigned char> reference_scratch(mercahash::SCRATCHPAD_BYTES);
    for (unsigned batch{0}; batch < 2; ++batch) {
        for (unsigned slot{0}; slot < contexts; ++slot) {
            const unsigned pattern{batch * contexts + slot};
            for (unsigned byte{0}; byte < 80; ++byte)
                headers[slot * 80 + byte] = pattern == 0   ? 0
                                            : pattern == 1 ? byte
                                            : pattern == 2 ? 255
                                                           : (byte * 37 + 11) & 255;
            mh_test_id = slot;
            mh_init(headers.data(), states.data(), contexts);
        }
        auto stage = [&](unsigned phase, unsigned begin, unsigned end) {
            for (unsigned slot{0}; slot < contexts; ++slot) {
                mh_test_id = slot;
                mh_stage(headers.data(), states.data(), scratch.data() + 1, outputs.data(), contexts, phase, begin,
                         end);
            }
        };
        // Deliberately irregular boundaries prove launch splitting does not
        // affect the serial algorithm, binding transitions or checkpoints.
        for (unsigned begin{0}; begin < 2097152; begin += 7001)
            stage(0, begin, std::min(begin + 7001, 2097152U));
        for (unsigned begin{0}; begin < 32768; begin += 31)
            stage(1, begin, std::min(begin + 31, 32768U));
        for (unsigned begin{1}; begin <= 131072; begin += 997)
            stage(2, begin, std::min(begin + 996, 131072U));
        stage(3, 0, 0);
        for (unsigned slot{0}; slot < contexts; ++slot) {
            const auto expected{ParseHex(vectors[batch * contexts + slot])};
            std::array<unsigned char, 32> authoritative{};
            mercahash::HashV1(std::span<const unsigned char>{headers.data() + slot * 80, 80}, reference_scratch,
                              authoritative);
            BOOST_CHECK_EQUAL_COLLECTIONS(outputs.begin() + slot * 32, outputs.begin() + (slot + 1) * 32,
                                          expected.begin(), expected.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(outputs.begin() + slot * 32, outputs.begin() + (slot + 1) * 32,
                                          authoritative.begin(), authoritative.end());
        }
        BOOST_CHECK_EQUAL(scratch.front(), 0xfedcba9876543210ULL);
        BOOST_CHECK_EQUAL(scratch.back(), 0xfedcba9876543210ULL);
    }
}
BOOST_AUTO_TEST_CASE(sha3_padding_and_rotation_boundaries)
{
    std::array<unsigned char, 136> input{};
    for (unsigned size : {32U, 64U}) {
        // Kernel SHA3's messages cross both SHA3-256/512 rate boundaries.
        for (unsigned length : {0U, 71U, 72U, 135U, 136U}) {
            std::array<unsigned char, 64> actual{}, expected{};
            mh_sha3(input.data(), length, actual.data(), size);
            if (size == 32)
                SHA3_256{}.Write(std::span{input}.first(length)).Finalize(std::span{expected}.first(32));
            else
                SHA3_512{}.Write(std::span{input}.first(length)).Finalize(expected);
            BOOST_CHECK_EQUAL_COLLECTIONS(actual.begin(), actual.begin() + size, expected.begin(),
                                          expected.begin() + size);
        }
    }
    BOOST_CHECK_EQUAL(mh_rotl(0x123456789abcdef0ULL, 0), 0x123456789abcdef0ULL);
    BOOST_CHECK_EQUAL(mh_rotl(1, 63), 0x8000000000000000ULL);
}
BOOST_AUTO_TEST_CASE(v2_stages_match_permanent_vectors_and_cpu_with_partial_batches)
{
    constexpr unsigned contexts{2};
    constexpr mh_u64 guard{0xfedcba9876543210ULL};
    std::array<unsigned char, contexts * 80> headers{};
    std::array<staged_v2::mh_u64, contexts * 16> states{};
    std::array<unsigned char, contexts * 32> outputs{};
    std::vector<staged_v2::mh_u64> scratch(contexts * mercahash::V2_SCRATCHPAD_BYTES / 8 + 2, guard);
    std::vector<unsigned char> cpu_scratch(mercahash::V2_SCRATCHPAD_BYTES);
    constexpr const char* vectors[]{
        "eb53364febf3cbe079be98c554f125ee3b78cc64d162fb831f4364f0f8b26dd8",
        "98c21c199742ff142100cc795857156909d73ac8fa81012a2803158dc0927b63",
        "306b4fd4000c5605e57f48395daf1e176c8cc92fd72a14440a9d028f4de93d62",
        "1328a9db2db6212d474953c5c5a5e20d91a04179d77c60c460ede9b61015243c",
        "2c3584c45afe5c1d9da5960519512a72fa8c2b12e2fef818ee4a11d9ce1b212d",
        "61660fe9341245f077278ebbf6ccd9f590848fc7ccd0d542fb546dc4944f575e",
        "921ed70fbd12e3cd51f03e34bfed923cc96e874ad22b05379c6ae14c610c8ede"};
    for (unsigned batch{0}; batch < 5; ++batch) {
        const unsigned count{batch == 3 ? 1U : contexts};
        for (unsigned slot{0}; slot < count; ++slot) {
            const unsigned pattern{batch * 2 + slot};
            for (unsigned byte{0}; byte < 80; ++byte)
                headers[slot * 80 + byte] = pattern == 0 ? 0 : pattern == 1 ? byte : pattern == 2 ? 255 : (byte * 37 + 11) & 255;
            if (pattern >= 4) {
                headers[slot * 80 + 76] = pattern - 3;
                headers[slot * 80 + 77] = headers[slot * 80 + 78] = headers[slot * 80 + 79] = 0;
            }
            staged_v2::mh_test_id = slot;
            staged_v2::mh_init(headers.data(), states.data(), count);
        }
        auto stage = [&](unsigned phase, unsigned begin, unsigned end) {
            for (unsigned slot{0}; slot < count; ++slot) {
                staged_v2::mh_test_id = slot;
                staged_v2::mh_stage(headers.data(), states.data(), scratch.data() + 1, outputs.data(), count, phase, begin, end);
            }
        };
        // Split across half rounds, binding ARX boundaries and checkpoints.
        for (unsigned begin{0}; begin < 262144; begin += 7001) stage(0, begin, std::min(begin + 7001, 262144U));
        for (unsigned begin{0}; begin < 4096; begin += 31) stage(1, begin, std::min(begin + 31, 4096U));
        for (unsigned begin{1}; begin <= 16384; begin += 997) stage(2, begin, std::min(begin + 996, 16384U));
        stage(3, 0, 0);
        for (unsigned slot{0}; slot < count; ++slot) {
            std::array<unsigned char, 32> expected{};
            mercahash::HashV2(std::span<const unsigned char>{headers.data() + slot * 80, 80}, cpu_scratch, expected);
            BOOST_CHECK_EQUAL_COLLECTIONS(outputs.begin() + slot * 32, outputs.begin() + (slot + 1) * 32, expected.begin(), expected.end());
            const unsigned pattern{batch * 2 + slot};
            if (pattern < std::size(vectors)) BOOST_CHECK(HexStr(expected) == vectors[pattern]);
        }
        const auto unchanged{outputs};
        staged_v2::mh_test_id = count; // Inactive launch groups must do nothing.
        staged_v2::mh_stage(headers.data(), states.data(), scratch.data() + 1, outputs.data(), count, 3, 0, 0);
        BOOST_CHECK(outputs == unchanged);
        BOOST_CHECK_EQUAL(scratch.front(), guard);
        BOOST_CHECK_EQUAL(scratch.back(), guard);
    }
}
BOOST_AUTO_TEST_SUITE_END()
