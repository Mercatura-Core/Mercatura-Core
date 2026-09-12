// Copyright (c) 2012-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <consensus/amount.h>
#include <interfaces/chain.h>
#include <node/context.h>
#include <outputtype.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <random.h>
#include <sync.h>
#include <util/result.h>
#include <wallet/coinselection.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/transaction.h>
#include <wallet/wallet.h>

#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

using node::NodeContext;
using wallet::AttemptSelection;
using wallet::CHANGE_LOWER;
using wallet::COutput;
using wallet::CWallet;
using wallet::CWalletTx;
using wallet::CoinEligibilityFilter;
using wallet::CoinSelectionParams;
using wallet::CreateMockableWalletDatabase;
using wallet::OutputGroup;
using wallet::SelectCoinsBnB;
using wallet::TxStateInactive;

static void addCoin(const CAmount& nValue, const CWallet& wallet, std::vector<std::unique_ptr<CWalletTx>>& wtxs)
{
    static int nextLockTime = 0;
    CMutableTransaction tx;
    tx.nLockTime = nextLockTime++; // so all transactions get different hashes
    tx.vout.resize(1);
    tx.vout[0].nValue = nValue;
    wtxs.push_back(std::make_unique<CWalletTx>(MakeTransactionRef(std::move(tx)), TxStateInactive{}));
}

// Simple benchmark for wallet coin selection. Note that it maybe be necessary
// to build up more complicated scenarios in order to get meaningful
// measurements of performance. From laanwj, "Wallet coin selection is probably
// the hardest, as you need a wider selection of scenarios, just testing the
// same one over and over isn't too useful. Generating random isn't useful
// either for measurements."
// (https://github.com/bitcoin/bitcoin/issues/7883#issuecomment-224807484)
static void CoinSelection(benchmark::Bench& bench)
{
    // Preserve the inherited synthetic P2WPKH-sized fixture used by this
    // benchmark. Mercatura's real native PQ inputs are substantially larger,
    // but changing this synthetic input size changes the benchmark scenario
    // rather than measuring the same coin-selection workload.
    constexpr int INHERITED_SMALL_INPUT_SIZE{68};
    constexpr int INHERITED_SMALL_INPUT_WEIGHT{272};

    // Bitcoin's original benchmark assumes COIN is much larger than the fixed
    // fee rates below. Mercatura uses COIN = 100, so use a larger test-only
    // unit to preserve the benchmark's original amount and fee relationships.
    const CAmount test_unit{1'000'000 * COIN};

    NodeContext node;
    auto chain = interfaces::MakeChain(node);
    CWallet wallet(chain.get(), "", CreateMockableWalletDatabase());
    std::vector<std::unique_ptr<CWalletTx>> wtxs;
    LOCK(wallet.cs_wallet);

    // Add coins.
    for (int i = 0; i < 1000; ++i) {
        addCoin(1000 * test_unit, wallet, wtxs);
    }
    addCoin(3 * test_unit, wallet, wtxs);

    // Create coins
    wallet::CoinsResult available_coins;
    for (const auto& wtx : wtxs) {
        const auto txout = wtx->tx->vout.at(0);
        available_coins.coins[OutputType::BECH32].emplace_back(COutPoint(wtx->GetHash(), 0), txout, /*depth=*/6 * 24, INHERITED_SMALL_INPUT_SIZE, /*solvable=*/true, /*safe=*/true, wtx->GetTxTime(), /*from_me=*/true, /*fees=*/0, INHERITED_SMALL_INPUT_WEIGHT);
    }

    const CoinEligibilityFilter filter_standard(1, 6, 0);
    FastRandomContext rand{};
    const CoinSelectionParams coin_selection_params{
        rand,
        /*change_output_size=*/ 34,
        /*change_spend_size=*/ 148,
        /*min_change_target=*/ CHANGE_LOWER,
        /*effective_feerate=*/ CFeeRate(20'000),
        /*long_term_feerate=*/ CFeeRate(10'000),
        /*discard_feerate=*/ CFeeRate(3000),
        /*tx_noinputs_size=*/ 0,
        /*avoid_partial=*/ false,
    };
    auto group = wallet::GroupOutputs(wallet, available_coins, coin_selection_params, {{filter_standard}})[filter_standard];
    bench.run([&] {
        auto result = AttemptSelection(wallet.chain(), 100299 * test_unit / 100, group, coin_selection_params, /*allow_mixed_output_types=*/true);
        assert(result);
        assert(result->GetSelectedValue() == 1003 * test_unit);
        assert(result->GetInputSet().size() == 2);
    });
}

// Copied from src/wallet/test/coinselector_tests.cpp
static void add_coin(const CAmount& nValue, int nInput, std::vector<OutputGroup>& set)
{
    CMutableTransaction tx;
    tx.vout.resize(nInput + 1);
    tx.vout[nInput].nValue = nValue;
    COutput output(COutPoint(tx.GetHash(), nInput), tx.vout.at(nInput), /*depth=*/0, /*input_bytes=*/-1, /*solvable=*/true, /*safe=*/true, /*time=*/0, /*from_me=*/true, /*fees=*/0);
    set.emplace_back();
    set.back().Insert(std::make_shared<COutput>(output), /*ancestors=*/ 0, /*cluster_count=*/ 0);
}
// Copied from src/wallet/test/coinselector_tests.cpp
static CAmount make_hard_case(int utxos, std::vector<OutputGroup>& utxo_pool)
{
    utxo_pool.clear();
    CAmount target = 0;
    for (int i = 0; i < utxos; ++i) {
        target += CAmount{1} << (utxos+i);
        add_coin(CAmount{1} << (utxos+i), 2*i, utxo_pool);
        add_coin((CAmount{1} << (utxos+i)) + (CAmount{1} << (utxos-1-i)), 2*i + 1, utxo_pool);
    }
    return target;
}

static void BnBExhaustion(benchmark::Bench& bench)
{
    // Setup
    std::vector<OutputGroup> utxo_pool;

    bench.run([&] {
        // Benchmark
        CAmount target = make_hard_case(17, utxo_pool);
        (void)SelectCoinsBnB(utxo_pool, target, /*cost_of_change=*/0, MAX_STANDARD_TX_WEIGHT); // Should exhaust

        // Cleanup
        utxo_pool.clear();
    });
}

BENCHMARK(CoinSelection);
BENCHMARK(BnBExhaustion);
