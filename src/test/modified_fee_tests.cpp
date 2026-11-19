// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <coins.h>
#include <node/mini_miner.h>
#include <policy/feerate.h>
#include <test/util/setup_common.h>
#include <test/util/txmempool.h>
#include <txmempool.h>
#include <util/translation.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>
#include <limits>
#include <stdexcept>

namespace {
constexpr CAmount MAX{std::numeric_limits<CAmount>::max()};
constexpr CAmount MIN{std::numeric_limits<CAmount>::min()};

class FeePool : public CTxMemPool
{
public:
    using CTxMemPool::CTxMemPool;
    using CTxMemPool::FeeExposure;
    using CTxMemPool::GetMinFee;
    using CTxMemPool::m_fee_exposure;
};

CTransactionRef Transaction(COutPoint input, CAmount value = 9'000)
{
    CMutableTransaction tx;
    tx.vin.emplace_back(input);
    tx.vout.emplace_back(value, CScript{} << OP_TRUE << OP_NOP << OP_NOP << OP_NOP << OP_NOP);
    // Keep fee-reporting rates representable even at the CAmount boundary.
    // The small spendable output also permits parent/child and RBF fixtures.
    tx.vout.emplace_back(0, CScript{} << OP_RETURN << std::vector<unsigned char>(1'000));
    return MakeTransactionRef(tx);
}

void Add(CTxMemPool& pool, const CTransactionRef& tx, CAmount fee = 1'000)
{
    TryAddToMempool(pool, TestMemPoolEntryHelper{}.Fee(fee).SigOpsCost(0).FromTx(tx));
}

struct FeeSetup : TestingSetup {
    FeeSetup() : TestingSetup{ChainType::REGTEST, TestOpts{.extra_args={"-acceptnonstdtxn=1", "-checkmempool=1"}}} {}
    CTransactionRef Spend(unsigned index)
    {
        const COutPoint point{Txid::FromUint256(uint256{42}), index};
        m_node.chainman->ActiveChainstate().CoinsTip().AddCoin(point, Coin{CTxOut{10'000, CScript{} << OP_TRUE}, 0, false}, false);
        return Transaction(point);
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(modified_fee_tests, FeeSetup)

BOOST_AUTO_TEST_CASE(exposure_and_individual_boundaries)
{
    BOOST_CHECK_EQUAL(FeePool::FeeExposure(0, 0, 1), 0);
    BOOST_CHECK_EQUAL(FeePool::FeeExposure(1'000, 1'200, 1'000), 1'200);
    BOOST_CHECK_EQUAL(FeePool::FeeExposure(1'000, -200, 1'000), 2'200);
    BOOST_CHECK_EQUAL(FeePool::FeeExposure(0, -MAX, 1'000), MAX);
    BOOST_CHECK_EQUAL(FeePool::FeeExposure(1'000, 2'000 - MAX, 1'000), MAX);
    BOOST_CHECK_EQUAL(FeePool::FeeExposure(1'000, MAX, 1'000), MAX);
    BOOST_CHECK_THROW(FeePool::FeeExposure(1'000, 1'999 - MAX, 1'000), std::overflow_error);
    BOOST_CHECK_THROW(FeePool::FeeExposure(1'000, MIN, 1'000), std::overflow_error);
    BOOST_CHECK_THROW(FeePool::FeeExposure(-1, 0, 1'000), std::overflow_error);
    BOOST_CHECK_THROW(FeePool::FeeExposure(0, 0, 0), std::overflow_error);
    BOOST_CHECK_THROW(FeePool::FeeExposure(0, MAX, 999), std::overflow_error);
    BOOST_CHECK_THROW(FeePool::FeeExposure(0, -MAX, 999), std::overflow_error);

    const auto tx{Transaction({Txid::FromUint256(uint256{1}), 0})};
    auto entry{TestMemPoolEntryHelper{}.Fee(1'000).FromTx(tx)};
    entry.UpdateModifiedFee(MAX);
    BOOST_CHECK_EQUAL(entry.GetModifiedFee(), MAX);
    entry.UpdateModifiedFee(1);
    BOOST_CHECK_EQUAL(entry.GetModifiedFee(), MAX);
    entry.UpdateModifiedFee(MIN);
    BOOST_CHECK_EQUAL(entry.GetModifiedFee(), -1);
    entry.UpdateModifiedFee(MIN);
    BOOST_CHECK_EQUAL(entry.GetModifiedFee(), MIN);
}

BOOST_AUTO_TEST_CASE(pending_deltas_and_admission)
{
    bilingual_str error;
    FeePool pool{{}, error};
    LOCK2(cs_main, pool.cs);
    const auto tx{Transaction({Txid::FromUint256(uint256{1}), 0})};
    pool.PrioritiseTransaction(tx->GetHash(), MAX);
    pool.PrioritiseTransaction(tx->GetHash(), MAX);
    BOOST_CHECK_EQUAL(pool.mapDeltas.at(tx->GetHash()), MAX);
    pool.PrioritiseTransaction(tx->GetHash(), MIN);
    BOOST_CHECK_EQUAL(pool.mapDeltas.at(tx->GetHash()), -1);
    pool.PrioritiseTransaction(tx->GetHash(), MIN);
    BOOST_CHECK_EQUAL(pool.mapDeltas.at(tx->GetHash()), MIN);
    BOOST_CHECK_THROW(Add(pool, tx), std::overflow_error);
    BOOST_CHECK_EQUAL(pool.size(), 0);
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, 0);
    pool.ClearPrioritisation(tx->GetHash());
    Add(pool, tx);
    for (CAmount delta : {CAmount{200}, CAmount{-500}, CAmount{300}}) pool.PrioritiseTransaction(tx->GetHash(), delta);
    BOOST_CHECK_EQUAL(pool.GetEntry(tx->GetHash())->GetModifiedFee(), 1'000);
    BOOST_CHECK_EQUAL(pool.info(tx->GetHash()).nFeeDelta, 0);
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, 1'000);
    BOOST_CHECK_THROW(pool.PrioritiseTransaction(tx->GetHash(), MIN), std::overflow_error);
    BOOST_CHECK(!pool.mapDeltas.contains(tx->GetHash()));
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, 1'000);
}

BOOST_AUTO_TEST_CASE(aggregate_ancestors_and_diagrams)
{
    bilingual_str error;
    FeePool pool{{}, error};
    LOCK2(cs_main, pool.cs);
    const auto parent{Transaction({Txid::FromUint256(uint256{1}), 0})};
    const auto child{Transaction({parent->GetHash(), 0}, 8'000)};
    Add(pool, parent);
    Add(pool, child);
    pool.PrioritiseTransaction(parent->GetHash(), MAX / 2 - 1'000);
    pool.PrioritiseTransaction(child->GetHash(), MAX - MAX / 2 - 1'000);
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, MAX);
    BOOST_CHECK_EQUAL(std::get<2>(pool.CalculateAncestorData(*pool.GetEntry(child->GetHash()))), MAX);
    BOOST_CHECK_EQUAL(std::get<2>(pool.CalculateDescendantData(*pool.GetEntry(parent->GetHash()))), MAX);
    BOOST_CHECK_EQUAL(pool.GetFeerateDiagram().back().fee, MAX);
    const auto updated{pool.GetTransactionsUpdated()};
    BOOST_CHECK_THROW(pool.PrioritiseTransaction(child->GetHash(), 1), std::overflow_error);
    BOOST_CHECK_EQUAL(pool.GetTransactionsUpdated(), updated);
    BOOST_CHECK_EQUAL(pool.GetFeerateDiagram().back().fee, MAX);
    // A negative entry consumes absolute exposure, even if the net sum is small.
    pool.PrioritiseTransaction(child->GetHash(), -(MAX - MAX / 2 - 1'000));
    pool.PrioritiseTransaction(child->GetHash(), -(MAX - MAX / 2 - 1'000));
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, MAX);
    BOOST_CHECK_EQUAL(pool.info(child->GetHash()).nFeeDelta, -(MAX - MAX / 2 - 1'000));
    BOOST_CHECK_THROW(pool.PrioritiseTransaction(child->GetHash(), -1), std::overflow_error);
    BOOST_CHECK_EQUAL(pool.GetFeerateDiagram().back().fee, 1'999);
    pool.removeRecursive(*parent, MemPoolRemovalReason::REPLACED);
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, 0);
    BOOST_CHECK_EQUAL(pool.size(), 0);
}

BOOST_AUTO_TEST_CASE(staged_reservations_commit_and_rollback)
{
    bilingual_str error;
    FeePool pool{{}, error};
    LOCK2(cs_main, pool.cs);
    const auto a{Transaction({Txid::FromUint256(uint256{1}), 0})};
    const auto b{Transaction({Txid::FromUint256(uint256{2}), 0})};
    const auto c{Transaction({Txid::FromUint256(uint256{3}), 0})};
    Add(pool, a);
    pool.PrioritiseTransaction(b->GetHash(), MAX - 2'000);
    {
        auto changes{pool.GetChangeSet()};
        changes->StageRemoval(*pool.GetIter(a->GetHash()));
        changes->StageAddition(b, 1'000, 0, 0, 0, false, 0, {});
        BOOST_CHECK_EQUAL(pool.m_fee_exposure, MAX);
        BOOST_CHECK_THROW(changes->StageAddition(c, 1'000, 0, 0, 0, false, 0, {}), std::overflow_error);
        BOOST_CHECK_THROW(pool.PrioritiseTransaction(a->GetHash(), 1), std::overflow_error);
        BOOST_CHECK(!pool.mapDeltas.contains(a->GetHash()));
        BOOST_CHECK_EQUAL(pool.m_fee_exposure, MAX);
    }
    BOOST_CHECK_EQUAL(pool.size(), 1);
    BOOST_CHECK(pool.exists(a->GetHash()));
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, 1'000);
    pool.ClearPrioritisation(b->GetHash());
    {
        auto changes{pool.GetChangeSet()};
        changes->StageRemoval(*pool.GetIter(a->GetHash()));
        changes->StageAddition(b, 1'000, 0, 0, 0, false, 0, {});
        changes->Apply();
    }
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, 1'000);
    BOOST_CHECK(!pool.exists(a->GetHash()));
    BOOST_CHECK(pool.exists(b->GetHash()));
    pool.removeRecursive(*b, MemPoolRemovalReason::BLOCK);
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, 0);
}

BOOST_AUTO_TEST_CASE(package_rejection_releases_reservations)
{
    auto& pool{*m_node.mempool};
    LOCK2(cs_main, pool.cs);
    const auto a{Spend(0)}, b{Spend(1)};
    for (const auto& tx : {a, b}) pool.PrioritiseTransaction(tx->GetHash(), MAX / 2 + 1 - 1'000);
    const auto result{ProcessNewPackage(m_node.chainman->ActiveChainstate(), pool, {a, b}, true, std::nullopt)};
    BOOST_CHECK(!result.m_state.IsValid());
    BOOST_REQUIRE(result.m_tx_results.contains(b->GetWitnessHash()));
    BOOST_CHECK_EQUAL(result.m_tx_results.at(b->GetWitnessHash()).m_state.GetRejectReason(), "modified-fee-out-of-range");
    BOOST_CHECK_EQUAL(pool.size(), 0);
    for (const auto& tx : {a, b}) pool.ClearPrioritisation(tx->GetHash());
    const auto accepted{ProcessNewPackage(m_node.chainman->ActiveChainstate(), pool, {a, b}, true, std::nullopt)};
    BOOST_CHECK(accepted.m_state.IsValid());
    BOOST_CHECK_EQUAL(pool.size(), 0); // test_accept also rolls back successful staging.
    for (const auto& tx : {a, b}) {
        const auto accepted_tx{AcceptToMemoryPool(m_node.chainman->ActiveChainstate(), tx, GetTime(), false, false)};
        BOOST_CHECK(accepted_tx.m_state.IsValid());
    }
    pool.check(m_node.chainman->ActiveChainstate().CoinsTip(), 1);
    BOOST_CHECK_EQUAL(pool.size(), 2);
}

BOOST_AUTO_TEST_CASE(replacement_aggregates_and_rollback)
{
    auto& pool{*m_node.mempool};
    LOCK2(cs_main, pool.cs);
    const auto a{Spend(0)}, b{Spend(1)};
    Add(pool, a);
    Add(pool, b);
    for (const auto& tx : {a, b}) pool.PrioritiseTransaction(tx->GetHash(), MAX / 2 - 3'000);
    CMutableTransaction replacement{*a};
    replacement.vin.push_back(b->vin[0]);
    replacement.vout[0].nValue = 17'000;
    const auto candidate{MakeTransactionRef(replacement)};
    const auto rejected{AcceptToMemoryPool(m_node.chainman->ActiveChainstate(), candidate, GetTime(), false, false)};
    BOOST_CHECK_EQUAL(rejected.m_state.GetRejectReason(), "insufficient fee");
    BOOST_CHECK_EQUAL(pool.size(), 2);
    BOOST_CHECK(pool.exists(a->GetHash()));
    BOOST_CHECK(pool.exists(b->GetHash()));
    // Negative conflict fees exercise subtraction and the diagram comparison
    // at the same absolute aggregate exposure, without exceeding it.
    for (const auto& tx : {a, b}) pool.PrioritiseTransaction(tx->GetHash(), 6'001 - MAX);
    const auto accepted{AcceptToMemoryPool(m_node.chainman->ActiveChainstate(), candidate, GetTime(), false, false)};
    BOOST_CHECK(accepted.m_state.IsValid());
    BOOST_CHECK_EQUAL(accepted.m_replaced_transactions.size(), 2);
    BOOST_CHECK_EQUAL(pool.size(), 1);
    BOOST_CHECK(pool.exists(candidate->GetHash()));
    pool.check(m_node.chainman->ActiveChainstate().CoinsTip(), 1);
}

BOOST_AUTO_TEST_CASE(negative_reporting_and_bump_fee_bounds)
{
    bilingual_str error;
    FeePool pool{{}, error};
    LOCK2(cs_main, pool.cs);
    const auto tx{Transaction({Txid::FromUint256(uint256{1}), 0})};
    Add(pool, tx);
    pool.PrioritiseTransaction(tx->GetHash(), 1'000 - MAX);
    BOOST_CHECK_EQUAL(pool.info(tx->GetHash()).nFeeDelta, 1'000 - MAX);
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, MAX);
    BOOST_CHECK_THROW(pool.PrioritiseTransaction(tx->GetHash(), -1), std::overflow_error);
    const std::vector<COutPoint> outpoints{{tx->GetHash(), 0}};
    BOOST_CHECK(node::MiniMiner(pool, outpoints).CalculateBumpFees(CFeeRate{1'000}).empty());
    BOOST_CHECK(!node::MiniMiner(pool, outpoints).CalculateTotalBumpFees(CFeeRate{1'000}));
    // An individually payable estimate may be duplicated for multiple outputs.
    pool.PrioritiseTransaction(tx->GetHash(), MAX - 1'000 - MAX_MONEY / 2);
    const std::vector<COutPoint> duplicates{{tx->GetHash(), 0}, {tx->GetHash(), 1}};
    BOOST_CHECK(node::MiniMiner(pool, duplicates).CalculateBumpFees(CFeeRate{1'000}).empty());
    BOOST_CHECK(node::MiniMiner(pool, outpoints).CalculateTotalBumpFees(CFeeRate{1'000}).has_value());
}

BOOST_AUTO_TEST_CASE(eviction_and_fee_estimation_boundaries)
{
    bilingual_str error;
    FeePool pool{{}, error};
    LOCK2(cs_main, pool.cs);
    CMutableTransaction sized{*Transaction({Txid::FromUint256(uint256{1}), 0})};
    sized.vout[1].scriptPubKey = CScript{} << OP_RETURN << std::vector<unsigned char>(920);
    const auto tx{MakeTransactionRef(sized)};
    BOOST_REQUIRE_EQUAL(TestMemPoolEntryHelper{}.FromTx(tx).GetTxSize(), 1'000);
    Add(pool, tx);
    pool.PrioritiseTransaction(tx->GetHash(), MAX - 1'000);
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, MAX);
    pool.TrimToSize(0);
    BOOST_CHECK_EQUAL(pool.size(), 0);
    BOOST_CHECK_EQUAL(pool.m_fee_exposure, 0);
    const auto rate{pool.GetMinFee(1)};
    BOOST_CHECK_GT(rate.GetFeePerK(), 0);
    BOOST_CHECK_GT(rate.GetFee(std::numeric_limits<int32_t>::max()), 0);
    BOOST_CHECK_LE(rate.GetFee(std::numeric_limits<int32_t>::max()), MAX);
}

BOOST_AUTO_TEST_SUITE_END()
