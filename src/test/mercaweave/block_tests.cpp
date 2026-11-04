// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/block_work.h>
#include <test/mercaweave/context_fixtures.h>
#include <iostream>
#include <random>

using namespace mercaweave;
using namespace mercaweave::test;
using namespace mercaweave::research;
namespace {
size_t assertions{0}, overflows{0}, mutations{0};
void Check(bool b, const char* name) { ++assertions; if (!b) throw std::runtime_error(name); }
RequestPtr View(const Fixture& f) { return Capture(f.tx, f.spent, f.genesis, MANDATORY_SCRIPT_VERIFY_FLAGS); }
void Tests()
{
    Check(Add(UINT64_MAX - 1, 1) == UINT64_MAX && Multiply(UINT64_MAX, 1) == UINT64_MAX && Multiply(UINT64_MAX, 0) == 0,
          "checked integer exact boundaries");
    try { Add(UINT64_MAX, 1); Check(false, "addition overflow accepted"); } catch (const ArithmeticError&) { ++overflows; }
    try { Multiply(UINT64_MAX, 2); Check(false, "multiplication overflow accepted"); } catch (const ArithmeticError&) { ++overflows; }
    auto f = MixedFixture(3, 1, 1);
    const auto request = View(f);
    const auto charge = Estimate(*request);
    Check(charge.group_cost == 7 && charge.existing_cost == 4 && charge.v3_inputs == 3 && charge.inputs == 4,
          "v3 proposal and actual native-v2 cost are distinct");
    Ledger base;
    Check(base.Reserve(charge) == BudgetError::None, "admit reference charge");
    const auto exact_bytes = base.BlockBytes();
    Budget exact;
    exact.bytes = exact_bytes;
    exact.witness_items = charge.witness_items;
    exact.sigops = charge.group_cost + charge.existing_cost;
    exact.weight = base.BlockWeight();
    Ledger edge{exact};
    Check(edge.Reserve(charge) == BudgetError::None, "all budgets accept at exact edge");
    for (int field : {0, 1, 2, 3}) {
        auto budget = exact;
        if (field == 0) --budget.bytes;
        if (field == 1) --budget.witness_items;
        if (field == 2) --budget.sigops;
        if (field == 3) --budget.weight;
        Ledger denied{budget};
        const auto error = denied.Reserve(charge);
        Check(error != BudgetError::None && denied.Total().transactions == 0, "one-unit over budget cannot partially charge");
        Check(denied.Reserve({}) == error, "failed budget cannot be reused as a successful ledger");
    }
    auto overflow = charge;
    overflow.groups = UINT64_MAX;
    Ledger huge;
    Check(huge.Reserve(overflow) == BudgetError::Overflow, "overflow in derived work charge rejects"); ++overflows;
    auto unlimited = Budget{UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    for (int field : {0,1,2,3}) {
        Charge synthetic;
        if (field == 0) synthetic.transactions = UINT64_MAX;
        if (field == 1) synthetic.tx_bytes = UINT64_MAX;
        if (field == 2) synthetic.tx_bytes = synthetic.stripped_bytes = UINT64_MAX / 2;
        if (field == 3) synthetic.existing_cost = UINT64_MAX;
        Ledger ledger{unlimited};
        const auto error = ledger.Reserve(synthetic);
        if (field == 3) Check(error == BudgetError::None && ledger.Reserve(charge) == BudgetError::Overflow, "cumulative sigop overflow");
        else Check(error == BudgetError::Overflow, "serialized cumulative accounting overflow");
        ++overflows;
    }
    auto valid = VerifyWorkload({request});
    Check(valid.authorized && valid.actual_groups == 1 && valid.actual_native == 1 && valid.ownership_checks == 3, "complete real mixed workload");
    Check(valid.body_passes == 1 && valid.manifest_passes == 1 && valid.public_key_bytes == 3 * 1952, "actual hash passes/PK volume tracked once");
    const auto block = SerializeWorkload({request});
    Check(valid.block_bytes == GetSerializeSize(TX_WITH_WITNESS(block)), "actual CBlock serialization matches ledger");
    Check(valid.block_weight == GetSerializeSize(TX_NO_WITNESS(block)) * 3 + GetSerializeSize(TX_WITH_WITNESS(block)), "actual legacy block weight distinct");
    auto duplicate = VerifyWorkload({request, request});
    Check(!duplicate.authorized && duplicate.budget_error == BudgetError::DuplicateOutpoint && duplicate.charged.transactions == 2,
          "cross-transaction double spend rejected after charging attempt");
    f.tx.vin[0].scriptWitness.stack[0][100] ^= 1;
    const auto invalid = VerifyWorkload({View(f)});
    Check(!invalid.authorized && invalid.charged.transactions == 1 && invalid.charged.tx_bytes == charge.tx_bytes &&
          invalid.actual_groups == 1 && invalid.ownership_checks == 3, "late invalid transaction retains every resource charge");
    Check(invalid.receipts[0].validation_error == Error::Signature, "late signature reason explicit");
    f.tx.vin[0].scriptWitness.stack.pop_back();
    const auto early = VerifyWorkload({View(f)});
    Check(!early.authorized && early.charged.transactions == 1 && early.charged.tx_bytes > 0 && early.ownership_checks == 0,
          "early invalid transaction also charged, no skipped accounting");
    Check(!VerifyWorkload({{}}).authorized, "null transaction never authorizes block workload");
    std::mt19937_64 rng{0x574f303342554447ULL};
    for (size_t n = 0; n < 5000; ++n) {
        auto budget = Budget{rng(), rng(), rng(), rng()};
        auto c = charge;
        c.existing_cost = rng();
        c.tx_bytes = rng();
        c.stripped_bytes = c.tx_bytes;
        Ledger ledger{budget};
        const auto outcome = ledger.Reserve(c);
        if (outcome == BudgetError::None) Check(ledger.BlockBytes() <= budget.bytes && ledger.BlockWeight() <= budget.weight,
                                              "seeded counters never wrap into passing budgets");
        else Check(ledger.Total().transactions == 0 && ledger.Error() != BudgetError::None, "seeded denial atomic and explicit");
        ++mutations;
    }
}
}
int main()
{
    try { Tests(); std::cout << "PASS block assertions=" << assertions << " overflows=" << overflows << " mutations=" << mutations << '\n'; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL block: " << e.what() << '\n'; return 1; }
}
