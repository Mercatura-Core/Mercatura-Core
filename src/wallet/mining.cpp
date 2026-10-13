// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <wallet/mining.h>
#include <wallet/types.h>

#include <interfaces/wallet.h>
#include <outputtype.h>
#include <util/translation.h>

namespace wallet {
util::Result<CTxDestination> GetMiningDestination(interfaces::Wallet& wallet)
{
    const auto addresses{wallet.getAddresses()};
    for (const auto& address : addresses) {
        if (address.name == MINING_REWARD_LABEL && address.is_mine &&
            std::holds_alternative<WitnessV2MercaturaPQ>(address.dest) && wallet.isSpendable(address.dest)) return address.dest;
    }
    // Reuse an eligible receiving address even if its mining label was edited.
    // Preserve the user's label and never repurpose sending/change addresses.
    for (const auto& address : addresses) {
        if (address.purpose == AddressPurpose::RECEIVE && address.is_mine &&
            std::holds_alternative<WitnessV2MercaturaPQ>(address.dest) && wallet.isSpendable(address.dest)) return address.dest;
    }
    if (wallet.isLocked()) return util::Error{Untranslated("Unlock this wallet once to create its native PQ mining reward address, then start mining again")};
    auto destination{wallet.getNewDestination(OutputType::BECH32M, MINING_REWARD_LABEL)};
    if (!destination) return destination;
    if (!std::holds_alternative<WitnessV2MercaturaPQ>(*destination) || !wallet.isSpendable(*destination)) {
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
