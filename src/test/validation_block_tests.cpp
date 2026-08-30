// Copyright (c) 2018-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/mercatura_controller.h>
#include <consensus/validation.h>
#include <node/miner.h>
#include <pow.h>
#include <random.h>
#include <test/util/common.h>
#include <test/util/random.h>
#include <test/util/script.h>
#include <test/util/setup_common.h>
#include <util/time.h>
#include <validation.h>
#include <validationinterface.h>

#include <thread>

using kernel::ChainstateRole;
using node::BlockAssembler;

namespace validation_block_tests {
struct MinerTestingSetup : public RegTestingSetup {
    std::shared_ptr<CBlock> Block(const uint256& prev_hash);
    std::shared_ptr<const CBlock> GoodBlock(const uint256& prev_hash);
    std::shared_ptr<const CBlock> BadBlock(const uint256& prev_hash);
    std::shared_ptr<CBlock> FinalizeBlock(std::shared_ptr<CBlock> pblock);
    void BuildChain(const uint256& root, int height, unsigned int invalid_rate, unsigned int branch_rate, unsigned int max_size, std::vector<std::shared_ptr<const CBlock>>& blocks);

    PoWHashContext m_pow_hash_context;
};
} // namespace validation_block_tests

BOOST_FIXTURE_TEST_SUITE(validation_block_tests, MinerTestingSetup)

struct TestSubscriber final : public CValidationInterface {
    uint256 m_expected_tip;

    explicit TestSubscriber(uint256 tip) : m_expected_tip(tip) {}

    void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, pindexNew->GetBlockHash());
    }

    void BlockConnected(const ChainstateRole& role, const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, block->hashPrevBlock);
        BOOST_CHECK_EQUAL(m_expected_tip, pindex->pprev->GetBlockHash());

        m_expected_tip = block->GetHash();
    }

    void BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, block->GetHash());
        BOOST_CHECK_EQUAL(m_expected_tip, pindex->GetBlockHash());

        m_expected_tip = block->hashPrevBlock;
    }
};

std::shared_ptr<CBlock> MinerTestingSetup::Block(const uint256& prev_hash)
{
    static int i = 0;
    static uint64_t time = Params().GenesisBlock().nTime;

    BlockAssembler::Options options;
    options.coinbase_output_script = CScript{} << i++ << OP_TRUE;
    options.include_dummy_extranonce = true;
    auto ptemplate = BlockAssembler{m_node.chainman->ActiveChainstate(), m_node.mempool.get(), options}.CreateNewBlock();
    auto pblock = std::make_shared<CBlock>(ptemplate->block);
    pblock->hashPrevBlock = prev_hash;
    pblock->nTime = ++time;

    const CBlockIndex* prev_index{
        WITH_LOCK(
            ::cs_main,
            return m_node.chainman->m_blockman.LookupBlockIndex(prev_hash))};

    BOOST_REQUIRE(prev_index);

    // BlockAssembler initially creates a template for the active tip. This
    // helper can deliberately construct a block on a side branch, so replace
    // the template subsidy with the command belonging to the explicitly
    // requested parent.
    const auto block_subsidy{
        WITH_LOCK(
            ::cs_main,
            return Consensus::GetNextMcaBlockSubsidy(*prev_index))};

    BOOST_REQUIRE(block_subsidy.has_value());

    // Make the coinbase transaction with two outputs:
    // One zero-value one that has a unique pubkey to make sure that blocks at the same height can have a different hash
    // Another one that has the coinbase reward in a P2WSH with OP_TRUE as witness program to make it easy to spend
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vout.resize(2);
    txCoinbase.vout[1].scriptPubKey = P2WSH_OP_TRUE;
    txCoinbase.vout[1].nValue = *block_subsidy;
    txCoinbase.vout[0].nValue = 0;
    txCoinbase.vin[0].scriptWitness.SetNull();

    // Always pad with OP_0 as dummy extraNonce (also avoids bad-cb-length error for block <=16)
    const int prev_height{
        WITH_LOCK(
            ::cs_main,
            return prev_index->nHeight)};

    txCoinbase.vin[0].scriptSig =
        CScript{} << prev_height + 1 << OP_0;

    txCoinbase.nLockTime =
        static_cast<uint32_t>(prev_height);

    pblock->vtx[0] =
        MakeTransactionRef(std::move(txCoinbase));

    return pblock;
}

std::shared_ptr<CBlock> MinerTestingSetup::FinalizeBlock(std::shared_ptr<CBlock> pblock)
{
    const CBlockIndex* prev_block{WITH_LOCK(::cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(pblock->hashPrevBlock))};
    BOOST_REQUIRE(prev_block);

    // The template may have originally been created for a different active
    // tip. Difficulty must therefore also be derived from the explicitly
    // requested parent before proof of work is mined.
    pblock->nBits = GetNextWorkRequired(
        prev_block,
        pblock.get(),
        Params().GetConsensus());

    m_node.chainman->GenerateCoinbaseCommitment(*pblock, prev_block);

    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);

    while (!CheckProofOfWork(
        *pblock,
        Params().GetConsensus(),
        m_pow_hash_context)) {
        ++(pblock->nNonce);
    }

    // submit block header, so that miner can get the block height from the
    // global state and the node has the topology of the chain
    BlockValidationState ignored;
    BOOST_CHECK(
        Assert(m_node.chainman)->ProcessNewBlockHeaders(
            {{*pblock}},
            /*min_pow_checked=*/true,
            ignored,
            /*ppindex=*/nullptr,
            PoWCheckStatus::CHECKED));

    return pblock;
}

// construct a valid block
std::shared_ptr<const CBlock> MinerTestingSetup::GoodBlock(const uint256& prev_hash)
{
    return FinalizeBlock(Block(prev_hash));
}

// construct an invalid block (but with a valid header)
std::shared_ptr<const CBlock> MinerTestingSetup::BadBlock(const uint256& prev_hash)
{
    auto pblock = Block(prev_hash);

    CMutableTransaction coinbase_spend;
    coinbase_spend.vin.emplace_back(COutPoint(pblock->vtx[0]->GetHash(), 0), CScript(), 0);
    coinbase_spend.vout.push_back(pblock->vtx[0]->vout[0]);

    CTransactionRef tx = MakeTransactionRef(coinbase_spend);
    pblock->vtx.push_back(tx);

    auto ret = FinalizeBlock(pblock);
    return ret;
}

// NOLINTNEXTLINE(misc-no-recursion)
void MinerTestingSetup::BuildChain(const uint256& root, int height, const unsigned int invalid_rate, const unsigned int branch_rate, const unsigned int max_size, std::vector<std::shared_ptr<const CBlock>>& blocks)
{
    if (height <= 0 || blocks.size() >= max_size) return;

    bool gen_invalid = m_rng.randrange(100U) < invalid_rate;
    bool gen_fork = m_rng.randrange(100U) < branch_rate;

    const std::shared_ptr<const CBlock> pblock = gen_invalid ? BadBlock(root) : GoodBlock(root);
    blocks.push_back(pblock);
    if (!gen_invalid) {
        BuildChain(pblock->GetHash(), height - 1, invalid_rate, branch_rate, max_size, blocks);
    }

    if (gen_fork) {
        blocks.push_back(GoodBlock(root));
        BuildChain(blocks.back()->GetHash(), height - 1, invalid_rate, branch_rate, max_size, blocks);
    }
}

BOOST_AUTO_TEST_CASE(mercatura_emission_state_survives_reorg)
{
    bool ignored;

    auto ProcessBlock =
        [&](std::shared_ptr<const CBlock> block) -> bool {
            return Assert(m_node.chainman)->ProcessNewBlock(
                block,
                /*force_processing=*/true,
                /*min_pow_checked=*/true,
                /*new_block=*/&ignored);
        };

    auto ActiveTipHash = [&]() {
        return WITH_LOCK(
            Assert(m_node.chainman)->GetMutex(),
            return m_node.chainman->ActiveChain().Tip()->GetBlockHash());
    };

    auto GetEmissionState =
        [&](const uint256& hash) {
            LOCK(::cs_main);

            const CBlockIndex* index{
                Assert(
                    m_node.chainman->m_blockman.LookupBlockIndex(
                        hash))};

            BOOST_REQUIRE(
                index->m_mca_emission_state.has_value());

            BOOST_REQUIRE_EQUAL(
                index->m_mca_emission_state->height,
                index->nHeight);

            return *index->m_mca_emission_state;
        };

    auto CheckStateEqual =
        [](const Consensus::McaEmissionState& actual,
           const Consensus::McaEmissionState& expected) {
            BOOST_CHECK_EQUAL(
                actual.height,
                expected.height);

            BOOST_CHECK_EQUAL(
                actual.s_q48,
                expected.s_q48);

            BOOST_CHECK_EQUAL(
                actual.l_q48,
                expected.l_q48);

            BOOST_CHECK_EQUAL(
                actual.q_q48,
                expected.q_q48);

            BOOST_CHECK_EQUAL(
                actual.r_q48,
                expected.r_q48);

            BOOST_CHECK_EQUAL(
                actual.subsidy,
                expected.subsidy);

            BOOST_CHECK_EQUAL(
                actual.controller_initialized,
                expected.controller_initialized);
        };

    // Establish a normal active chain:
    //
    // genesis -- A1 -- A2
    //
    BOOST_REQUIRE(
        ProcessBlock(
            std::make_shared<CBlock>(
                Params().GenesisBlock())));

    const auto a1{
        GoodBlock(
            Params().GenesisBlock().GetHash())};

    BOOST_REQUIRE(ProcessBlock(a1));

    const auto a2{
        GoodBlock(a1->GetHash())};

    BOOST_REQUIRE(ProcessBlock(a2));

    BOOST_CHECK_EQUAL(
        ActiveTipHash(),
        a2->GetHash());

    const auto a1_state_before{
        GetEmissionState(a1->GetHash())};

    const auto a2_state_before{
        GetEmissionState(a2->GetHash())};

    // Build headers for a competing branch before connecting its blocks:
    //
    // genesis -- A1 -- A2
    //               |
    //                B2 -- B3
    //
    // GoodBlock() submits each header during FinalizeBlock(), so these monetary
    // states exist branch-locally before the blocks trigger an active-chain
    // change.
    const auto b2{
        GoodBlock(a1->GetHash())};

    const auto b3{
        GoodBlock(b2->GetHash())};

    const auto b2_state_pre_activation{
        GetEmissionState(b2->GetHash())};

    const auto b3_state_pre_activation{
        GetEmissionState(b3->GetHash())};

    // Connecting B2 alone does not give the fork more work than A2.
    BOOST_REQUIRE(ProcessBlock(b2));

    // B3 makes the competing branch longer and forces activation of B.
    BOOST_REQUIRE(ProcessBlock(b3));

    BOOST_CHECK_EQUAL(
        ActiveTipHash(),
        b3->GetHash());

    // Activation must reuse the state derived when the headers entered the
    // block index. Reorg handling must not recalculate or mutate it.
    CheckStateEqual(
        GetEmissionState(b2->GetHash()),
        b2_state_pre_activation);

    CheckStateEqual(
        GetEmissionState(b3->GetHash()),
        b3_state_pre_activation);

    // The disconnected A branch remains in the block tree with its original
    // branch-local monetary state intact.
    CheckStateEqual(
        GetEmissionState(a1->GetHash()),
        a1_state_before);

    CheckStateEqual(
        GetEmissionState(a2->GetHash()),
        a2_state_before);

    // While B is active, extend the old A branch. This exercises block
    // construction from a non-active parent and proves the side branch gets
    // its own already-derived monetary states.
    const auto a3{
        GoodBlock(a2->GetHash())};

    const auto a4{
        GoodBlock(a3->GetHash())};

    const auto a3_state_pre_activation{
        GetEmissionState(a3->GetHash())};

    const auto a4_state_pre_activation{
        GetEmissionState(a4->GetHash())};

    BOOST_REQUIRE(ProcessBlock(a3));
    BOOST_REQUIRE(ProcessBlock(a4));

    BOOST_CHECK_EQUAL(
        ActiveTipHash(),
        a4->GetHash());

    // Switching back to A again reuses the states that were already attached
    // to A3/A4 while they were a side branch.
    CheckStateEqual(
        GetEmissionState(a3->GetHash()),
        a3_state_pre_activation);

    CheckStateEqual(
        GetEmissionState(a4->GetHash()),
        a4_state_pre_activation);

    // And the now-disconnected B branch still retains exactly the state it had
    // before either reorg.
    CheckStateEqual(
        GetEmissionState(b2->GetHash()),
        b2_state_pre_activation);

    CheckStateEqual(
        GetEmissionState(b3->GetHash()),
        b3_state_pre_activation);

    CheckStateEqual(
        GetEmissionState(a2->GetHash()),
        a2_state_before);

    // ---------------------------------------------------------------------
    // Replay/restart reconstruction
    // ---------------------------------------------------------------------
    //
    // m_mca_emission_state is deliberately memory-only. Simulate losing all
    // of that runtime state, then invoke the same LoadBlockIndex path used
    // during startup. Every indexed branch must reconstruct bit-for-bit from
    // ancestry and block work alone.
    {
        LOCK(::cs_main);

        auto& chainman{
            *Assert(m_node.chainman)};

        for (CBlockIndex* index :
             chainman.m_blockman.GetAllBlockIndices()) {
            index->m_mca_emission_state.reset();
        }

        // Genesis deliberately remains outside the monetary state machine.
        BOOST_REQUIRE(
            !chainman.ActiveChain().Genesis()
                 ->m_mca_emission_state.has_value());

        BOOST_REQUIRE(
            chainman.LoadBlockIndex());
    }

    // The active branch must reproduce exactly.
    CheckStateEqual(
        GetEmissionState(a1->GetHash()),
        a1_state_before);

    CheckStateEqual(
        GetEmissionState(a2->GetHash()),
        a2_state_before);

    CheckStateEqual(
        GetEmissionState(a3->GetHash()),
        a3_state_pre_activation);

    CheckStateEqual(
        GetEmissionState(a4->GetHash()),
        a4_state_pre_activation);

    // The disconnected side branch must also reproduce exactly. Startup
    // reconstruction is therefore block-tree based, not active-chain based.
    CheckStateEqual(
        GetEmissionState(b2->GetHash()),
        b2_state_pre_activation);

    CheckStateEqual(
        GetEmissionState(b3->GetHash()),
        b3_state_pre_activation);

    // Genesis still has no Mercatura emission state after replay.
    {
        LOCK(::cs_main);

        BOOST_CHECK(
            !Assert(m_node.chainman)
                 ->ActiveChain()
                 .Genesis()
                 ->m_mca_emission_state.has_value());
    }
}

BOOST_AUTO_TEST_CASE(missing_prev_rejected_before_mercahash)
{
    CBlockHeader header{Params().GenesisBlock()};
    header.hashPrevBlock = uint256{"0000000000000000000000000000000000000000000000000000000000000001"};

    // Make the PoW target invalid as well. If AcceptBlockHeader() performs
    // MercaHash before checking whether the previous block exists, this would
    // fail as "high-hash" instead of the cheaper missing-prev rejection.
    header.nBits = 0;

    BlockValidationState state;
    const bool accepted{
        Assert(m_node.chainman)->ProcessNewBlockHeaders(
            {{header}},
            /*min_pow_checked=*/true,
            state)};

    BOOST_CHECK(!accepted);
    BOOST_CHECK(
        state.GetResult() ==
        BlockValidationResult::BLOCK_MISSING_PREV);
    BOOST_CHECK_EQUAL(
        state.GetRejectReason(),
        "prev-blk-not-found");
}

BOOST_AUTO_TEST_CASE(processnewblock_signals_ordering)
{
    // build a large-ish chain that's likely to have some forks
    std::vector<std::shared_ptr<const CBlock>> blocks;
    while (blocks.size() < 50) {
        blocks.clear();
        BuildChain(Params().GenesisBlock().GetHash(), 100, 15, 10, 500, blocks);
    }

    bool ignored;
    // Connect the genesis block and drain any outstanding events
    BOOST_CHECK(Assert(m_node.chainman)->ProcessNewBlock(std::make_shared<CBlock>(Params().GenesisBlock()), true, true, &ignored));
    m_node.validation_signals->SyncWithValidationInterfaceQueue();

    // subscribe to events (this subscriber will validate event ordering)
    const CBlockIndex* initial_tip = nullptr;
    {
        LOCK(cs_main);
        initial_tip = m_node.chainman->ActiveChain().Tip();
    }
    auto sub = std::make_shared<TestSubscriber>(initial_tip->GetBlockHash());
    m_node.validation_signals->RegisterSharedValidationInterface(sub);

    // create a bunch of threads that repeatedly process a block generated above at random
    // this will create parallelism and randomness inside validation - the ValidationInterface
    // will subscribe to events generated during block validation and assert on ordering invariance
    std::vector<std::thread> threads;
    threads.reserve(10);
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&]() {
            bool ignored;
            FastRandomContext insecure;
            for (int i = 0; i < 1000; i++) {
                const auto& block = blocks[insecure.randrange(blocks.size() - 1)];
                Assert(m_node.chainman)->ProcessNewBlock(block, true, true, &ignored);
            }

            // to make sure that eventually we process the full chain - do it here
            for (const auto& block : blocks) {
                if (block->vtx.size() == 1) {
                    bool processed = Assert(m_node.chainman)->ProcessNewBlock(block, true, true, &ignored);
                    assert(processed);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    m_node.validation_signals->SyncWithValidationInterfaceQueue();

    m_node.validation_signals->UnregisterSharedValidationInterface(sub);

    LOCK(cs_main);
    BOOST_CHECK_EQUAL(sub->m_expected_tip, m_node.chainman->ActiveChain().Tip()->GetBlockHash());
}

/**
 * Test that mempool updates happen atomically with reorgs.
 *
 * This prevents RPC clients, among others, from retrieving immediately-out-of-date mempool data
 * during large reorgs.
 *
 * The test verifies this by creating a chain of `num_txs` blocks, matures their coinbases, and then
 * submits txns spending from their coinbase to the mempool. A fork chain is then processed,
 * invalidating the txns and evicting them from the mempool.
 *
 * We verify that the mempool updates atomically by polling it continuously
 * from another thread during the reorg and checking that its size only changes
 * once. The size changing exactly once indicates that the polling thread's
 * view of the mempool is either consistent with the chain state before reorg,
 * or consistent with the chain state after the reorg, and not just consistent
 * with some intermediate state during the reorg.
 */
BOOST_AUTO_TEST_CASE(mempool_locks_reorg)
{
    bool ignored;
    auto ProcessBlock = [&](std::shared_ptr<const CBlock> block) -> bool {
        return Assert(m_node.chainman)->ProcessNewBlock(block, /*force_processing=*/true, /*min_pow_checked=*/true, /*new_block=*/&ignored);
    };

    // Process all mined blocks
    BOOST_REQUIRE(ProcessBlock(std::make_shared<CBlock>(Params().GenesisBlock())));
    auto last_mined = GoodBlock(Params().GenesisBlock().GetHash());
    BOOST_REQUIRE(ProcessBlock(last_mined));

    // Run the test multiple times
    for (int test_runs = 3; test_runs > 0; --test_runs) {
        BOOST_CHECK_EQUAL(last_mined->GetHash(), WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain().Tip()->GetBlockHash()));

        // Later on split from here
        const uint256 split_hash{last_mined->hashPrevBlock};

        // Create a bunch of transactions to spend the miner rewards of the
        // most recent blocks
        std::vector<CTransactionRef> txs;
        for (int num_txs = 22; num_txs > 0; --num_txs) {
            CMutableTransaction mtx;
            mtx.vin.emplace_back(COutPoint{last_mined->vtx[0]->GetHash(), 1}, CScript{});
            mtx.vin[0].scriptWitness.stack.push_back(WITNESS_STACK_ELEM_OP_TRUE);
            mtx.vout.push_back(last_mined->vtx[0]->vout[1]);
            mtx.vout[0].nValue -= 1000;
            txs.push_back(MakeTransactionRef(mtx));

            last_mined = GoodBlock(last_mined->GetHash());
            BOOST_REQUIRE(ProcessBlock(last_mined));
        }

        // Mature the inputs of the txs
        for (int j = COINBASE_MATURITY; j > 0; --j) {
            last_mined = GoodBlock(last_mined->GetHash());
            BOOST_REQUIRE(ProcessBlock(last_mined));
        }

        // Mine a reorg (and hold it back) before adding the txs to the mempool
        const uint256 tip_init{last_mined->GetHash()};

        std::vector<std::shared_ptr<const CBlock>> reorg;
        last_mined = GoodBlock(split_hash);
        reorg.push_back(last_mined);
        for (size_t j = COINBASE_MATURITY + txs.size() + 1; j > 0; --j) {
            last_mined = GoodBlock(last_mined->GetHash());
            reorg.push_back(last_mined);
        }

        // Add the txs to the tx pool
        {
            LOCK(cs_main);
            for (const auto& tx : txs) {
                const MempoolAcceptResult result = m_node.chainman->ProcessTransaction(tx);
                BOOST_REQUIRE(result.m_result_type == MempoolAcceptResult::ResultType::VALID);
            }
        }

        // Check that all txs are in the pool
        {
            BOOST_CHECK_EQUAL(m_node.mempool->size(), txs.size());
        }

        // Run a thread that simulates an RPC caller that is polling while
        // validation is doing a reorg
        std::thread rpc_thread{[&]() {
            // This thread is checking that the mempool either contains all of
            // the transactions invalidated by the reorg, or none of them, and
            // not some intermediate amount.
            while (true) {
                LOCK(m_node.mempool->cs);
                if (m_node.mempool->size() == 0) {
                    // We are done with the reorg
                    break;
                }
                // Internally, we might be in the middle of the reorg, but
                // externally the reorg to the most-proof-of-work chain should
                // be atomic. So the caller assumes that the returned mempool
                // is consistent. That is, it has all txs that were there
                // before the reorg.
                assert(m_node.mempool->size() == txs.size());
                continue;
            }
            LOCK(cs_main);
            // We are done with the reorg, so the tip must have changed
            assert(tip_init != m_node.chainman->ActiveChain().Tip()->GetBlockHash());
        }};

        // Submit the reorg in this thread to invalidate and remove the txs from the tx pool
        for (const auto& b : reorg) {
            ProcessBlock(b);
        }
        // Check that the reorg was eventually successful
        BOOST_CHECK_EQUAL(last_mined->GetHash(), WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain().Tip()->GetBlockHash()));

        // We can join the other thread, which returns when the reorg was successful
        rpc_thread.join();
    }
}

BOOST_AUTO_TEST_CASE(witness_commitment_index)
{
    LOCK(Assert(m_node.chainman)->GetMutex());
    CScript pubKey;
    pubKey << 1 << OP_TRUE;
    BlockAssembler::Options options;
    options.coinbase_output_script = pubKey;
    options.include_dummy_extranonce = true;
    auto ptemplate = BlockAssembler{m_node.chainman->ActiveChainstate(), m_node.mempool.get(), options}.CreateNewBlock();
    CBlock pblock = ptemplate->block;

    CTxOut witness;
    witness.scriptPubKey.resize(MINIMUM_WITNESS_COMMITMENT);
    witness.scriptPubKey[0] = OP_RETURN;
    witness.scriptPubKey[1] = 0x24;
    witness.scriptPubKey[2] = 0xaa;
    witness.scriptPubKey[3] = 0x21;
    witness.scriptPubKey[4] = 0xa9;
    witness.scriptPubKey[5] = 0xed;

    // A witness larger than the minimum size is still valid
    CTxOut min_plus_one = witness;
    min_plus_one.scriptPubKey.resize(MINIMUM_WITNESS_COMMITMENT + 1);

    CTxOut invalid = witness;
    invalid.scriptPubKey[0] = OP_VERIFY;

    CMutableTransaction txCoinbase(*pblock.vtx[0]);
    txCoinbase.vout.resize(4);
    txCoinbase.vout[0] = witness;
    txCoinbase.vout[1] = witness;
    txCoinbase.vout[2] = min_plus_one;
    txCoinbase.vout[3] = invalid;
    pblock.vtx[0] = MakeTransactionRef(std::move(txCoinbase));

    BOOST_CHECK_EQUAL(GetWitnessCommitmentIndex(pblock), 2);
}
BOOST_AUTO_TEST_SUITE_END()
