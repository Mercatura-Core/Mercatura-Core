// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_ACCOUNTING_H
#define BITCOIN_POOL_ACCOUNTING_H

#include <consensus/amount.h>
#include <pool/number.h>
#include <univalue.h>

#include <map>
#include <string>
#include <vector>

namespace pool {
inline constexpr int ACCOUNTING_VERSION{1};
inline constexpr std::string_view MARKER{"MCA-POOL/1"};
struct Share {
    int64_t sequence;
    std::string script;
    Number work;
};
struct Window {
    std::map<std::string, Number> weights;
    Number total;
    int64_t oldest{0};
    Number oldest_used;
};
// Shares are newest first. A share's identity never follows the current session.
Window SelectWindow(const std::vector<Share>& newest_first, int64_t cutoff, const Number& limit);
struct Allocation {
    std::string script;
    Number work;
    CAmount amount;
    Number remainder;
    std::string tie;
};
std::vector<Allocation> Allocate(CAmount reward, const Window& window, const std::string& seed);
std::string Digest(const std::string& domain, const std::string& text);
UniValue AllocationsJson(const std::vector<Allocation>& allocations);
} // namespace pool
#endif // BITCOIN_POOL_ACCOUNTING_H
