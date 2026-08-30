// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/mercatura_fixedpoint.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(mercatura_fixedpoint_tests)

BOOST_AUTO_TEST_CASE(consensus_constant_vectors)
{
    using namespace Consensus;

    BOOST_CHECK_EQUAL(
        Q48_ONE,
        281'474'976'710'656);

    BOOST_CHECK_EQUAL(
        MERCATURA_LN2_Q48,
        195'103'586'505'167);

    BOOST_CHECK_EQUAL(
        MERCATURA_LN_EDGE_SUBSIDY_Q48,
        4'132'578'665'435'046);

    BOOST_CHECK_EQUAL(
        MERCATURA_D_PLUS_Q48,
        298'750'599);

    BOOST_CHECK_EQUAL(
        MERCATURA_D_MINUS_Q48,
        -536'170'157);
}

BOOST_AUTO_TEST_CASE(signed_division_truncates_toward_zero)
{
    using namespace Consensus;

    BOOST_CHECK_EQUAL(
        DivideQ48TowardZero(7, 3), 2);
    BOOST_CHECK_EQUAL(
        DivideQ48TowardZero(-7, 3), -2);

    BOOST_CHECK_EQUAL(
        DivideQ48TowardZero(8, 3), 2);
    BOOST_CHECK_EQUAL(
        DivideQ48TowardZero(-8, 3), -2);

    BOOST_CHECK_EQUAL(
        DivideQ48TowardZero(1, 4), 0);
    BOOST_CHECK_EQUAL(
        DivideQ48TowardZero(-1, 4), 0);

    BOOST_CHECK_EQUAL(
        DivideQ48TowardZero(0, 4033), 0);
}

BOOST_AUTO_TEST_CASE(log_q48_permanent_vectors)
{
    using namespace Consensus;

    BOOST_CHECK(!LogUint256Q48(
        arith_uint256{0}).has_value());

    BOOST_CHECK(!LogAmountQ48(0).has_value());
    BOOST_CHECK(!LogAmountQ48(-1).has_value());

    BOOST_CHECK_EQUAL(
        LogAmountQ48(1).value(),
        0);

    BOOST_CHECK_EQUAL(
        LogAmountQ48(2).value(),
        195'103'586'505'167);

    BOOST_CHECK_EQUAL(
        LogAmountQ48(3).value(),
        309'231'868'366'897);

    BOOST_CHECK_EQUAL(
        LogAmountQ48(10).value(),
        648'120'085'424'803);

    BOOST_CHECK_EQUAL(
        LogAmountQ48(100).value(),
        1'296'240'170'849'605);

    BOOST_CHECK_EQUAL(
        LogAmountQ48(23'782).value(),
        2'836'334'470'499'662);

    BOOST_CHECK_EQUAL(
        LogAmountQ48(2'378'234).value(),
        MERCATURA_LN_EDGE_SUBSIDY_Q48);

    BOOST_CHECK_EQUAL(
        LogAmountQ48(MAX_MONEY).value(),
        11'666'161'537'646'448);
}

BOOST_AUTO_TEST_CASE(log_q48_large_work_vectors)
{
    using namespace Consensus;

    arith_uint256 two_pow_200{1};
    two_pow_200 <<= 200;

    BOOST_CHECK_EQUAL(
        LogUint256Q48(two_pow_200).value(),
        39'020'717'301'033'495);

    arith_uint256 three_times_two_pow_199{3};
    three_times_two_pow_199 <<= 199;

    BOOST_CHECK_EQUAL(
        LogUint256Q48(
            three_times_two_pow_199).value(),
        39'134'845'582'895'224);
}

BOOST_AUTO_TEST_CASE(q96_amount_rounding_half_up)
{
    using namespace Consensus;

    arith_uint256 base{123};
    base <<= 96;

    arith_uint256 half{1};
    half <<= 95;

    BOOST_CHECK_EQUAL(
        RoundPositiveQ96ToAmount(
            base + half - arith_uint256{1})
            .value(),
        123);

    BOOST_CHECK_EQUAL(
        RoundPositiveQ96ToAmount(
            base + half)
            .value(),
        124);

    BOOST_CHECK_EQUAL(
        RoundPositiveQ96ToAmount(
            base + half + arith_uint256{1})
            .value(),
        124);

    arith_uint256 maximum{
        static_cast<uint64_t>(MAX_MONEY)};
    maximum <<= 96;

    BOOST_CHECK_EQUAL(
        RoundPositiveQ96ToAmount(maximum)
            .value(),
        MAX_MONEY);

    BOOST_CHECK(
        !RoundPositiveQ96ToAmount(
            maximum + half)
             .has_value());
}

BOOST_AUTO_TEST_CASE(exp_q48_permanent_vectors)
{
    using namespace Consensus;

    BOOST_CHECK_EQUAL(
        ExpQ48ToAmount(0).value(),
        1);

    BOOST_CHECK_EQUAL(
        ExpQ48ToAmount(Q48_ONE).value(),
        3);

    BOOST_CHECK_EQUAL(
        ExpQ48ToAmount(-Q48_ONE).value(),
        0);

    BOOST_CHECK_EQUAL(
        ExpQ48ToAmount(
            MERCATURA_LN2_Q48).value(),
        2);

    BOOST_CHECK_EQUAL(
        ExpQ48ToAmount(
            -MERCATURA_LN2_Q48).value(),
        1);

    BOOST_CHECK_EQUAL(
        ExpQ48ToAmount(
            2'836'334'470'499'662).value(),
        23'782);

    BOOST_CHECK_EQUAL(
        ExpQ48ToAmount(
            MERCATURA_LN_EDGE_SUBSIDY_Q48)
            .value(),
        2'378'234);

    // A mathematical result above the individual monetary safety ceiling is
    // deterministically capped at MAX_MONEY.
    BOOST_CHECK_EQUAL(
        ExpQ48ToAmount(
            50 * Q48_ONE)
            .value(),
        MAX_MONEY);
}

BOOST_AUTO_TEST_SUITE_END()
