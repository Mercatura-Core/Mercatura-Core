// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <index/txospenderindex.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <test/util/script.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(txospenderindex_tests)

BOOST_FIXTURE_TEST_CASE(txospenderindex_initial_sync, TestChain100Setup)
{
    // Setup phase:
    // Mercatura permanently disables inherited ECDSA/Schnorr ownership.
    // Use the established signature-free P2WSH OP_TRUE test fixture so this
    // test continues exercising tx-spender index behavior rather than
    // inherited Bitcoin signature authorization.
    const CScript& coinbase_script = m_coinbase_txns[0]->vout[0].scriptPubKey;
    for (int i = 0; i < 10; i++) CreateAndProcessBlock({}, coinbase_script);

    // Create and spend 10 deterministic synthetic non-coinbase outputs.
    std::vector<COutPoint> spent(10);
    std::vector<CMutableTransaction> spender(spent.size());

    {
        LOCK(Assert(m_node.chainman)->GetMutex());

        auto& coins{
            Assert(m_node.chainman)
                ->ActiveChainstate()
                .CoinsTip()
        };

        const int funding_height{
            Assert(m_node.chainman)->ActiveHeight()
        };

        for (size_t i = 0; i < spent.size(); ++i) {
            spent[i] = COutPoint{
                Txid::FromUint256(uint256(i + 1)),
                0};

            coins.AddCoin(
                spent[i],
                Coin{
                    CTxOut{50 * COIN, P2WSH_OP_TRUE},
                    funding_height,
                    /*coinbase=*/false},
                /*possible_overwrite=*/false);
        }
    }

    for (size_t i = 0; i < spent.size(); ++i) {
        spender[i].version = 1;
        spender[i].vin.emplace_back(spent[i]);
        spender[i].vin[0].scriptWitness.stack.push_back(
            WITNESS_STACK_ELEM_OP_TRUE);

        // Leave the normal Mercatura minimum fee of one base unit.
        spender[i].vout.emplace_back(
            50 * COIN - CENT,
            P2WSH_OP_TRUE);
    }

    // Generate and ensure block has been fully processed
    const uint256 tip_hash = CreateAndProcessBlock(spender, coinbase_script).GetHash();
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    BOOST_CHECK_EQUAL(WITH_LOCK(::cs_main, return m_node.chainman->ActiveTip()->GetBlockHash()), tip_hash);

    // Now we concluded the setup phase, run index
    TxoSpenderIndex txospenderindex(interfaces::MakeChain(m_node), 1 << 20, true);
    BOOST_REQUIRE(txospenderindex.Init());
    BOOST_CHECK(!txospenderindex.BlockUntilSyncedToCurrentChain()); // false when not synced
    BOOST_CHECK_NE(txospenderindex.GetSummary().best_block_hash, tip_hash);

    // Transaction should not be found in the index before it is synced.
    for (const auto& outpoint : spent) {
        BOOST_CHECK(!txospenderindex.FindSpender(outpoint).value());
    }

    txospenderindex.Sync();
    BOOST_CHECK_EQUAL(txospenderindex.GetSummary().best_block_hash, tip_hash);

    for (size_t i = 0; i < spent.size(); i++) {
        const auto tx_spender{txospenderindex.FindSpender(spent[i])};
        BOOST_REQUIRE(tx_spender.has_value());
        BOOST_REQUIRE(tx_spender->has_value());
        BOOST_CHECK_EQUAL((*tx_spender)->tx->GetHash(), spender[i].GetHash());
        BOOST_CHECK_EQUAL((*tx_spender)->block_hash, tip_hash);
    }

    // Shutdown sequence (c.f. Shutdown() in init.cpp)
    txospenderindex.Stop();
}

BOOST_AUTO_TEST_SUITE_END()
