// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/accounting.h>

#include <hash.h>

#include <algorithm>
#include <stdexcept>

namespace pool {
std::string Digest(const std::string& domain, const std::string& text) { return (HashWriter{} << domain << text).GetHash().GetHex(); }
Window SelectWindow(const std::vector<Share>& shares, int64_t cutoff, const Number& limit)
{
    if (limit.Zero()) throw std::invalid_argument("zero work window");
    Window w;
    int64_t previous = INT64_MAX;
    for (const auto& s : shares) {
        if (s.sequence <= 0 || s.sequence >= previous || s.work.Zero()) throw std::invalid_argument("invalid share order or score");
        previous = s.sequence;
        if (s.sequence > cutoff) continue;
        const auto remaining = limit - w.total;
        const auto used = std::min(s.work, remaining);
        if (used.Zero()) break;
        w.weights[s.script] += used;
        w.total += used;
        w.oldest = s.sequence;
        w.oldest_used = used;
        if (w.total == limit) break;
    }
    return w;
}
std::vector<Allocation> Allocate(CAmount reward, const Window& w, const std::string& seed)
{
    if (!MoneyRange(reward) || w.total.Zero()) throw std::invalid_argument("invalid reward or empty window");
    std::vector<Allocation> out;
    Number sum;
    CAmount paid = 0;
    for (const auto& [script, work] : w.weights) {
        if (work.Zero()) throw std::invalid_argument("zero recipient work");
        if (work > w.total - sum) throw std::invalid_argument("inconsistent work weights");
        sum += work;
        auto [q, r] = Number::DivMod(Number{static_cast<uint64_t>(reward)} * work, w.total);
        const auto units = q.Uint64();
        if (units > static_cast<uint64_t>(reward - paid)) throw std::overflow_error("allocation overflow");
        const auto amount = static_cast<CAmount>(units);
        paid += amount;
        out.push_back({script, work, amount, std::move(r), Digest("MCA-PPLNS/1/remainder", seed + ":" + script)});
    }
    if (sum != w.total) throw std::invalid_argument("inconsistent work weights");
    std::vector<size_t> order;
    for (size_t i = 0; i < out.size(); ++i)
        order.push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (out[a].remainder != out[b].remainder) return out[a].remainder > out[b].remainder;
        if (out[a].tie != out[b].tie) return out[a].tie < out[b].tie;
        return out[a].script < out[b].script; // Only a cryptographic digest collision.
    });
    const auto leftover = static_cast<uint64_t>(reward - paid);
    if (leftover > out.size()) throw std::logic_error("invalid largest remainder");
    for (size_t i = 0; i < leftover; ++i)
        ++out[order[i]].amount;
    return out; // Canonical script-byte order, including auditable zero allocations.
}
UniValue AllocationsJson(const std::vector<Allocation>& allocations)
{
    UniValue a{UniValue::VARR};
    for (const auto& p : allocations) {
        UniValue o{UniValue::VOBJ};
        o.pushKV("script", p.script);
        o.pushKV("work", p.work.Decimal());
        o.pushKV("amount", p.amount);
        o.pushKV("remainder", p.remainder.Decimal());
        o.pushKV("tie", p.tie);
        a.push_back(std::move(o));
    }
    return a;
}
} // namespace pool
