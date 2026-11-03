// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef MERCATURA_TEST_PRODUCTION_PATH_NATIVE_FIXTURE_H
#define MERCATURA_TEST_PRODUCTION_PATH_NATIVE_FIXTURE_H
#include "wallet_access.h"
#include "../fixtures.h"
#include "../native_v2.h"
#include <chainparams.h>
#include <consensus/validation.h>
#include <node/miner.h>
#include <test/util/setup_common.h>
#include <util/time.h>
#include <validationinterface.h>
#include <wallet/receive.h>
#include <wallet/sqlite.h>
#include <wallet/test/util.h>
#include <wallet/walletdb.h>
#include <boost/test/unit_test.hpp>

namespace mercaweave::production_test {
struct NativeSetup : TestingSetup {
    PoWHashContext pow;
    static TestOpts Options()
    {
        TestOpts options;
        options.coins_db_in_memory = false; options.block_tree_db_in_memory = false;
        options.setup_net = false; options.min_validation_cache = true;
        return options;
    }
    NativeSetup() : TestingSetup(ChainType::REGTEST,Options())
    {
        SetMockTime(m_node.chainman->GetParams().GenesisBlock().Time());
        // Height zero has a reserved legacy undo encoding. Establish a genuine
        // ordinary block before installing fixture coins with full metadata.
        Connect(Block({}));
    }
    Chainstate& State() { LOCK(cs_main); return m_node.chainman->ActiveChainstate(); }
    B32 Genesis() const
    {
        B32 bytes;
        const auto& hash = m_node.chainman->GetConsensus().hashGenesisBlock;
        std::copy(hash.begin(),hash.end(),bytes.begin()); return bytes;
    }
    std::shared_ptr<const IsolatedExecution> Execution() const { return ProductionPathTestAccess::Execution(Genesis()); }
    CBlock Block(const std::vector<CMutableTransaction>& transactions)
    {
        node::BlockAssembler::Options options;
        options.coinbase_output_script = CScript() << OP_TRUE;
        options.include_dummy_extranonce = true;
        auto block = node::BlockAssembler(State(),nullptr,options).CreateNewBlock()->block;
        for (const auto& tx : transactions) block.vtx.push_back(MakeTransactionRef(tx));
        node::RegenerateCommitments(block,*m_node.chainman);
        const int height = WITH_LOCK(cs_main,return State().m_chain.Height()+1);
        while (!CheckProofOfWork(block,m_node.chainman->GetConsensus(),pow,height)) ++block.nNonce;
        block.fChecked = false; block.m_checked_merkle_root = false;
        block.m_checked_witness_commitment = false; block.m_checked_pow_version = 0;
        SetMockTime(GetTime()+1);
        return block;
    }
    CBlockIndex* Store(const CBlock& block)
    {
        CBlockIndex* index{nullptr}; BlockValidationState state;
        LOCK(cs_main);
        BOOST_REQUIRE_MESSAGE(m_node.chainman->AcceptBlock(std::make_shared<const CBlock>(block),state,
            &index,true,nullptr,nullptr,true),state.ToString());
        BOOST_REQUIRE(index); return index;
    }
    void Connect(const CBlock& block)
    {
        Store(block); BlockValidationState state;
        BOOST_REQUIRE_MESSAGE(State().ActivateBestChain(state,std::make_shared<const CBlock>(block)),state.ToString());
        BOOST_REQUIRE(WITH_LOCK(cs_main,return State().m_chain.Tip()->GetBlockHash()) == block.GetHash());
        m_node.chain->waitForNotificationsIfTipChanged({});
    }
    void Invalidate(CBlockIndex* index)
    {
        BlockValidationState state;
        BOOST_REQUIRE_MESSAGE(State().InvalidateBlock(state,index),state.ToString());
        BOOST_REQUIRE(State().ActivateBestChain(state));
        m_node.chain->waitForNotificationsIfTipChanged({});
    }
    std::map<COutPoint,Bytes> DiskState()
    {
        State().ForceFlushStateToDisk();
        LOCK(cs_main);
        std::map<COutPoint,Bytes> result;
        auto cursor = State().CoinsDB().Cursor();
        for (; cursor->Valid(); cursor->Next()) {
            COutPoint point; Coin coin;
            BOOST_REQUIRE(cursor->GetKey(point) && cursor->GetValue(coin));
            result.emplace(point,SerializeBytes(coin));
        }
        return result;
    }
    test::Fixture Fixture(size_t n, size_t groups, uint32_t serial)
    {
        auto fixture = test::Make(n,groups);
        fixture.genesis = Genesis();
        for (uint32_t i = 0; i < n; ++i) {
            auto& input = fixture.tx.vin[i];
            input.prevout.n += 10000 * serial;
            B32 nonce; std::copy(input.scriptWitness.stack.back().begin(),input.scriptWitness.stack.back().end(),nonce.begin());
            if (i >= groups) std::copy(input.scriptWitness.stack[0].begin(),input.scriptWitness.stack[0].end(),nonce.begin());
            fixture.spent[i].scriptPubKey = Output(Genesis(),fixture.keys[i % groups]->pk,nonce);
        }
        test::SignFixture(fixture);
        return fixture;
    }
    void Fund(const test::Fixture& fixture)
    {
        LOCK(cs_main);
        for (size_t i = 0; i < fixture.spent.size(); ++i)
            State().CoinsTip().AddCoin(fixture.tx.vin[i].prevout,Coin{CTxOut(fixture.spent[i]),1,false},false);
    }
    std::shared_ptr<wallet::CWallet> Wallet(std::string name, unsigned char seed_byte,
                                           std::unique_ptr<wallet::WalletDatabase> database = {})
    {
        if (!database) database = wallet::CreateMockableWalletDatabase();
        auto wallet = std::make_shared<wallet::CWallet>(m_node.chain.get(),name,std::move(database));
        LOCK(wallet->cs_wallet);
        wallet->SetWalletFlag(wallet::WALLET_FLAG_DESCRIPTORS);
        wallet::MercaturaPQWalletState metadata;
        wallet::CKeyingMaterial seed(32,seed_byte);
        BOOST_REQUIRE(wallet->LoadMercaturaPQState(metadata));
        BOOST_REQUIRE(wallet->LoadMercaturaPQSeed(seed));
        wallet::WalletBatch batch(wallet->GetDatabase());
        BOOST_REQUIRE(batch.TxnBegin());
        BOOST_REQUIRE(batch.WriteMercaturaPQState(metadata) && batch.WriteMercaturaPQSeed(seed));
        BOOST_REQUIRE(batch.TxnCommit());
        ProductionPathTestAccess::Attach(*wallet,Execution());
        wallet->SetLastBlockProcessed(WITH_LOCK(cs_main,return State().m_chain.Height()),
            WITH_LOCK(cs_main,return State().m_chain.Tip()->GetBlockHash()));
        return wallet;
    }
    std::shared_ptr<wallet::CWallet> Reload(std::string name, std::unique_ptr<wallet::WalletDatabase> database,
                                           bool isolated = true)
    {
        auto wallet = std::make_shared<wallet::CWallet>(m_node.chain.get(),name,std::move(database));
        if (isolated) ProductionPathTestAccess::Attach(*wallet,Execution());
        LOCK(wallet->cs_wallet);
        BOOST_REQUIRE(wallet::WalletBatch(wallet->GetDatabase()).LoadWallet(wallet.get()) == wallet::DBErrors::LOAD_OK);
        wallet->SetLastBlockProcessed(WITH_LOCK(cs_main,return State().m_chain.Height()),
            WITH_LOCK(cs_main,return State().m_chain.Tip()->GetBlockHash()));
        return wallet;
    }
};
}
#endif
