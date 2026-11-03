// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "block_work.h"
#include <set>

namespace mercaweave::research {
uint64_t Add(uint64_t a, uint64_t b)
{
    if (a > UINT64_MAX - b) throw ArithmeticError{};
    return a + b;
}
uint64_t Multiply(uint64_t a, uint64_t b)
{
    if (b != 0 && a > UINT64_MAX / b) throw ArithmeticError{};
    return a * b;
}
CMutableTransaction ResearchCoinbase()
{
    CMutableTransaction tx;
    tx.vin.emplace_back();
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(0, CScript{} << OP_RETURN);
    return tx;
}
Charge Estimate(const Request& r)
{
    if (r.spent.size() != r.tx.vin.size()) throw Failure(Error::Context);
    if (r.tx.vin.size() > UINT32_MAX) throw Failure(Error::Format);
    const auto sizes = Measure(r.tx);
    Charge c;
    c.transactions = 1;
    c.inputs = r.tx.vin.size();
    c.tx_bytes = sizes.total;
    c.stripped_bytes = sizes.stripped;
    std::vector<uint64_t> members(r.tx.vin.size(), 0);
    std::vector<uint32_t> anchors;
    for (size_t i = 0; i < r.tx.vin.size(); ++i) {
        const auto& in = r.tx.vin[i];
        const auto& spent = r.spent[i];
        const auto& w = in.scriptWitness.stack;
        c.witness_items = Add(c.witness_items, w.size());
        uint64_t legacy = in.scriptSig.GetSigOpCount(false);
        if (spent.scriptPubKey.IsPayToScriptHash()) legacy = Add(legacy, spent.scriptPubKey.GetSigOpCount(in.scriptSig));
        c.existing_cost = Add(c.existing_cost, Multiply(legacy, WITNESS_SCALE_FACTOR));
        c.existing_cost = Add(c.existing_cost, CountWitnessSigOps(in.scriptSig, spent.scriptPubKey, in.scriptWitness, r.flags));
        int version;
        Bytes program;
        if (spent.scriptPubKey.IsWitnessProgram(version, program) && version == 3) {
            c.v3_inputs = Add(c.v3_inputs, 1);
            if (w.size() == 3) {
                c.groups = Add(c.groups, 1); // Potential anchors, NOT deduped for invalid transactions.
                anchors.push_back(static_cast<uint32_t>(i));
                members[i] = Add(members[i], 1);
            } else if (w.size() == 2 && w[1].size() == 4) {
                const auto a = ReadLE32(w[1].data());
                if (a < i) members[a] = Add(members[a], 1);
            }
        }
        c.component_bytes = Add(c.component_bytes, Add(48, GetSerializeSize(spent.scriptPubKey)));
    }
    for (const auto& out : r.tx.vout) {
        c.existing_cost = Add(c.existing_cost, Multiply(out.scriptPubKey.GetSigOpCount(false), WITNESS_SCALE_FACTOR));
        c.component_bytes = Add(c.component_bytes, GetSerializeSize(out));
    }
    c.component_bytes = Add(c.component_bytes, 296);
    c.group_cost = Add(Multiply(4, c.groups), c.v3_inputs);
    c.manifest_bytes = GetSizeOfCompactSize(c.groups);
    for (auto a : anchors) c.manifest_bytes = Add(c.manifest_bytes,
        Add(1956 + GetSizeOfCompactSize(members[a]), Multiply(36, members[a])));
    return c;
}
uint64_t Ledger::BlockBytes() const
{
    return Add(Add(80 + GetSizeOfCompactSize(Add(m_total.transactions, 1)), GetSerializeSize(TX_WITH_WITNESS(ResearchCoinbase()))), m_total.tx_bytes);
}
uint64_t Ledger::BlockWeight() const
{
    const auto base = Add(Add(80 + GetSizeOfCompactSize(Add(m_total.transactions, 1)), GetSerializeSize(TX_NO_WITNESS(ResearchCoinbase()))), m_total.stripped_bytes);
    return Add(Multiply(3, base), BlockBytes());
}
BudgetError Ledger::Reserve(const Charge& c)
{
    if (m_error != BudgetError::None) return m_error;
    try {
        if (c.stripped_bytes > c.tx_bytes || c.v3_inputs > c.inputs ||
            c.group_cost != Add(Multiply(4, c.groups), c.v3_inputs)) return m_error = BudgetError::Context;
        auto candidate = m_total;
        candidate.transactions = Add(candidate.transactions, c.transactions);
        candidate.inputs = Add(candidate.inputs, c.inputs);
        candidate.v3_inputs = Add(candidate.v3_inputs, c.v3_inputs);
        candidate.groups = Add(candidate.groups, c.groups);
        candidate.tx_bytes = Add(candidate.tx_bytes, c.tx_bytes);
        candidate.stripped_bytes = Add(candidate.stripped_bytes, c.stripped_bytes);
        candidate.witness_items = Add(candidate.witness_items, c.witness_items);
        candidate.existing_cost = Add(candidate.existing_cost, c.existing_cost);
        candidate.group_cost = Add(candidate.group_cost, c.group_cost);
        candidate.manifest_bytes = Add(candidate.manifest_bytes, c.manifest_bytes);
        candidate.component_bytes = Add(candidate.component_bytes, c.component_bytes);
        const auto overhead = Add(80 + GetSizeOfCompactSize(Add(candidate.transactions, 1)), GetSerializeSize(TX_WITH_WITNESS(ResearchCoinbase())));
        const auto bytes = Add(overhead, candidate.tx_bytes);
        const auto weight = Add(Multiply(3, Add(overhead, candidate.stripped_bytes)), bytes);
        if (bytes > m_budget.bytes) return m_error = BudgetError::Bytes;
        if (weight > m_budget.weight) return m_error = BudgetError::Weight;
        if (candidate.witness_items > m_budget.witness_items) return m_error = BudgetError::WitnessItems;
        if (Add(candidate.existing_cost, candidate.group_cost) > m_budget.sigops) return m_error = BudgetError::Sigops;
        m_total = candidate;
    } catch (const ArithmeticError&) { return m_error = BudgetError::Overflow; }
    return BudgetError::None;
}
CBlock SerializeWorkload(const std::vector<RequestPtr>& requests)
{
    CBlock b;
    b.vtx.push_back(MakeTransactionRef(ResearchCoinbase()));
    for (const auto& r : requests) {
        if (!r) throw Failure(Error::Context);
        b.vtx.push_back(MakeTransactionRef(r->tx));
    }
    return b;
}
Workload VerifyWorkload(const std::vector<RequestPtr>& requests, Budget budget)
{
    Workload result;
    Ledger ledger{budget};
    bool valid{true};
    std::set<COutPoint> spent_in_block;
    for (const auto& request : requests) {
        Receipt receipt;
        if (!request) { receipt.budget_error = BudgetError::Context; result.receipts.push_back(receipt); valid = false; break; }
        try {
            const auto charge = Estimate(*request);
            receipt.budget_error = ledger.Reserve(charge);
            if (receipt.budget_error != BudgetError::None) { result.receipts.push_back(receipt); valid = false; break; }
            for (const auto& in : request->tx.vin) if (!spent_in_block.insert(in.prevout).second) receipt.budget_error = BudgetError::DuplicateOutpoint;
            if (receipt.budget_error != BudgetError::None) { result.receipts.push_back(receipt); valid = false; continue; }
            const auto prepared = Prepare(request);
            receipt.context_error = prepared.error;
            receipt.validation_error = prepared.validation_error;
            receipt.work = prepared.work;
            receipt.native_calls = prepared.native_pq_calls;
            if (!prepared.context) valid = false;
            else {
                const auto checks = Bind(request, prepared.context);
                if (!checks.checks) { valid = false; receipt.context_error = checks.error; }
                else for (size_t i = 0; i < request->tx.vin.size(); ++i) {
                    if (checks.checks->Check(static_cast<uint32_t>(i)) != ContextError::None) { valid = false; receipt.context_error = ContextError::Invariant; }
                }
            }
            result.actual_groups = Add(result.actual_groups, prepared.work.verification_calls);
            result.actual_native = Add(result.actual_native, prepared.native_pq_calls);
            result.ownership_checks = Add(result.ownership_checks, prepared.work.ownership_checks);
            result.public_key_bytes = Add(result.public_key_bytes, Multiply(1952, prepared.work.ownership_checks));
            result.body_passes = Add(result.body_passes, prepared.work.body_hash_passes);
            result.manifest_passes = Add(result.manifest_passes, prepared.work.manifest_hash_passes);
            result.component_bytes = Add(result.component_bytes, Multiply(charge.component_bytes, prepared.work.body_hash_passes));
            result.manifest_bytes = Add(result.manifest_bytes, Multiply(charge.manifest_bytes, prepared.work.manifest_hash_passes));
        } catch (const ArithmeticError&) { receipt.budget_error = BudgetError::Overflow; valid = false; }
          catch (const Failure&) { receipt.budget_error = BudgetError::Context; valid = false; }
        result.receipts.push_back(receipt);
    }
    result.charged = ledger.Total();
    result.budget_error = ledger.Error();
    if (result.budget_error == BudgetError::None) for (const auto& r : result.receipts) {
        if (r.budget_error != BudgetError::None) { result.budget_error = r.budget_error; break; }
    }
    result.block_bytes = ledger.BlockBytes();
    result.block_weight = ledger.BlockWeight();
    result.witness_bytes = result.charged.tx_bytes - result.charged.stripped_bytes;
    result.authorized = valid && result.receipts.size() == requests.size() && result.budget_error == BudgetError::None;
    return result;
}
} // namespace mercaweave::research
