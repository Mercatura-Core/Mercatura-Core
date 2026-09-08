// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/mercatura_controller.h>

#include <chain.h>
#include <consensus/mercatura_emission.h>
#include <consensus/mercatura_fixedpoint.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <utility>

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
        840'960);

    // Mercatura has 210,240 target blocks/year. k = 0.25 = 1/4,
    // so the exact integer controller divisor is:
    //
    //     210,240 / 0.25 = 4 * 210,240 = 840,960
    //
    // This permanently locks the gain relationship without introducing
    // floating-point arithmetic into consensus.
    constexpr int64_t BLOCKS_PER_YEAR{210'240};
    BOOST_CHECK_EQUAL(
        MERCATURA_CONTROLLER_DIVISOR,
        4 * BLOCKS_PER_YEAR);

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

BOOST_AUTO_TEST_CASE(upward_work_yield_step_ema_vector)
{
    using namespace Consensus;

    const uint64_t edge{
        static_cast<uint64_t>(
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    // Start at z_1 = ln(W_1 / R_1) = 0.
    arith_uint256 initial_work{edge};

    const auto first{
        AdvanceMcaEmissionState(
            nullptr,
            1,
            initial_work,
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    BOOST_REQUIRE(first.has_value());
    BOOST_CHECK_EQUAL(first->s_q48, 0);
    BOOST_CHECK_EQUAL(first->l_q48, 0);

    // Step upward to W_2 = 2 * R_2, so z_2 = ln(2).
    arith_uint256 stepped_work{
        edge * uint64_t{2}};

    const auto second{
        AdvanceMcaEmissionState(
            &*first,
            2,
            stepped_work,
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    BOOST_REQUIRE(second.has_value());

    // Exact truncate-toward-zero vectors using:
    // alpha_s = 2 / 4,033  (4,032-block span)
    // alpha_l = 2 / 103,681 (103,680-block span)
    BOOST_CHECK_EQUAL(
        second->s_q48,
        96'753'576'248);
    BOOST_CHECK_EQUAL(
        second->l_q48,
        3'763'535'971);
    BOOST_CHECK_EQUAL(
        second->s_q48 - second->l_q48,
        92'990'040'277);
}

BOOST_AUTO_TEST_CASE(steady_signal_has_zero_ema_error)
{
    using namespace Consensus;

    const uint64_t edge{
        static_cast<uint64_t>(
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    // Keep the work/subsidy ratio constant across both observations so
    // z_1 == z_2 == ln(2). Both EMAs must therefore remain identical.
    arith_uint256 steady_work{
        edge * uint64_t{2}};

    const auto first{
        AdvanceMcaEmissionState(
            nullptr,
            1,
            steady_work,
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    BOOST_REQUIRE(first.has_value());

    BOOST_CHECK_EQUAL(
        first->s_q48,
        MERCATURA_LN2_Q48);
    BOOST_CHECK_EQUAL(
        first->l_q48,
        MERCATURA_LN2_Q48);
    BOOST_CHECK_EQUAL(
        first->s_q48 - first->l_q48,
        0);

    const auto second{
        AdvanceMcaEmissionState(
            &*first,
            2,
            steady_work,
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY)};

    BOOST_REQUIRE(second.has_value());

    BOOST_CHECK_EQUAL(
        second->s_q48,
        MERCATURA_LN2_Q48);
    BOOST_CHECK_EQUAL(
        second->l_q48,
        MERCATURA_LN2_Q48);
    BOOST_CHECK_EQUAL(
        second->s_q48 - second->l_q48,
        0);
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

    // q receives the complete k=0.25 controller movement:
    // trunc(1,256,238,426,931,200 / 840,960) = 1,493,814,720.
    BOOST_CHECK_EQUAL(
        command->q_q48 - parent.q_q48,
        1'493'814'720);
    BOOST_CHECK_EQUAL(
        command->q_q48,
        4'132'580'159'249'766);

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

BOOST_AUTO_TEST_CASE(upward_rate_limiter_boundary_vectors)
{
    using namespace Consensus;

    const auto command_for_gap = [](int64_t gap_q48) {
        McaEmissionState parent;

        parent.height =
            MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT;

        // With q_h == r_h, choose e_h so that:
        //
        // q_(h+1) - r_h
        //   = trunc(e_h / 840,960)
        //   = gap_q48 exactly.
        parent.s_q48 =
            gap_q48 * MERCATURA_CONTROLLER_DIVISOR;
        parent.l_q48 = 0;

        parent.q_q48 =
            MERCATURA_LN_EDGE_SUBSIDY_Q48;
        parent.r_q48 =
            MERCATURA_LN_EDGE_SUBSIDY_Q48;

        parent.subsidy =
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY;
        parent.controller_initialized = true;

        return std::pair{
            parent,
            GetMcaEmissionCommand(
                &parent,
                MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT + 1)};
    };

    // Just inside the upper limiter: no clipping.
    const auto [inside_parent, inside]{
        command_for_gap(MERCATURA_D_PLUS_Q48 - 1)};

    BOOST_REQUIRE(inside.has_value());
    BOOST_CHECK_EQUAL(
        inside->q_q48 - inside_parent.r_q48,
        MERCATURA_D_PLUS_Q48 - 1);
    BOOST_CHECK_EQUAL(
        inside->r_q48 - inside_parent.r_q48,
        MERCATURA_D_PLUS_Q48 - 1);

    // Exactly at d_plus: accepted without changing the value.
    const auto [boundary_parent, boundary]{
        command_for_gap(MERCATURA_D_PLUS_Q48)};

    BOOST_REQUIRE(boundary.has_value());
    BOOST_CHECK_EQUAL(
        boundary->q_q48 - boundary_parent.r_q48,
        MERCATURA_D_PLUS_Q48);
    BOOST_CHECK_EQUAL(
        boundary->r_q48 - boundary_parent.r_q48,
        MERCATURA_D_PLUS_Q48);

    // One Q16.48 unit outside: q keeps the desired value, while r clips
    // to exactly d_plus.
    const auto [outside_parent, outside]{
        command_for_gap(MERCATURA_D_PLUS_Q48 + 1)};

    BOOST_REQUIRE(outside.has_value());
    BOOST_CHECK_EQUAL(
        outside->q_q48 - outside_parent.r_q48,
        MERCATURA_D_PLUS_Q48 + 1);
    BOOST_CHECK_EQUAL(
        outside->r_q48 - outside_parent.r_q48,
        MERCATURA_D_PLUS_Q48);
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

    // Negative controller division remains truncate-toward-zero:
    // trunc(-2,254,540,184,409,600 / 840,960) = -2,680,912,510.
    BOOST_CHECK_EQUAL(
        command->q_q48 - parent.q_q48,
        -2'680'912'510);
    BOOST_CHECK_EQUAL(
        command->q_q48,
        4'132'575'984'522'536);

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

BOOST_AUTO_TEST_CASE(downward_rate_limiter_boundary_vectors)
{
    using namespace Consensus;

    BOOST_CHECK_EQUAL(
        MERCATURA_D_MINUS_Q48,
        -536'170'157);

    const auto command_for_gap = [](int64_t gap_q48) {
        McaEmissionState parent;

        parent.height =
            MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT;

        // With q_h == r_h, choose e_h so that:
        //
        // q_(h+1) - r_h
        //   = trunc(e_h / 840,960)
        //   = gap_q48 exactly.
        parent.s_q48 =
            gap_q48 * MERCATURA_CONTROLLER_DIVISOR;
        parent.l_q48 = 0;

        parent.q_q48 =
            MERCATURA_LN_EDGE_SUBSIDY_Q48;
        parent.r_q48 =
            MERCATURA_LN_EDGE_SUBSIDY_Q48;

        parent.subsidy =
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY;
        parent.controller_initialized = true;

        return std::pair{
            parent,
            GetMcaEmissionCommand(
                &parent,
                MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT + 1)};
    };

    // Just inside the lower limiter: no clipping.
    const auto [inside_parent, inside]{
        command_for_gap(MERCATURA_D_MINUS_Q48 + 1)};

    BOOST_REQUIRE(inside.has_value());
    BOOST_CHECK_EQUAL(
        inside->q_q48 - inside_parent.r_q48,
        MERCATURA_D_MINUS_Q48 + 1);
    BOOST_CHECK_EQUAL(
        inside->r_q48 - inside_parent.r_q48,
        MERCATURA_D_MINUS_Q48 + 1);

    // Exactly at d_minus: accepted without changing the value.
    const auto [boundary_parent, boundary]{
        command_for_gap(MERCATURA_D_MINUS_Q48)};

    BOOST_REQUIRE(boundary.has_value());
    BOOST_CHECK_EQUAL(
        boundary->q_q48 - boundary_parent.r_q48,
        MERCATURA_D_MINUS_Q48);
    BOOST_CHECK_EQUAL(
        boundary->r_q48 - boundary_parent.r_q48,
        MERCATURA_D_MINUS_Q48);

    // One Q16.48 unit outside: q keeps the desired value, while r clips
    // to exactly d_minus.
    const auto [outside_parent, outside]{
        command_for_gap(MERCATURA_D_MINUS_Q48 - 1)};

    BOOST_REQUIRE(outside.has_value());
    BOOST_CHECK_EQUAL(
        outside->q_q48 - outside_parent.r_q48,
        MERCATURA_D_MINUS_Q48 - 1);
    BOOST_CHECK_EQUAL(
        outside->r_q48 - outside_parent.r_q48,
        MERCATURA_D_MINUS_Q48);
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

BOOST_AUTO_TEST_CASE(subsidy_floor_entry_persistence_and_exit)
{
    using namespace Consensus;

    const CAmount floor{
        MERCATURA_PERPETUAL_SUBSIDY_FLOOR};

    const auto start_r{
        LogAmountQ48(floor + 1)};
    BOOST_REQUIRE(start_r.has_value());

    // Lock the starting point to an actual unfloored subsidy one base unit
    // above the permanent floor.
    const auto start_candidate{
        ExpQ48ToAmount(*start_r)};
    BOOST_REQUIRE(start_candidate.has_value());
    BOOST_CHECK_EQUAL(
        *start_candidate,
        floor + 1);

    McaEmissionState state;
    state.height =
        MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT;

    // Put desired state far enough below production state that r remains on
    // the exact d_minus limiter for long enough to cross below the floor.
    state.q_q48 =
        *start_r +
        100 * MERCATURA_D_MINUS_Q48;
    state.r_q48 =
        *start_r;

    state.s_q48 = 0;
    state.l_q48 = 0;
    state.subsidy = floor + 1;
    state.controller_initialized = true;

    bool entered_floor{false};

    // Drive production downward until the unconstrained candidate is strictly
    // below the permanent floor.
    for (int step = 0; step < 100; ++step) {
        const int64_t prior_q{state.q_q48};
        const int64_t prior_r{state.r_q48};

        const auto command{
            GetMcaEmissionCommand(
                &state,
                state.height + 1)};
        BOOST_REQUIRE(command.has_value());

        const auto candidate{
            ExpQ48ToAmount(command->r_q48)};
        BOOST_REQUIRE(candidate.has_value());

        // q remains independent of the actual subsidy floor.
        BOOST_CHECK_EQUAL(
            command->q_q48,
            prior_q);

        if (*candidate < floor) {
            entered_floor = true;

            BOOST_CHECK_EQUAL(
                command->subsidy,
                floor);
            BOOST_CHECK_LT(
                command->r_q48,
                prior_r);

            state.height += 1;
            state.q_q48 = command->q_q48;
            state.r_q48 = command->r_q48;
            state.subsidy = command->subsidy;
            break;
        }

        state.height += 1;
        state.q_q48 = command->q_q48;
        state.r_q48 = command->r_q48;
        state.subsidy = command->subsidy;
    }

    BOOST_REQUIRE(entered_floor);

    // Persistence: while the candidate continues downward below the floor,
    // the actual subsidy remains exactly at the floor and r keeps evolving.
    for (int step = 0; step < 3; ++step) {
        const int64_t prior_q{state.q_q48};
        const int64_t prior_r{state.r_q48};

        const auto command{
            GetMcaEmissionCommand(
                &state,
                state.height + 1)};
        BOOST_REQUIRE(command.has_value());

        const auto candidate{
            ExpQ48ToAmount(command->r_q48)};
        BOOST_REQUIRE(candidate.has_value());

        BOOST_CHECK_LT(
            *candidate,
            floor);
        BOOST_CHECK_EQUAL(
            command->subsidy,
            floor);
        BOOST_CHECK_EQUAL(
            command->q_q48,
            prior_q);
        BOOST_CHECK_LT(
            command->r_q48,
            prior_r);

        state.height += 1;
        state.q_q48 = command->q_q48;
        state.r_q48 = command->r_q48;
        state.subsidy = command->subsidy;
    }

    // Reverse desired state through the real controller update:
    //
    //   84,096,000,000,000,000 / 840,960
    //     = +100,000,000,000 Q16.48 units exactly.
    //
    // This moves q well above r. r must then recover only through d_plus.
    state.s_q48 =
        84'096'000'000'000'000;
    state.l_q48 = 0;

    const int64_t reversal_parent_q{
        state.q_q48};
    const int64_t reversal_parent_r{
        state.r_q48};

    const auto reversal{
        GetMcaEmissionCommand(
            &state,
            state.height + 1)};
    BOOST_REQUIRE(reversal.has_value());

    BOOST_CHECK_EQUAL(
        reversal->q_q48 - reversal_parent_q,
        100'000'000'000);
    BOOST_CHECK_EQUAL(
        reversal->r_q48 - reversal_parent_r,
        MERCATURA_D_PLUS_Q48);

    state.height += 1;
    state.q_q48 = reversal->q_q48;
    state.r_q48 = reversal->r_q48;
    state.subsidy = reversal->subsidy;

    // Hold the EMA error at zero after the reversal so q stays fixed while r
    // independently catches up through the upper rate limiter.
    state.s_q48 = 0;
    state.l_q48 = 0;

    bool exited_floor{false};

    for (int step = 0; step < 300; ++step) {
        const int64_t prior_q{state.q_q48};
        const int64_t prior_r{state.r_q48};

        const auto command{
            GetMcaEmissionCommand(
                &state,
                state.height + 1)};
        BOOST_REQUIRE(command.has_value());

        const auto candidate{
            ExpQ48ToAmount(command->r_q48)};
        BOOST_REQUIRE(candidate.has_value());

        BOOST_CHECK_EQUAL(
            command->q_q48,
            prior_q);
        BOOST_CHECK_GT(
            command->r_q48,
            prior_r);

        if (*candidate > floor) {
            exited_floor = true;

            BOOST_CHECK_EQUAL(
                command->subsidy,
                *candidate);
            BOOST_CHECK_GT(
                command->subsidy,
                floor);
            break;
        }

        BOOST_CHECK_EQUAL(
            command->subsidy,
            floor);

        state.height += 1;
        state.q_q48 = command->q_q48;
        state.r_q48 = command->r_q48;
        state.subsidy = command->subsidy;
    }

    BOOST_CHECK(exited_floor);
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
