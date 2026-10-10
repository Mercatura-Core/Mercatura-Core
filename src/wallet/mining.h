// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_WALLET_MINING_H
#define BITCOIN_WALLET_MINING_H
#include <addresstype.h>
#include <util/result.h>

namespace interfaces { class Wallet; }
namespace wallet {
inline constexpr const char* MINING_REWARD_LABEL{"Mercatura mining reward"};
// Reuse an owned, persisted public PQ destination even when locked. Creating
// one uses the normal wallet API and therefore requires an unlocked PQ seed.
util::Result<CTxDestination> GetMiningDestination(interfaces::Wallet& wallet);
} // namespace wallet
#endif // BITCOIN_WALLET_MINING_H
