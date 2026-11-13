// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <wallet/mining.h>
#include <wallet/types.h>

#include <interfaces/wallet.h>
#include <outputtype.h>
#include <key_io.h>
#include <script/solver.h>
#include <util/translation.h>

namespace wallet {
util::Result<CTxDestination> GetMiningDestination(interfaces::Wallet& wallet, int payout, const std::string& selected, bool fresh)
{
    const auto is_v3 = [](const CTxDestination& dest) {
        std::vector<std::vector<unsigned char>> solutions;
        return Solver(GetScriptForDestination(dest), solutions) == TxoutType::WITNESS_V3_MERCAWEAVE;
    };
    const bool v3{payout != 1 && wallet.mercaweaveAvailable()};
    if (payout < 0 || payout > 2 || (payout == 2 && !v3))
        return util::Error{Untranslated("MercaWeave mining payouts are unavailable")};
    const auto eligible = [&](const CTxDestination& dest) {
        return wallet.isSpendable(dest) &&
            (std::holds_alternative<WitnessV2MercaturaPQ>(dest) ||
             (wallet.mercaweaveAvailable() && is_v3(dest)));
    };
    if (!selected.empty()) {
        auto dest{DecodeDestination(selected)};
        if (!eligible(dest) || (payout == 1 && !std::holds_alternative<WitnessV2MercaturaPQ>(dest)) ||
            (payout == 2 && !is_v3(dest)))
            return util::Error{Untranslated("Selected mining address is unowned, unsupported, inactive, or incompatible with the payout type")};
        return dest;
    }
    if (!fresh) for (const auto& address : wallet.getAddresses()) {
        if ((address.name == MINING_REWARD_LABEL || address.purpose == AddressPurpose::RECEIVE) && eligible(address.dest) &&
            (v3 == is_v3(address.dest))) return address.dest;
    }
    if (wallet.isLocked()) return util::Error{Untranslated("Unlock this wallet once to create its native PQ mining reward address, then start mining again")};
    auto destination{v3 ? wallet.getNewMiningMercaWeaveDestination(MINING_REWARD_LABEL) : wallet.getNewDestination(OutputType::BECH32M, MINING_REWARD_LABEL)};
    if (!destination) return destination;
    if (!eligible(*destination)) {
        return util::Error{Untranslated("The selected wallet did not provide an owned native Mercatura PQ destination")};
    }
    // Require successful persistence of the public address-book marker. The
    // wallet's existing PQ derivation API already persists ownership metadata.
    if (!wallet.setAddressBook(*destination, MINING_REWARD_LABEL, AddressPurpose::RECEIVE)) {
        return util::Error{Untranslated("Could not persist the mining reward address")};
    }
    return destination;
}
} // namespace wallet
