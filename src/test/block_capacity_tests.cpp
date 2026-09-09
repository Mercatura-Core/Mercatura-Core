// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <policy/policy.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <limits>
#include <vector>

BOOST_AUTO_TEST_SUITE(block_capacity_tests)

BOOST_AUTO_TEST_CASE(block_capacity_constants)
{
    BOOST_CHECK_EQUAL(MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES, 1'048'576ULL);
    BOOST_CHECK_EQUAL(MERCATURA_BLOCK_CAPACITY_DOUBLING_INTERVAL, 1'051'200ULL);
    BOOST_CHECK_EQUAL(MERCATURA_BLOCK_CAPACITY_MAX_DOUBLINGS, 10ULL);
    BOOST_CHECK_EQUAL(MERCATURA_MAX_BLOCK_CAPACITY_BYTES, 1'073'741'824ULL);
}

BOOST_AUTO_TEST_CASE(initial_capacity)
{
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(0), 1'048'576ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(1), 1'048'576ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(1'051'199), 1'048'576ULL);
}

BOOST_AUTO_TEST_CASE(first_doubling_boundary)
{
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(1'051'199), 1'048'576ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(1'051'200), 2'097'152ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(1'051'201), 2'097'152ULL);
}

BOOST_AUTO_TEST_CASE(later_doubling_boundaries)
{
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(2'102'399), 2'097'152ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(2'102'400), 4'194'304ULL);

    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(3'153'600), 8'388'608ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(4'204'800), 16'777'216ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(5'256'000), 33'554'432ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(6'307'200), 67'108'864ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(7'358'400), 134'217'728ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(8'409'600), 268'435'456ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(9'460'800), 536'870'912ULL);
}

BOOST_AUTO_TEST_CASE(first_several_doubling_boundaries_exact)
{
    for (uint64_t doubling = 1; doubling <= 4; ++doubling) {
        const int64_t boundary{
            static_cast<int64_t>(
                MERCATURA_BLOCK_CAPACITY_DOUBLING_INTERVAL *
                doubling)};

        const uint64_t before{
            MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES <<
            (doubling - 1)};

        const uint64_t after{
            MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES <<
            doubling};

        BOOST_CHECK_EQUAL(
            GetMaxBlockCapacityBytes(boundary - 1),
            before);

        BOOST_CHECK_EQUAL(
            GetMaxBlockCapacityBytes(boundary),
            after);

        BOOST_CHECK_EQUAL(
            GetMaxBlockCapacityBytes(boundary + 1),
            after);
    }
}

BOOST_AUTO_TEST_CASE(maximum_capacity_boundary)
{
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(10'511'999), 536'870'912ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(10'512'000), 1'073'741'824ULL);
    BOOST_CHECK_EQUAL(GetMaxBlockCapacityBytes(10'512'001), 1'073'741'824ULL);
}

BOOST_AUTO_TEST_CASE(maximum_capacity_saturates)
{
    BOOST_CHECK_EQUAL(
        GetMaxBlockCapacityBytes(11'563'200),
        1'073'741'824ULL);

    BOOST_CHECK_EQUAL(
        GetMaxBlockCapacityBytes(std::numeric_limits<int64_t>::max()),
        1'073'741'824ULL);
}

BOOST_AUTO_TEST_CASE(transaction_capacity_uses_full_serialized_bytes)
{
    CMutableTransaction legacy;
    legacy.vin.resize(1);
    legacy.vout.resize(1);

    const CTransaction legacy_tx{legacy};

    const uint64_t legacy_full_size{
        ::GetSerializeSize(TX_WITH_WITNESS(legacy_tx))};

    BOOST_CHECK_EQUAL(
        GetTransactionCapacityBytes(legacy_tx),
        legacy_full_size);

    // A transaction without witness data still has ordinary BIP141
    // weight equal to four times its complete serialized size.
    BOOST_CHECK_EQUAL(
        GetTransactionWeight(legacy_tx),
        legacy_full_size * WITNESS_SCALE_FACTOR);

    CMutableTransaction witness{legacy};
    witness.vin[0].scriptWitness.stack.push_back(
        std::vector<unsigned char>(100, 0x01));

    const CTransaction witness_tx{witness};

    const uint64_t witness_full_size{
        ::GetSerializeSize(TX_WITH_WITNESS(witness_tx))};

    BOOST_CHECK_EQUAL(
        GetTransactionCapacityBytes(witness_tx),
        witness_full_size);

    // Mercatura capacity charges every serialized witness byte normally.
    // BIP141 weight remains distinct and continues to discount witness
    // bytes for the Bitcoin mechanisms that still use weight.
    BOOST_CHECK_LT(
        GetTransactionWeight(witness_tx),
        witness_full_size * WITNESS_SCALE_FACTOR);
}

BOOST_AUTO_TEST_CASE(equal_witness_and_nonwitness_growth_costs_the_same)
{
    constexpr size_t GROWTH_BYTES{32};

    CMutableTransaction base;
    base.vin.resize(1);
    base.vout.resize(1);

    // Keep witness serialization active in all three variants so marker/flag
    // overhead is identical and only the location of the added bytes differs.
    base.vin[0].scriptWitness.stack.push_back(
        std::vector<unsigned char>(1, 0x01));

    CMutableTransaction nonwitness_growth{base};
    const std::vector<unsigned char> nonwitness_bytes(
        GROWTH_BYTES,
        0x02);
    nonwitness_growth.vin[0].scriptSig =
        CScript(
            nonwitness_bytes.begin(),
            nonwitness_bytes.end());

    CMutableTransaction witness_growth{base};
    witness_growth.vin[0].scriptWitness.stack[0].resize(
        1 + GROWTH_BYTES,
        0x03);

    const CTransaction base_tx{base};
    const CTransaction nonwitness_tx{nonwitness_growth};
    const CTransaction witness_tx{witness_growth};

    const uint64_t base_size{
        ::GetSerializeSize(TX_WITH_WITNESS(base_tx))};

    const uint64_t nonwitness_size{
        ::GetSerializeSize(TX_WITH_WITNESS(nonwitness_tx))};

    const uint64_t witness_size{
        ::GetSerializeSize(TX_WITH_WITNESS(witness_tx))};

    BOOST_REQUIRE_EQUAL(
        nonwitness_size - base_size,
        GROWTH_BYTES);

    BOOST_REQUIRE_EQUAL(
        witness_size - base_size,
        GROWTH_BYTES);

    BOOST_CHECK_EQUAL(
        GetTransactionCapacityBytes(nonwitness_tx) -
            GetTransactionCapacityBytes(base_tx),
        GROWTH_BYTES);

    BOOST_CHECK_EQUAL(
        GetTransactionCapacityBytes(witness_tx) -
            GetTransactionCapacityBytes(base_tx),
        GROWTH_BYTES);

    BOOST_CHECK_EQUAL(
        GetTransactionFeeSize(nonwitness_tx) -
            GetTransactionFeeSize(base_tx),
        GROWTH_BYTES);

    BOOST_CHECK_EQUAL(
        GetTransactionFeeSize(witness_tx) -
            GetTransactionFeeSize(base_tx),
        GROWTH_BYTES);

    // BIP141 virtual size still distinguishes the two locations, proving
    // this vector really does compare non-witness bytes against witness bytes.
    BOOST_CHECK_GT(
        GetVirtualTransactionSize(nonwitness_tx),
        GetVirtualTransactionSize(witness_tx));
}

BOOST_AUTO_TEST_CASE(block_capacity_uses_full_serialized_bytes)
{
    CMutableTransaction legacy;
    legacy.vin.resize(1);
    legacy.vout.resize(1);

    CMutableTransaction witness{legacy};
    witness.vin[0].scriptWitness.stack.push_back(
        std::vector<unsigned char>(100, 0x02));

    CBlock block;
    block.vtx.push_back(MakeTransactionRef(legacy));
    block.vtx.push_back(MakeTransactionRef(witness));

    const uint64_t full_size{
        ::GetSerializeSize(TX_WITH_WITNESS(block))};

    BOOST_CHECK_EQUAL(
        GetBlockCapacityBytes(block),
        full_size);

    // Presence of witness data makes BIP141 block weight different from
    // Mercatura's undiscounted serialized-byte capacity metric.
    BOOST_CHECK_LT(
        GetBlockWeight(block),
        full_size * WITNESS_SCALE_FACTOR);
}

BOOST_AUTO_TEST_CASE(block_capacity_predicate_changes_at_boundary)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.resize(1);

    // Make a witness-inclusive block comfortably larger than 1 MiB but
    // smaller than 2 MiB. Its validity under the capacity rule must therefore
    // change exactly at height 1,051,200.
    tx.vin[0].scriptWitness.stack.push_back(
        std::vector<unsigned char>(1'100'000, 0x03));

    CBlock block;
    block.vtx.push_back(MakeTransactionRef(tx));

    const uint64_t capacity{GetBlockCapacityBytes(block)};

    BOOST_REQUIRE_GT(
        capacity,
        MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES);

    BOOST_REQUIRE_LE(
        capacity,
        MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES * 2);

    BOOST_CHECK(!IsBlockWithinCapacity(block, 1'051'199));
    BOOST_CHECK(IsBlockWithinCapacity(block, 1'051'200));
    BOOST_CHECK(IsBlockWithinCapacity(block, 1'051'201));
}

BOOST_AUTO_TEST_SUITE_END()
