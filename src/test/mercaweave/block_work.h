// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_BLOCK_WORK_H
#define BITCOIN_TEST_MERCAWEAVE_BLOCK_WORK_H
#include <test/mercaweave/auth_context.h>
#include <primitives/block.h>
#include <limits>

namespace mercaweave::research {
enum class BudgetError { None, Overflow, Bytes, Weight, WitnessItems, Sigops, Context, DuplicateOutpoint };
struct ArithmeticError {};
uint64_t Add(uint64_t a, uint64_t b);
uint64_t Multiply(uint64_t a, uint64_t b);
struct Charge {
    uint64_t transactions{0}, inputs{0}, v3_inputs{0}, groups{0};
    uint64_t tx_bytes{0}, stripped_bytes{0}, witness_items{0}, existing_cost{0}, group_cost{0};
    uint64_t manifest_bytes{0}, component_bytes{0};
};
// Fixed reference snapshot, not network configuration or an activation rule.
// Legacy weight is measured; optional weight budget is off by default because
// current Mercatura's capacity check uses full bytes. All budgets are explicit.
struct Budget {
    uint64_t bytes{MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES};
    uint64_t witness_items{MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES / Consensus::WITNESS_ITEM_CAPACITY_DIVISOR};
    uint64_t sigops{MAX_BLOCK_SIGOPS_COST};
    uint64_t weight{std::numeric_limits<uint64_t>::max()};
};
CMutableTransaction ResearchCoinbase();
Charge Estimate(const Request& request);
class Ledger {
    const Budget m_budget;
    Charge m_total;
    BudgetError m_error{BudgetError::None};
public:
    explicit Ledger(Budget b = {}) : m_budget(b) {}
    // Reserve atomically BEFORE verification. An invalid authorization does
    // not refund this work. Any failed reservation poisons this ledger.
    BudgetError Reserve(const Charge&);
    const Charge& Total() const { return m_total; }
    BudgetError Error() const { return m_error; }
    uint64_t BlockBytes() const;
    uint64_t BlockWeight() const;
};
struct Receipt {
    BudgetError budget_error{BudgetError::None};
    ContextError context_error{ContextError::None};
    std::optional<Error> validation_error;
    Statistics work;
    size_t native_calls{0};
};
struct Workload {
    bool authorized{false};
    Charge charged;
    BudgetError budget_error{BudgetError::None};
    std::vector<Receipt> receipts;
    uint64_t block_bytes{0}, block_weight{0}, witness_bytes{0};
    uint64_t actual_groups{0}, actual_native{0}, ownership_checks{0}, public_key_bytes{0};
    uint64_t body_passes{0}, manifest_passes{0}, component_bytes{0}, manifest_bytes{0};
};
// No production block acceptance is implied. Requests carry synthetic/test UTXOs
// or caller-authenticated context; this harness does not check PoW/maturity.
Workload VerifyWorkload(const std::vector<RequestPtr>&, Budget = {});
// Real inherited CBlock serialization, including header/count and a synthetic
// coinbase. Only for measurement; no header/merkle/PoW validity is claimed.
CBlock SerializeWorkload(const std::vector<RequestPtr>&);
} // namespace mercaweave::research
#endif // BITCOIN_TEST_MERCAWEAVE_BLOCK_WORK_H
