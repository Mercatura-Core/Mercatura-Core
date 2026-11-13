// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_WALLET_MINING_H
#define BITCOIN_WALLET_MINING_H
#include <addresstype.h>
#include <string>
#include <util/result.h>

namespace interfaces { class Wallet; }
namespace wallet {
inline constexpr const char* MINING_REWARD_LABEL{"Mercatura mining reward"};
// Payout: 1 standard v2, 2 activated MercaWeave v3. Other values are rejected.
// An explicit selection must validate as-is; never silently substitute it.
// Fresh derivation requires unlock and uses the dedicated v3 mining purpose.
// create=false performs a read-only lookup for GUI previews.
// Public owned destinations can be reused while locked when authenticated.
util::Result<CTxDestination> GetMiningDestination(interfaces::Wallet& wallet, int payout = 1, const std::string& selected = {}, bool fresh = false, bool create = true);
} // namespace wallet
#endif // BITCOIN_WALLET_MINING_H
