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
    auto wallet = CreateSyncedWallet(
        *m_node.chain,
        WITH_LOCK(
            Assert(m_node.chainman)->GetMutex(),
            return m_node.chainman->ActiveChain()),
        coinbaseKey);

    {
        LOCK(wallet->cs_wallet);

        if (!wallet->HasMercaturaPQState()) {
            BOOST_REQUIRE(
                wallet->InitializeMercaturaPQWallet());
        }
    }

    auto source_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-subtract-fee-source")
    };

    auto recipient_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-subtract-fee-recipient")
    };

    BOOST_REQUIRE(source_result);
    BOOST_REQUIRE(recipient_result);

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &*source_result) != nullptr);

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &*recipient_result) != nullptr);

    const CTxDestination source_destination{
        *source_result
    };

    const CTxDestination recipient_destination{
        *recipient_result
    };

    const CAmount spendable_coinbase_value{
        m_coinbase_txns.at(0)->vout.at(0).nValue
    };

    // Replace the inherited classical coinbase spend fixture with a
    // confirmed native Mercatura PQ output. The test below continues
    // to exercise the original subtract-fee/change economics.
    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        spendable_coinbase_value,
        GetScriptForDestination(
            source_destination));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    uint256 tip_hash;
    int tip_height;

    {
        LOCK(
            Assert(m_node.chainman)
                ->GetMutex());

        const CBlockIndex* tip{
            m_node.chainman
                ->ActiveChain()
                .Tip()
        };

        BOOST_REQUIRE(tip);

        tip_hash = tip->GetBlockHash();
        tip_height = tip->nHeight;
    }

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->AddToWallet(
                funding_tx,
                TxStateConfirmed{
                    tip_hash,
                    tip_height,
                    /*position=*/1},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);
    }

    const COutPoint funding_outpoint{
        funding_tx->GetHash(),
        0
    };

    // Use a higher test-only discard rate so Mercatura's coarse monetary
    // granularity leaves room between the transaction fee and minimum viable
    // change. This preserves the original subtract-fee test semantics.
    wallet->m_discard_rate = CFeeRate{20};

    // Check that a subtract-from-recipient transaction slightly less than the
    // input amount does not create a change output, and that the leftover
    // amount is paid to the recipient instead of unnecessarily to the miner.
    auto check_tx =
        [&wallet,
         spendable_coinbase_value,
         funding_outpoint,
         recipient_destination]
        (CAmount leftover_input_amount) {
            CRecipient recipient{
                recipient_destination,
                spendable_coinbase_value -
                    leftover_input_amount,
                /*subtract_fee=*/true};

            CCoinControl coin_control;

            // Force this exact native PQ UTXO so inherited classical wallet
            // fixtures cannot participate in coin selection.
            coin_control.Select(
                funding_outpoint);

            coin_control.m_allow_other_inputs =
                false;

            coin_control.m_feerate.emplace(10);
            coin_control.fOverrideFeeRate = true;

            // Mercatura change is native PQ/Bech32m rather than legacy.
            coin_control.m_change_type =
                OutputType::BECH32M;

            auto res{
                CreateTransaction(
                    *wallet,
                    {recipient},
                    /*change_pos=*/std::nullopt,
                    coin_control,
                    /*sign=*/true)
            };

            const std::string result_error{
                res
                    ? std::string{}
                    : util::ErrorString(res).original
            };

            BOOST_REQUIRE_MESSAGE(
                static_cast<bool>(res),
                result_error);

            const auto& txr = *res;

            BOOST_CHECK_EQUAL(
                txr.tx->vout.size(),
                1);

            BOOST_CHECK_EQUAL(
                txr.tx->vout[0].nValue,
                recipient.nAmount +
                    leftover_input_amount -
                    txr.fee);

            BOOST_CHECK_GT(
                txr.fee,
                0);

            return txr.fee;
        };

    // Send full input amount to recipient, check that only nonzero fee is
    // subtracted (to_reduce == fee).
    const CAmount fee{
        check_tx(0)
    };

    // Send slightly less than full input amount to recipient, check leftover
    // input amount is paid to recipient not the miner (to_reduce == fee - 1).
    BOOST_CHECK_EQUAL(
        fee,
        check_tx(1));

    // Send full input minus fee amount to recipient, check leftover input
    // amount is paid to recipient not the miner (to_reduce == 0).
    BOOST_CHECK_EQUAL(
        fee,
        check_tx(fee));

    // Send full input minus more than the fee amount to recipient, check
    // leftover input amount is paid to recipient not the miner
    // (to_reduce == -1).
    BOOST_CHECK_EQUAL(
        fee,
        check_tx(fee + 1));
}

BOOST_FIXTURE_TEST_CASE(wallet_duplicated_preset_inputs_test, TestChain100Setup)
{
    // Use a classical key unrelated to the test chain so inherited
    // Bitcoin coinbase outputs cannot participate in this fixture.
    const CKey unrelated_key{
        GenerateRandomKey()
    };

    auto wallet = CreateSyncedWallet(
        *m_node.chain,
        WITH_LOCK(
            Assert(m_node.chainman)->GetMutex(),
            return m_node.chainman->ActiveChain()),
        unrelated_key);

    {
        LOCK(wallet->cs_wallet);

        if (!wallet->HasMercaturaPQState()) {
            BOOST_REQUIRE(
                wallet->InitializeMercaturaPQWallet());
        }
    }

    // Create four genuine wallet-owned Mercatura PQ outputs.
    CMutableTransaction funding;
    funding.version = 2;

    for (int i = 0; i < 4; ++i) {
        auto destination{
            wallet->GetNewDestination(
                OutputType::BECH32,
                "pq-duplicate-input")
        };

        BOOST_REQUIRE(destination);

        BOOST_REQUIRE(
            std::get_if<WitnessV2MercaturaPQ>(
                &*destination) != nullptr);

        funding.vout.emplace_back(
            10 * COIN,
            GetScriptForDestination(
                *destination));
    }

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    uint256 tip_hash;
    int tip_height;

    {
        LOCK(
            Assert(m_node.chainman)
                ->GetMutex());

        const CBlockIndex* tip{
            m_node.chainman
                ->ActiveChain()
                .Tip()
        };

        BOOST_REQUIRE(tip);

        tip_hash = tip->GetBlockHash();
        tip_height = tip->nHeight;
    }

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->AddToWallet(
                funding_tx,
                TxStateConfirmed{
                    tip_hash,
                    tip_height,
                    /*position=*/1},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);
    }

    LOCK(wallet->cs_wallet);

    auto available_coins{
        AvailableCoins(*wallet)
    };

    std::vector<COutput> coins{
        available_coins.All()
    };

    // The unrelated classical descriptor owns no chain outputs, so these
    // four native PQ outputs are the complete spendable test balance.
    BOOST_REQUIRE_EQUAL(
        coins.size(),
        4U);

    // Preselect the first three UTXOs.
    std::set<COutPoint> preset_inputs{
        coins[0].outpoint,
        coins[1].outpoint,
        coins[2].outpoint
    };

    const CAmount wallet_total{
        available_coins.GetTotalAmount()
    };

    CAmount preset_total{0};

    for (const auto& outpoint :
         preset_inputs) {
        const auto wallet_tx{
            wallet->mapWallet.find(
                outpoint.hash)
        };

        BOOST_REQUIRE(
            wallet_tx !=
            wallet->mapWallet.end());

        preset_total +=
            wallet_tx->second.tx
                ->vout.at(outpoint.n)
                .nValue;
    }

    // The target exceeds the real wallet balance, but remains below the
    // incorrectly inflated balance that would result if the three preset
    // inputs were counted again by ordinary coin selection.
    const CAmount target_amount{
        wallet_total +
        preset_total / 2
    };

    BOOST_REQUIRE(
        target_amount >
        wallet_total);

    BOOST_REQUIRE(
        target_amount <
        wallet_total +
        preset_total);

    auto recipient{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-duplicate-recipient")
    };

    BOOST_REQUIRE(recipient);

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &*recipient) != nullptr);

    std::vector<CRecipient> recipients{
        {
            *recipient,
            /*nAmount=*/target_amount,
            /*fSubtractFeeFromAmount=*/true
        }
    };

    CCoinControl coin_control;
    coin_control.m_allow_other_inputs = true;

    for (const auto& outpoint :
         preset_inputs) {
        coin_control.Select(
            outpoint);
    }

    // Attempt to send more than the wallet actually owns. The wallet must
    // exclude preset inputs from the ordinary coin-selection pool and fail
    // with insufficient funds. If preset inputs are counted twice, the
    // artificially inflated balance is large enough to fund the target.

    // First case: subtract the fee from the output.
    BOOST_CHECK(
        !CreateTransaction(
            *wallet,
            recipients,
            /*change_pos=*/std::nullopt,
            coin_control));

    // Second case: do not subtract the fee from the output.
    recipients[0].fSubtractFeeFromAmount =
        false;

    BOOST_CHECK(
        !CreateTransaction(
            *wallet,
            recipients,
            /*change_pos=*/std::nullopt,
            coin_control));
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
