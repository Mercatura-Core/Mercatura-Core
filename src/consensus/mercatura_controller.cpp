// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <consensus/mercatura_controller.h>

#include <consensus/mercatura_emission.h>
#include <consensus/mercatura_fixedpoint.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace Consensus {
namespace {

bool CheckedAdd(int64_t a, int64_t b, int64_t& result)
{
    constexpr int64_t MIN{std::numeric_limits<int64_t>::min()};
    constexpr int64_t MAX{std::numeric_limits<int64_t>::max()};

    if (b > 0 && a > MAX - b) {
        return false;
    }

    if (b < 0 && a < MIN - b) {
        return false;
    }

    result = a + b;
    return true;
}

bool CheckedSubtract(int64_t a, int64_t b, int64_t& result)
{
    constexpr int64_t MIN{std::numeric_limits<int64_t>::min()};
    constexpr int64_t MAX{std::numeric_limits<int64_t>::max()};

    if (b > 0 && a < MIN + b) {
        return false;
    }

    if (b < 0 && a > MAX + b) {
        return false;
    }

    result = a - b;
    return true;
}

bool CheckedDouble(int64_t value, int64_t& result)
{
    constexpr int64_t MIN{std::numeric_limits<int64_t>::min()};
    constexpr int64_t MAX{std::numeric_limits<int64_t>::max()};

    if (value > MAX / 2 || value < MIN / 2) {
        return false;
    }

    result = value * 2;
    return true;
}

std::optional<int64_t> UpdateEma(
    int64_t previous_q48,
    int64_t signal_q48,
    int64_t divisor)
{
    int64_t difference;

    if (!CheckedSubtract(
            signal_q48,
            previous_q48,
            difference)) {
        return std::nullopt;
    }

    int64_t numerator;

    if (!CheckedDouble(difference, numerator)) {
        return std::nullopt;
    }

    const int64_t adjustment{
        DivideQ48TowardZero(
            numerator,
            divisor)};

    int64_t result;

    if (!CheckedAdd(
            previous_q48,
            adjustment,
            result)) {
        return std::nullopt;
    }

    return result;
}

std::optional<int64_t> CalculateSignalQ48(
    const arith_uint256& block_work,
    CAmount protocol_subsidy)
{
    if (block_work == 0 || protocol_subsidy <= 0) {
        return std::nullopt;
    }

    const auto log_work{
        LogUint256Q48(block_work)};

    const auto log_subsidy{
        LogAmountQ48(protocol_subsidy)};

    if (!log_work || !log_subsidy) {
        return std::nullopt;
    }

    int64_t signal;

    if (!CheckedSubtract(
            *log_work,
            *log_subsidy,
            signal)) {
        return std::nullopt;
    }

    return signal;
}

std::optional<McaEmissionCommand> GetAdaptiveCommand(
    const McaEmissionState& parent)
{
    if (!parent.controller_initialized ||
        parent.height < MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT) {
        return std::nullopt;
    }

    // e_h = s_h - l_h
    int64_t error_q48;

    if (!CheckedSubtract(
            parent.s_q48,
            parent.l_q48,
            error_q48)) {
        return std::nullopt;
    }

    // q_(h+1) = q_h + e_h / 840,960
    const int64_t q_adjustment{
        DivideQ48TowardZero(
            error_q48,
            MERCATURA_CONTROLLER_DIVISOR)};

    int64_t next_q;

    if (!CheckedAdd(
            parent.q_q48,
            q_adjustment,
            next_q)) {
        return std::nullopt;
    }

    // r_(h+1) = r_h + clip(q_(h+1) - r_h, d_minus, d_plus)
    int64_t gap;

    if (!CheckedSubtract(
            next_q,
            parent.r_q48,
            gap)) {
        return std::nullopt;
    }

    const int64_t limited_delta{
        std::clamp(
            gap,
            MERCATURA_D_MINUS_Q48,
            MERCATURA_D_PLUS_Q48)};

    int64_t next_r;

    if (!CheckedAdd(
            parent.r_q48,
            limited_delta,
            next_r)) {
        return std::nullopt;
    }

    const auto candidate{
        ExpQ48ToAmount(next_r)};

    if (!candidate) {
        return std::nullopt;
    }

    McaEmissionCommand command;
    command.q_q48 = next_q;
    command.r_q48 = next_r;
    command.subsidy = std::max(
        *candidate,
        MERCATURA_PERPETUAL_SUBSIDY_FLOOR);
    command.controller_initialized = true;

    return command;
}

} // namespace

std::optional<McaEmissionCommand> GetMcaEmissionCommand(
    const McaEmissionState* parent,
    int height)
{
    if (height < 1) {
        return std::nullopt;
    }

    if (height == 1) {
        if (parent != nullptr) {
            return std::nullopt;
        }
    } else {
        if (parent == nullptr ||
            parent->height != height - 1) {
            return std::nullopt;
        }
    }

    if (height <= MERCATURA_BOOTSTRAP_LAST_HEIGHT) {
        const CAmount subsidy{
            GetMercaturaBootstrapSubsidy(height)};

        if (subsidy <= 0) {
            return std::nullopt;
        }

        McaEmissionCommand command;
        command.subsidy = subsidy;
        return command;
    }

    if (height == MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT) {
        McaEmissionCommand command;
        command.subsidy =
            MERCATURA_BOOTSTRAP_EDGE_SUBSIDY;
        command.q_q48 =
            MERCATURA_LN_EDGE_SUBSIDY_Q48;
        command.r_q48 =
            MERCATURA_LN_EDGE_SUBSIDY_Q48;
        command.controller_initialized = true;

        return command;
    }

    return GetAdaptiveCommand(*parent);
}

std::optional<McaEmissionState> AdvanceMcaEmissionState(
    const McaEmissionState* parent,
    int height,
    const arith_uint256& block_work,
    CAmount protocol_subsidy)
{
    const auto command{
        GetMcaEmissionCommand(parent, height)};

    if (!command ||
        protocol_subsidy != command->subsidy) {
        return std::nullopt;
    }

    const auto signal_q48{
        CalculateSignalQ48(
            block_work,
            protocol_subsidy)};

    if (!signal_q48) {
        return std::nullopt;
    }

    McaEmissionState state;

    state.height = height;
    state.q_q48 = command->q_q48;
    state.r_q48 = command->r_q48;
    state.subsidy = command->subsidy;
    state.controller_initialized =
        command->controller_initialized;

    if (height == 1) {
        // Consensus initialization:
        // s_1 = l_1 = z_1.
        state.s_q48 = *signal_q48;
        state.l_q48 = *signal_q48;
        return state;
    }

    const auto next_s{
        UpdateEma(
            parent->s_q48,
            *signal_q48,
            MERCATURA_SHORT_EMA_DIVISOR)};

    const auto next_l{
        UpdateEma(
            parent->l_q48,
            *signal_q48,
            MERCATURA_LONG_EMA_DIVISOR)};

    if (!next_s || !next_l) {
        return std::nullopt;
    }

    state.s_q48 = *next_s;
    state.l_q48 = *next_l;

    return state;
}

std::optional<McaEmissionState> DeriveMcaEmissionState(
    const McaEmissionState* parent,
    int height,
    const arith_uint256& block_work)
{
    const auto command{
        GetMcaEmissionCommand(parent, height)};

    if (!command) {
        return std::nullopt;
    }

    return AdvanceMcaEmissionState(
        parent,
        height,
        block_work,
        command->subsidy);
}

std::optional<CAmount> GetMcaBlockSubsidy(
    const CBlockIndex& block)
{
    if (block.nHeight <= 0 ||
        !block.m_mca_emission_state) {
        return std::nullopt;
    }

    const McaEmissionState& state{
        *block.m_mca_emission_state};

    if (state.height != block.nHeight ||
        state.subsidy <= 0 ||
        state.subsidy > MAX_MONEY) {
        return std::nullopt;
    }

    return state.subsidy;
}

std::optional<CAmount> GetNextMcaBlockSubsidy(
    const CBlockIndex& parent)
{
    if (parent.nHeight < 0 ||
        parent.nHeight == std::numeric_limits<int>::max()) {
        return std::nullopt;
    }

    const int height{parent.nHeight + 1};
    const McaEmissionState* emission_parent{nullptr};

    if (height > 1) {
        if (!parent.m_mca_emission_state ||
            parent.m_mca_emission_state->height != parent.nHeight) {
            return std::nullopt;
        }

        emission_parent =
            &*parent.m_mca_emission_state;
    }

    const auto command{
        GetMcaEmissionCommand(
            emission_parent,
            height)};

    if (!command) {
        return std::nullopt;
    }

    return command->subsidy;
}

} // namespace Consensus
