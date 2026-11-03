// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "native_fixture.h"
#include <iostream>
#include <chrono>
#include <random>

using namespace mercaweave;
using namespace mercaweave::production_test;

BOOST_FIXTURE_TEST_SUITE(pqgroup_chainstate,NativeSetup)
BOOST_AUTO_TEST_CASE(real_connect_disconnect_undo_rollback_and_reload)
{
    ProductionPathTestAccess::Attach(State(),Execution());
    uint32_t serial{1};
    for (const auto& [n,groups] : {std::pair{1,1}, {10,1}, {53,1}, {100,1}, {100,10}, {100,100},
                                 {1000,1}, {1024,1}, {256,256}, {4096,4}}) {
        auto fixture = Fixture(n,groups,serial++); Fund(fixture);
        const auto before = DiskState();
        const auto previous = WITH_LOCK(cs_main,return State().m_chain.Tip()->GetBlockHash());
        const auto block = Block({fixture.tx});
        const auto started = std::chrono::steady_clock::now(); Connect(block);
        const auto connect_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        {
            LOCK(cs_main);
            TxValidationState duplicate_spend; Statistics work;
            BOOST_CHECK(!CheckInputScriptsWithPQGroups(CTransaction(fixture.tx),duplicate_spend,
                State().CoinsTip(),STANDARD_SCRIPT_VERIFY_FLAGS,m_node.chainman->m_validation_cache,
                nullptr,Execution(),m_node.chainman->GetConsensus().hashGenesisBlock,&work));
            BOOST_CHECK_EQUAL(work.verification_calls,0);
        }
        CBlockIndex* index;
        {
            LOCK(cs_main);
            index = m_node.chainman->m_blockman.LookupBlockIndex(block.GetHash());
            BOOST_REQUIRE(index->nStatus & BLOCK_HAVE_UNDO);
            for (const auto& input : fixture.tx.vin) BOOST_CHECK(!State().CoinsTip().HaveCoin(input.prevout));
            const auto& output = State().CoinsTip().AccessCoin(COutPoint(CTransaction(fixture.tx).GetHash(),0));
            BOOST_CHECK(output.out == fixture.tx.vout[0]);
            BOOST_CHECK_EQUAL(output.nHeight,index->nHeight);
        }
        Invalidate(index);
        BOOST_CHECK(WITH_LOCK(cs_main,return State().m_chain.Tip()->GetBlockHash()) == previous);
        BOOST_CHECK(DiskState() == before);
        {
            LOCK(cs_main); State().ResetBlockFailureFlags(index); m_node.chainman->RecalculateBestHeader();
        }
        BlockValidationState state;
        BOOST_REQUIRE(State().ActivateBestChain(state));
        BOOST_CHECK(WITH_LOCK(cs_main,return State().m_chain.Tip()->GetBlockHash()) == block.GetHash());
        std::cout << "WO07C_JSON {\"scenario\":\"actual-connect-disconnect\",\"inputs\":" << n
                  << ",\"groups\":" << groups << ",\"connect_us\":" << connect_us
                  << ",\"bytes\":" << Measure(fixture.tx).total << ",\"weight\":" << Measure(fixture.tx).weight
                  << ",\"persistent_undo\":true,\"exact_utxo_restore\":true}\n";
    }
    // A valid prefix must not survive a later invalid group in the same block.
    auto first = Fixture(10,1,serial++); auto last = Fixture(10,2,serial++);
    Fund(first); Fund(last); last.tx.vin[1].scriptWitness.stack[0][100] ^= 1;
    const auto before_bad = DiskState();
    const auto old_tip = WITH_LOCK(cs_main,return State().m_chain.Tip()->GetBlockHash());
    const auto bad = Block({first.tx,last.tx}); const auto bad_index = Store(bad);
    BlockValidationState invalid_state;
    State().ActivateBestChain(invalid_state,std::make_shared<const CBlock>(bad));
    BOOST_CHECK(WITH_LOCK(cs_main,return State().m_chain.Tip()->GetBlockHash()) == old_tip);
    BOOST_CHECK(DiskState() == before_bad);
    BOOST_CHECK(WITH_LOCK(cs_main,return bool(bad_index->nStatus & BLOCK_FAILED_VALID)));

    // Independent competing spend B, then restore history A through real undo.
    auto history = Fixture(10,1,serial++); Fund(history);
    const auto block_a = Block({history.tx}); Connect(block_a);
    auto index_a = WITH_LOCK(cs_main,return m_node.chainman->m_blockman.LookupBlockIndex(block_a.GetHash()));
    const auto state_a = DiskState(); Invalidate(index_a);
    auto competing = history.tx; competing.vout[0].nValue -= 1;
    Sign(competing,history.spent,history.genesis,history.KeyRefs(),test::ZeroRandomnessForTestsOnly);
    const auto block_b = Block({competing}); Connect(block_b);
    auto index_b = WITH_LOCK(cs_main,return m_node.chainman->m_blockman.LookupBlockIndex(block_b.GetHash()));
    Invalidate(index_b);
    { LOCK(cs_main); State().ResetBlockFailureFlags(index_a); m_node.chainman->RecalculateBestHeader(); }
    BlockValidationState restore_state; BOOST_REQUIRE(State().ActivateBestChain(restore_state));
    BOOST_CHECK(DiskState() == state_a);
    const auto persisted = DiskState();
    {
        LOCK(cs_main);
        State().ResetCoinsViews();
        State().InitCoinsDB(1 << 20,false,false);
        State().InitCoinsCache(1 << 20);
        BOOST_REQUIRE(State().LoadChainTip());
    }
    BOOST_CHECK(DiskState() == persisted);
    std::cout << "WO07C_JSON {\"scenario\":\"actual-chainstate-recovery\",\"late_invalid_atomic\":true,"
                 "\"competing_spends\":true,\"coins_database_reopened\":true,\"real_network_v3\":false}\n";
}
BOOST_AUTO_TEST_SUITE_END()
