// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/mercatura_controller.h>

#include <chain.h>
#include <consensus/mercatura_emission.h>
#include <consensus/mercatura_fixedpoint.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>

BOOST_AUTO_TEST_SUITE(mercatura_controller_tests)

BOOST_AUTO_TEST_CASE(controller_consensus_constants)
{
    using namespace Consensus;

    BOOST_CHECK_EQUAL(
        MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT,
        311'041);

    BOOST_CHECK_EQUAL(
        MERCATURA_SHORT_EMA_DIVISOR,
        4'033);

    BOOST_CHECK_EQUAL(
        MERCATURA_LONG_EMA_DIVISOR,
        103'681);

    BOOST_CHECK_EQUAL(
        MERCATURA_CONTROLLER_DIVISOR,
        4'204'800);

    BOOST_CHECK_EQUAL(
        MERCATURA_PERPETUAL_SUBSIDY_FLOOR,
        23'782);
}

BOOST_AUTO_TEST_CASE(bootstrap_signal_initialization_and_ema)
{
    using namespace Consensus;

    const uint64_t edge{
        static_cast<uint64_t>(
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    // W_1 = 2 * R_1, so z_1 is exactly the permanent ln(2) vector.
    arith_uint256 first_work{
        edge * uint64_t{2}};

    const auto first{
        AdvanceMcaEmissionState(
            nullptr,
            1,
            first_work,
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    BOOST_REQUIRE(first.has_value());

    BOOST_CHECK_EQUAL(
        first->height,
        1);

    BOOST_CHECK_EQUAL(
        first->s_q48,
        MERCATURA_LN2_Q48);

    BOOST_CHECK_EQUAL(
        first->l_q48,
        MERCATURA_LN2_Q48);

    BOOST_CHECK(!first->controller_initialized);
    BOOST_CHECK_EQUAL(first->q_q48, 0);
    BOOST_CHECK_EQUAL(first->r_q48, 0);

    // W_2 = R_2, therefore z_2 = 0.
    arith_uint256 second_work{edge};

    const auto second{
        AdvanceMcaEmissionState(
            &*first,
            2,
            second_work,
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    BOOST_REQUIRE(second.has_value());

    // These vectors permanently lock truncate-toward-zero EMA division.
    BOOST_CHECK_EQUAL(
        second->s_q48,
        195'006'832'928'919);

    BOOST_CHECK_EQUAL(
        second->l_q48,
        195'099'822'969'196);

    BOOST_CHECK_EQUAL(
        second->s_q48 - second->l_q48,
        -92'990'040'277);

    BOOST_CHECK(!second->controller_initialized);
}

BOOST_AUTO_TEST_CASE(activation_has_no_controller_backlog)
{
    using namespace Consensus;

    McaEmissionState bootstrap_parent;
    bootstrap_parent.height =
        MERCATURA_BOOTSTRAP_LAST_HEIGHT;
    bootstrap_parent.s_q48 =
        4 * Q48_ONE;
    bootstrap_parent.l_q48 =
        -4 * Q48_ONE;
    bootstrap_parent.subsidy =
        MERCATURA_BOOTSTRAP_EDGE_SUBSIDY;
    bootstrap_parent.controller_initialized = false;

    const auto command{
        GetMcaEmissionCommand(
            &bootstrap_parent,
            MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT)};

    BOOST_REQUIRE(command.has_value());

    // The large warmed EMA error above must NOT be applied to block 311041.
    BOOST_CHECK_EQUAL(
        command->subsidy,
        MERCATURA_BOOTSTRAP_EDGE_SUBSIDY);

    BOOST_CHECK_EQUAL(
        command->q_q48,
        MERCATURA_LN_EDGE_SUBSIDY_Q48);

    BOOST_CHECK_EQUAL(
        command->r_q48,
        MERCATURA_LN_EDGE_SUBSIDY_Q48);

    BOOST_CHECK(command->controller_initialized);

    arith_uint256 activation_work{
        static_cast<uint64_t>(
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    const auto activation_state{
        AdvanceMcaEmissionState(
            &bootstrap_parent,
            MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT,
            activation_work,
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    BOOST_REQUIRE(activation_state.has_value());

    BOOST_CHECK_EQUAL(
        activation_state->q_q48,
        MERCATURA_LN_EDGE_SUBSIDY_Q48);

    BOOST_CHECK_EQUAL(
        activation_state->r_q48,
        MERCATURA_LN_EDGE_SUBSIDY_Q48);

    // The warmed s/l state is carried through block 311041. Its error is
    // therefore eligible to affect the command for block 311042.
    const auto first_adaptive_command{
        GetMcaEmissionCommand(
            &*activation_state,
            MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT + 1)};

    BOOST_REQUIRE(first_adaptive_command.has_value());

    BOOST_CHECK_NE(
        first_adaptive_command->q_q48,
        MERCATURA_LN_EDGE_SUBSIDY_Q48);
}

BOOST_AUTO_TEST_CASE(upward_rate_limiter_vector)
{
    using namespace Consensus;

    McaEmissionState parent;
    parent.height =
        MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT;

    parent.s_q48 =
        1'256'238'426'931'200;
    parent.l_q48 = 0;

    parent.q_q48 =
        MERCATURA_LN_EDGE_SUBSIDY_Q48;
    parent.r_q48 =
        MERCATURA_LN_EDGE_SUBSIDY_Q48;

    parent.subsidy =
        MERCATURA_BOOTSTRAP_EDGE_SUBSIDY;
    parent.controller_initialized = true;

    const auto command{
        GetMcaEmissionCommand(
            &parent,
            MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT + 1)};

    BOOST_REQUIRE(command.has_value());

    // q receives the complete controller movement.
    BOOST_CHECK_EQUAL(
        command->q_q48,
        4'132'578'964'197'990);

    // r is independently clipped to exactly d_plus.
    BOOST_CHECK_EQUAL(
        command->r_q48,
        4'132'578'964'185'645);

    BOOST_CHECK_EQUAL(
        command->r_q48 - parent.r_q48,
        MERCATURA_D_PLUS_Q48);

    BOOST_CHECK_EQUAL(
        command->subsidy,
        2'378'237);
}

BOOST_AUTO_TEST_CASE(downward_rate_limiter_vector)
{
    using namespace Consensus;

    McaEmissionState parent;
    parent.height =
        MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT;

    parent.s_q48 =
        -2'254'540'184'409'600;
    parent.l_q48 = 0;

    parent.q_q48 =
        MERCATURA_LN_EDGE_SUBSIDY_Q48;
    parent.r_q48 =
        MERCATURA_LN_EDGE_SUBSIDY_Q48;

    parent.subsidy =
        MERCATURA_BOOTSTRAP_EDGE_SUBSIDY;
    parent.controller_initialized = true;

    const auto command{
        GetMcaEmissionCommand(
            &parent,
            MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT + 1)};

    BOOST_REQUIRE(command.has_value());

    BOOST_CHECK_EQUAL(
        command->q_q48,
        4'132'578'129'252'544);

    BOOST_CHECK_EQUAL(
        command->r_q48,
        4'132'578'129'264'889);

    BOOST_CHECK_EQUAL(
        command->r_q48 - parent.r_q48,
        MERCATURA_D_MINUS_Q48);

    BOOST_CHECK_EQUAL(
        command->subsidy,
        2'378'229);
}

BOOST_AUTO_TEST_CASE(subsidy_floor_does_not_reset_controller)
{
    using namespace Consensus;

    // Permanent Q16.48 vector for ln(1000).
    constexpr int64_t LN_1000_Q48{
        1'944'360'256'274'408};

    McaEmissionState parent;
    parent.height =
        MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT;

    parent.s_q48 = 0;
    parent.l_q48 = 0;

    parent.q_q48 = LN_1000_Q48;
    parent.r_q48 = LN_1000_Q48;

    parent.subsidy =
        MERCATURA_PERPETUAL_SUBSIDY_FLOOR;
    parent.controller_initialized = true;

    const auto command{
        GetMcaEmissionCommand(
            &parent,
            MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT + 1)};

    BOOST_REQUIRE(command.has_value());

    // Candidate exp(r) is approximately 1000 base units, below the floor.
    BOOST_CHECK_EQUAL(
        command->subsidy,
        MERCATURA_PERPETUAL_SUBSIDY_FLOOR);

    // Floor applies only to actual subsidy. It does not overwrite q or r.
    BOOST_CHECK_EQUAL(
        command->q_q48,
        LN_1000_Q48);

    BOOST_CHECK_EQUAL(
        command->r_q48,
        LN_1000_Q48);
}

BOOST_AUTO_TEST_CASE(transition_rejects_wrong_subsidy_or_work)
{
    using namespace Consensus;

    arith_uint256 work{
        static_cast<uint64_t>(
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    BOOST_CHECK(
        !AdvanceMcaEmissionState(
             nullptr,
             1,
             work,
             MERCATURA_BOOTSTRAP_EDGE_SUBSIDY + 1)
             .has_value());

    BOOST_CHECK(
        !AdvanceMcaEmissionState(
             nullptr,
             1,
             arith_uint256{0},
             MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)
             .has_value());
}

BOOST_AUTO_TEST_CASE(parent_height_is_consensus_input)
{
    using namespace Consensus;

    McaEmissionState parent;
    parent.height = 100;

    BOOST_CHECK(
        !GetMcaEmissionCommand(
             &parent,
             100)
             .has_value());

    BOOST_CHECK(
        !GetMcaEmissionCommand(
             &parent,
             102)
             .has_value());

    BOOST_CHECK(
        GetMcaEmissionCommand(
            &parent,
            101)
            .has_value());
}


BOOST_AUTO_TEST_CASE(upper_safety_ceiling_does_not_reset_controller)
{
    using namespace Consensus;

    McaEmissionState parent;
    parent.height =
        MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT;

    parent.s_q48 = 0;
    parent.l_q48 = 0;

    parent.q_q48 = 50 * Q48_ONE;
    parent.r_q48 = 50 * Q48_ONE;

    parent.subsidy = MAX_MONEY;
    parent.controller_initialized = true;

    const auto command{
        GetMcaEmissionCommand(
            &parent,
            MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT + 1)};

    BOOST_REQUIRE(command.has_value());

    BOOST_CHECK_EQUAL(
        command->subsidy,
        MAX_MONEY);

    // The implementation-safety ceiling changes only actual subsidy.
    BOOST_CHECK_EQUAL(
        command->q_q48,
        parent.q_q48);

    BOOST_CHECK_EQUAL(
        command->r_q48,
        parent.r_q48);
}

BOOST_AUTO_TEST_CASE(derived_states_are_branch_local)
{
    using namespace Consensus;

    const uint64_t edge{
        static_cast<uint64_t>(
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    const auto parent{
        DeriveMcaEmissionState(
            nullptr,
            1,
            arith_uint256{edge})};

    BOOST_REQUIRE(parent.has_value());

    const auto branch_a{
        DeriveMcaEmissionState(
            &*parent,
            2,
            arith_uint256{edge})};

    const auto branch_b{
        DeriveMcaEmissionState(
            &*parent,
            2,
            arith_uint256{edge * uint64_t{2}})};

    BOOST_REQUIRE(branch_a.has_value());
    BOOST_REQUIRE(branch_b.has_value());

    BOOST_CHECK_EQUAL(branch_a->height, 2);
    BOOST_CHECK_EQUAL(branch_b->height, 2);

    BOOST_CHECK_NE(
        branch_a->s_q48,
        branch_b->s_q48);

    BOOST_CHECK_NE(
        branch_a->l_q48,
        branch_b->l_q48);

    // Deriving either child does not mutate the shared parent state.
    BOOST_CHECK_EQUAL(parent->height, 1);
    BOOST_CHECK_EQUAL(parent->s_q48, 0);
    BOOST_CHECK_EQUAL(parent->l_q48, 0);
}

BOOST_AUTO_TEST_CASE(blockindex_emission_state_is_memory_only)
{
    using namespace Consensus;

    CBlockIndex index;

    BOOST_CHECK(
        !index.m_mca_emission_state.has_value());

    McaEmissionState state;
    state.height = 1;
    state.s_q48 = 123;
    state.l_q48 = 456;
    state.subsidy =
        MERCATURA_BOOTSTRAP_EDGE_SUBSIDY;

    index.m_mca_emission_state = state;

    BOOST_REQUIRE(
        index.m_mca_emission_state.has_value());

    BOOST_CHECK_EQUAL(
        index.m_mca_emission_state->height,
        1);

    // CDiskBlockIndex deliberately drops Mercatura's reconstructed,
    // memory-only controller state.
    CDiskBlockIndex disk{&index};

    BOOST_CHECK(
        !disk.m_mca_emission_state.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
