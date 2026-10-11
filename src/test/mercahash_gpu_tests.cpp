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
BOOST_AUTO_TEST_SUITE_END()
