// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <key.h>
#include <policy/fees/block_policy_estimator.h>
#include <script/solver.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/test/wallet_test_fixture.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(spend_tests, WalletTestingSetup)

BOOST_AUTO_TEST_CASE(max_signed_input_size_uses_external_outpoint)
{
    const CKey key{GenerateRandomKey()};
    FillableSigningProvider provider;
    BOOST_REQUIRE(provider.AddKey(key));

    const CTxOut txout{COIN, GetScriptForDestination(PKHash{key.GetPubKey()})};
    const COutPoint outpoint{Txid{}, 0};
    CCoinControl coin_control;
    coin_control.Select(outpoint).SetTxOut(txout);

    const int low_r{CalculateMaximumSignedInputSize(txout, COutPoint{}, &provider, /*can_grind_r=*/true, &coin_control)};
    const int high_r{CalculateMaximumSignedInputSize(txout, outpoint, &provider, /*can_grind_r=*/true, &coin_control)};
    BOOST_CHECK_EQUAL(high_r, low_r + 1);
}

BOOST_FIXTURE_TEST_CASE(SubtractFee, TestChain100Setup)
{
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);
    const CAmount spendable_coinbase_value{m_coinbase_txns.at(0)->vout.at(0).nValue};

    // Use a higher test-only discard rate so Mercatura's coarse monetary
    // granularity leaves room between the transaction fee and minimum viable
    // change. This preserves the original subtract-fee test semantics.
    wallet->m_discard_rate = CFeeRate{20};

    // Check that a subtract-from-recipient transaction slightly less than the
    // coinbase input amount does not create a change output (because it would
    // be uneconomical to add and spend the output), and make sure it pays the
    // leftover input amount which would have been change to the recipient
    // instead of the miner.
    auto check_tx = [&wallet, spendable_coinbase_value](CAmount leftover_input_amount) {
        CRecipient recipient{
            PubKeyDestination({}),
            spendable_coinbase_value - leftover_input_amount,
            /*subtract_fee=*/true};
        CCoinControl coin_control;
        coin_control.m_feerate.emplace(10);
        coin_control.fOverrideFeeRate = true;
        // We need to use a change type with high cost of change so that the leftover amount will be dropped to fee instead of added as a change output
        coin_control.m_change_type = OutputType::LEGACY;
        auto res = CreateTransaction(*wallet, {recipient}, /*change_pos=*/std::nullopt, coin_control);
        BOOST_CHECK(res);
        const auto& txr = *res;
        BOOST_CHECK_EQUAL(txr.tx->vout.size(), 1);
        BOOST_CHECK_EQUAL(txr.tx->vout[0].nValue, recipient.nAmount + leftover_input_amount - txr.fee);
        BOOST_CHECK_GT(txr.fee, 0);
        return txr.fee;
    };

    // Send full input amount to recipient, check that only nonzero fee is
    // subtracted (to_reduce == fee).
    const CAmount fee{check_tx(0)};

    // Send slightly less than full input amount to recipient, check leftover
    // input amount is paid to recipient not the miner (to_reduce == fee - 1)
    BOOST_CHECK_EQUAL(fee, check_tx(1));

    // Send full input minus fee amount to recipient, check leftover input
    // amount is paid to recipient not the miner (to_reduce == 0)
    BOOST_CHECK_EQUAL(fee, check_tx(fee));

    // Send full input minus more than the fee amount to recipient, check
    // leftover input amount is paid to recipient not the miner (to_reduce ==
    // -1). This overpays the recipient instead of overpaying the miner more
    // than double the necessary fee.
    BOOST_CHECK_EQUAL(fee, check_tx(fee + 1));
}

BOOST_FIXTURE_TEST_CASE(wallet_duplicated_preset_inputs_test, TestChain100Setup)
{
    // Verify that the wallet's Coin Selection process does not include pre-selected inputs twice in a transaction.

    // Add four spendable Mercatura UTXOs to the wallet.
    for (int i = 0; i < 4; i++) CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);

    LOCK(wallet->cs_wallet);
    auto available_coins = AvailableCoins(*wallet);
    std::vector<COutput> coins = available_coins.All();
    // Preselect the first three UTXOs.
    std::set<COutPoint> preset_inputs = {coins[0].outpoint, coins[1].outpoint, coins[2].outpoint};

    const CAmount wallet_total{available_coins.GetTotalAmount()};

    CAmount preset_total{0};
    for (const auto& outpoint : preset_inputs) {
        const auto wallet_tx = wallet->mapWallet.find(outpoint.hash);
        BOOST_REQUIRE(wallet_tx != wallet->mapWallet.end());
        preset_total += wallet_tx->second.tx->vout.at(outpoint.n).nValue;
    }

    // The target must exceed the real wallet balance, but remain below the
    // incorrectly inflated balance that would result from counting preset
    // inputs twice. Half of the preset-input value gives ample fee margin.
    const CAmount target_amount{wallet_total + preset_total / 2};
    BOOST_REQUIRE(target_amount > wallet_total);
    BOOST_REQUIRE(target_amount < wallet_total + preset_total);

    std::vector<CRecipient> recipients{{*Assert(wallet->GetNewDestination(OutputType::BECH32, "dummy")),
                                           /*nAmount=*/target_amount, /*fSubtractFeeFromAmount=*/true}};
    CCoinControl coin_control;
    coin_control.m_allow_other_inputs = true;
    for (const auto& outpoint : preset_inputs) {
        coin_control.Select(outpoint);
    }

    // Attempt to send more than the wallet actually owns. The wallet must
    // exclude preset inputs from the ordinary coin-selection pool and fail
    // with insufficient funds. If preset inputs are counted twice, the
    // artificially inflated balance would be large enough to fund this target.

    // First case, use 'subtract_fee_from_outputs=true'
    BOOST_CHECK(!CreateTransaction(*wallet, recipients, /*change_pos=*/std::nullopt, coin_control));

    // Second case, don't use 'subtract_fee_from_outputs'.
    recipients[0].fSubtractFeeFromAmount = false;
    BOOST_CHECK(!CreateTransaction(*wallet, recipients, /*change_pos=*/std::nullopt, coin_control));
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
