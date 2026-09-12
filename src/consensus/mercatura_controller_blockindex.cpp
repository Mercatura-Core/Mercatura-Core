// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <consensus/mercatura_controller.h>

#include <limits>

namespace Consensus {

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
