// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/mercatura_emission.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(mercatura_emission_tests)

BOOST_AUTO_TEST_CASE(bootstrap_boundary_vectors)
{
    using namespace Consensus;

    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(-1), 0);
    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(0), 0);

    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(1), 2'378'234);
    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(2), 2'378'234);

    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapSubsidy(MERCATURA_BOOTSTRAP_HALF_BLOCKS),
        10'481'849);
    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapSubsidy(MERCATURA_BOOTSTRAP_HALF_BLOCKS + 1),
        10'481'849);

    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapSubsidy(MERCATURA_BOOTSTRAP_LAST_HEIGHT - 1),
        2'378'234);
    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapSubsidy(MERCATURA_BOOTSTRAP_LAST_HEIGHT),
        2'378'234);

    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapSubsidy(MERCATURA_BOOTSTRAP_LAST_HEIGHT + 1),
        0);
}

BOOST_AUTO_TEST_CASE(bootstrap_permanent_vectors)
{
    using namespace Consensus;

    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(118), 2'378'235);
    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(119), 2'378'234);

    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(1'000), 2'378'255);
    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(10'000), 2'397'748);
    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(50'000), 3'939'468);
    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(77'760), 6'429'993);
    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(100'000), 8'487'271);
    BOOST_CHECK_EQUAL(GetMercaturaBootstrapSubsidy(150'000), 10'478'415);

    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapSubsidy(155'519), 10'481'848);
    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapSubsidy(155'520), 10'481'849);
    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapSubsidy(155'521), 10'481'849);
    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapSubsidy(155'522), 10'481'848);
}

BOOST_AUTO_TEST_CASE(bootstrap_cumulative_vectors)
{
    using namespace Consensus;

    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapCumulativeIssuance(-1), 0);
    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapCumulativeIssuance(0), 0);
    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapCumulativeIssuance(1),
        2'378'234);
    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapCumulativeIssuance(1'000),
        2'378'239'333);
    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapCumulativeIssuance(77'760),
        283'389'756'541);

    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapCumulativeIssuance(
            MERCATURA_BOOTSTRAP_HALF_BLOCKS),
        MERCATURA_BOOTSTRAP_HALF_ISSUANCE);

    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapCumulativeIssuance(
            MERCATURA_BOOTSTRAP_HALF_BLOCKS + 1),
        1'000'010'481'849);

    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapCumulativeIssuance(
            MERCATURA_BOOTSTRAP_LAST_HEIGHT),
        MERCATURA_BOOTSTRAP_TOTAL_ISSUANCE);

    BOOST_CHECK_EQUAL(
        GetMercaturaBootstrapCumulativeIssuance(
            MERCATURA_BOOTSTRAP_LAST_HEIGHT + 1),
        MERCATURA_BOOTSTRAP_TOTAL_ISSUANCE);
}

BOOST_AUTO_TEST_CASE(bootstrap_exhaustive_symmetry_and_total)
{
    using namespace Consensus;

    CAmount first_half_total{0};
    CAmount mirrored_total{0};
    CAmount observed_peak{0};

    for (int height = MERCATURA_BOOTSTRAP_FIRST_HEIGHT;
         height <= MERCATURA_BOOTSTRAP_HALF_BLOCKS;
         ++height) {
        const CAmount left{
            GetMercaturaBootstrapSubsidy(height)};

        const int mirror_height{
            MERCATURA_BOOTSTRAP_LAST_HEIGHT + 1 - height};

        const CAmount right{
            GetMercaturaBootstrapSubsidy(mirror_height)};

        BOOST_CHECK_EQUAL(left, right);
        BOOST_CHECK(left >= MERCATURA_BOOTSTRAP_EDGE_SUBSIDY);
        BOOST_CHECK(MoneyRange(left));

        first_half_total += left;
        mirrored_total += right;

        if (left > observed_peak) {
            observed_peak = left;
        }
    }

    BOOST_CHECK_EQUAL(
        first_half_total,
        MERCATURA_BOOTSTRAP_HALF_ISSUANCE);

    BOOST_CHECK_EQUAL(
        mirrored_total,
        MERCATURA_BOOTSTRAP_HALF_ISSUANCE);

    BOOST_CHECK_EQUAL(
        first_half_total + mirrored_total,
        MERCATURA_BOOTSTRAP_TOTAL_ISSUANCE);

    BOOST_CHECK_EQUAL(
        observed_peak,
        GetMercaturaBootstrapSubsidy(
            MERCATURA_BOOTSTRAP_HALF_BLOCKS));
}

BOOST_AUTO_TEST_CASE(bootstrap_every_height_matches_cumulative_difference)
{
    using namespace Consensus;

    CAmount running_total{0};

    for (int height = MERCATURA_BOOTSTRAP_FIRST_HEIGHT;
         height <= MERCATURA_BOOTSTRAP_LAST_HEIGHT;
         ++height) {
        const CAmount subsidy{
            GetMercaturaBootstrapSubsidy(height)};

        BOOST_CHECK(subsidy >= 0);
        BOOST_CHECK(MoneyRange(subsidy));

        running_total += subsidy;

        BOOST_CHECK_EQUAL(
            running_total,
            GetMercaturaBootstrapCumulativeIssuance(height));
    }

    BOOST_CHECK_EQUAL(
        running_total,
        MERCATURA_BOOTSTRAP_TOTAL_ISSUANCE);
}

BOOST_AUTO_TEST_SUITE_END()
