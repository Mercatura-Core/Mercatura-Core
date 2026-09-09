// Copyright (c) 2018-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <coins.h>
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
struct MinerTestingSetup : public TestingSetup {
    explicit MinerTestingSetup(
        ChainType chain_type = ChainType::REGTEST)
        : TestingSetup{chain_type} {}

    std::shared_ptr<CBlock> Block(const uint256& prev_hash);
    std::shared_ptr<const CBlock> GoodBlock(const uint256& prev_hash);
    std::shared_ptr<const CBlock> BadBlock(const uint256& prev_hash);
    std::shared_ptr<CBlock> FinalizeBlock(std::shared_ptr<CBlock> pblock);
    void BuildChain(const uint256& root, int height, unsigned int invalid_rate, unsigned int branch_rate, unsigned int max_size, std::vector<std::shared_ptr<const CBlock>>& blocks);

    PoWHashContext m_pow_hash_context;
};

struct MainDgwReorgTestingSetup : public MinerTestingSetup {
    MainDgwReorgTestingSetup()
        : MinerTestingSetup{ChainType::MAIN} {}
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

BOOST_AUTO_TEST_CASE(mercatura_adaptive_emission_state_survives_loadblockindex)
{
    using namespace Consensus;

    auto& chainman{
        *Assert(m_node.chainman)};

    constexpr int CHECKPOINT_HEIGHT{
        MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT + 32};

    McaEmissionState expected_state;
    CAmount expected_next_subsidy{0};
    uint256 checkpoint_hash;

    {
        LOCK(::cs_main);

        CBlockIndex* prev{
            chainman.m_blockman.LookupBlockIndex(
                ::Params().GenesisBlock().GetHash())};

        if (!prev) {
            prev =
                chainman.m_blockman.AddToBlockIndex(
                    ::Params().GenesisBlock(),
                    chainman.m_best_header);
        }

        BOOST_REQUIRE(prev);
        BOOST_REQUIRE_EQUAL(
            prev->nHeight,
            0);

        // Genesis is deliberately outside Mercatura's emission state machine.
        prev->m_mca_emission_state.reset();

        // Construct a lightweight contiguous block-index ancestry through a
        // known adaptive checkpoint. These are header/index entries only:
        // no MercaHash mining, transaction data, or block files are required.
        for (int height = 1;
             height <= CHECKPOINT_HEIGHT;
             ++height) {
            CBlockHeader header;

            header.nVersion =
                ::Params().GenesisBlock().nVersion;
            header.hashPrevBlock =
                prev->GetBlockHash();
            header.hashMerkleRoot =
                uint256::ZERO;
            header.nTime =
                prev->nTime + 1;
            header.nBits =
                ::Params().GenesisBlock().nBits;
            header.nNonce =
                static_cast<uint32_t>(height);

            CBlockIndex* index{
                chainman.m_blockman.AddToBlockIndex(
                    header,
                    chainman.m_best_header)};

            BOOST_REQUIRE(index);
            BOOST_REQUIRE(
                index->pprev == prev);
            BOOST_REQUIRE_EQUAL(
                index->nHeight,
                height);

            const McaEmissionState* emission_parent{
                nullptr};

            if (height > 1) {
                BOOST_REQUIRE(
                    prev->m_mca_emission_state.has_value());

                emission_parent =
                    &*prev->m_mca_emission_state;
            }

            const auto emission_state{
                DeriveMcaEmissionState(
                    emission_parent,
                    height,
                    GetBlockProof(*index))};

            BOOST_REQUIRE(
                emission_state.has_value());

            index->m_mca_emission_state =
                *emission_state;

            prev = index;
        }

        BOOST_REQUIRE_EQUAL(
            prev->nHeight,
            CHECKPOINT_HEIGHT);
        BOOST_REQUIRE(
            prev->m_mca_emission_state.has_value());
        BOOST_REQUIRE(
            prev->m_mca_emission_state
                ->controller_initialized);

        expected_state =
            *prev->m_mca_emission_state;

        checkpoint_hash =
            prev->GetBlockHash();

        const auto next_subsidy{
            GetNextMcaBlockSubsidy(
                *prev)};

        BOOST_REQUIRE(
            next_subsidy.has_value());

        expected_next_subsidy =
            *next_subsidy;

        // Simulate process-memory loss during restart/reindex. Mercatura
        // emission state is intentionally not serialized into CDiskBlockIndex.
        for (CBlockIndex* index :
             chainman.m_blockman.GetAllBlockIndices()) {
            index->m_mca_emission_state.reset();
        }

        BOOST_CHECK(
            !prev->m_mca_emission_state.has_value());

        // This invokes the real startup reconstruction path. It walks the
        // block index parent-before-child and rebuilds every Mercatura state
        // solely from ancestry, height, and GetBlockProof().
        BOOST_REQUIRE(
            chainman.LoadBlockIndex());

        CBlockIndex* reconstructed{
            chainman.m_blockman.LookupBlockIndex(
                checkpoint_hash)};

        BOOST_REQUIRE(reconstructed);
        BOOST_REQUIRE_EQUAL(
            reconstructed->nHeight,
            CHECKPOINT_HEIGHT);
        BOOST_REQUIRE(
            reconstructed
                ->m_mca_emission_state
                .has_value());

        const auto& actual{
            *reconstructed
                 ->m_mca_emission_state};

        BOOST_CHECK_EQUAL(
            actual.height,
            expected_state.height);
        BOOST_CHECK_EQUAL(
            actual.s_q48,
            expected_state.s_q48);
        BOOST_CHECK_EQUAL(
            actual.l_q48,
            expected_state.l_q48);
        BOOST_CHECK_EQUAL(
            actual.q_q48,
            expected_state.q_q48);
        BOOST_CHECK_EQUAL(
            actual.r_q48,
            expected_state.r_q48);
        BOOST_CHECK_EQUAL(
            actual.subsidy,
            expected_state.subsidy);
        BOOST_CHECK_EQUAL(
            actual.controller_initialized,
            expected_state.controller_initialized);

        // F18 also locks the next commanded subsidy across reconstruction.
        const auto reconstructed_next_subsidy{
            GetNextMcaBlockSubsidy(
                *reconstructed)};

        BOOST_REQUIRE(
            reconstructed_next_subsidy.has_value());

        BOOST_CHECK_EQUAL(
            *reconstructed_next_subsidy,
            expected_next_subsidy);
    }
}

BOOST_AUTO_TEST_CASE(mercatura_coinbase_plus_one_overclaim_rejected)
{
    using namespace Consensus;

    auto& chainman{
        *Assert(m_node.chainman)};

    auto& chainstate{
        chainman.ActiveChainstate()};

    constexpr int BOOTSTRAP_TEST_HEIGHT{100};
    constexpr int ADAPTIVE_TEST_HEIGHT{
        MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT + 1};

    CBlockIndex* bootstrap_parent{nullptr};
    CBlockIndex* adaptive_parent{nullptr};

    {
        LOCK(::cs_main);

        const CBlock& genesis{
            ::Params().GenesisBlock()};

        CBlockIndex* prev{
            chainman.m_blockman.LookupBlockIndex(
                genesis.GetHash())};

        if (!prev) {
            prev =
                chainman.m_blockman.AddToBlockIndex(
                    genesis,
                    chainman.m_best_header);
        }

        BOOST_REQUIRE(prev);
        BOOST_REQUIRE_EQUAL(
            prev->nHeight,
            0);

        prev->m_mca_emission_state.reset();

        // Build lightweight header/index ancestry only. No MercaHash mining,
        // transaction data, or block files are needed for this consensus test.
        //
        // Stop at the parent of the adaptive candidate so candidate height
        // 311042 is validated locally without inserting that candidate.
        for (int height = 1;
             height < ADAPTIVE_TEST_HEIGHT;
             ++height) {
            CBlockHeader header;

            header.nVersion =
                genesis.nVersion;
            header.hashPrevBlock =
                prev->GetBlockHash();
            header.hashMerkleRoot =
                uint256::ZERO;
            header.nTime =
                prev->nTime + 1;
            header.nBits =
                genesis.nBits;
            header.nNonce =
                static_cast<uint32_t>(height);

            CBlockIndex* index{
                chainman.m_blockman.AddToBlockIndex(
                    header,
                    chainman.m_best_header)};

            BOOST_REQUIRE(index);
            BOOST_REQUIRE(
                index->pprev == prev);
            BOOST_REQUIRE_EQUAL(
                index->nHeight,
                height);

            const McaEmissionState* emission_parent{
                nullptr};

            if (height > 1) {
                BOOST_REQUIRE(
                    prev->m_mca_emission_state.has_value());

                emission_parent =
                    &*prev->m_mca_emission_state;
            }

            const auto emission_state{
                DeriveMcaEmissionState(
                    emission_parent,
                    height,
                    GetBlockProof(*index))};

            BOOST_REQUIRE(
                emission_state.has_value());

            index->m_mca_emission_state =
                *emission_state;

            if (height ==
                BOOTSTRAP_TEST_HEIGHT - 1) {
                bootstrap_parent = index;
            }

            if (height ==
                ADAPTIVE_TEST_HEIGHT - 1) {
                adaptive_parent = index;
            }

            prev = index;
        }

        BOOST_REQUIRE(bootstrap_parent);
        BOOST_REQUIRE(adaptive_parent);

        BOOST_REQUIRE_EQUAL(
            bootstrap_parent->nHeight,
            BOOTSTRAP_TEST_HEIGHT - 1);

        BOOST_REQUIRE_EQUAL(
            adaptive_parent->nHeight,
            ADAPTIVE_TEST_HEIGHT - 1);

        BOOST_REQUIRE(
            adaptive_parent
                ->m_mca_emission_state
                .has_value());

        BOOST_REQUIRE(
            adaptive_parent
                ->m_mca_emission_state
                ->controller_initialized);

        auto CheckPlusOneOverclaim =
            [&](CBlockIndex& parent,
                int candidate_height) {
                BOOST_REQUIRE(
                    parent.m_mca_emission_state
                        .has_value());

                const auto subsidy{
                    GetNextMcaBlockSubsidy(
                        parent)};

                BOOST_REQUIRE(
                    subsidy.has_value());

                CMutableTransaction coinbase;

                coinbase.version = 2;
                coinbase.vin.resize(1);
                coinbase.vin[0].prevout.SetNull();

                // Valid coinbase-height encoding plus padding keeps scriptSig
                // inside the consensus 2..100 byte coinbase range.
                coinbase.vin[0].scriptSig =
                    CScript{}
                    << candidate_height
                    << OP_0;

                coinbase.vout.emplace_back(
                    *subsidy + CAmount{1},
                    CScript{} << OP_TRUE);

                CBlock block;

                block.nVersion =
                    ::Params().GenesisBlock().nVersion;
                block.hashPrevBlock =
                    parent.GetBlockHash();
                block.nTime =
                    parent.nTime + 1;
                block.nBits =
                    parent.nBits;
                block.nNonce =
                    static_cast<uint32_t>(
                        candidate_height);
                block.vtx.push_back(
                    MakeTransactionRef(
                        std::move(coinbase)));

                block.hashMerkleRoot =
                    BlockMerkleRoot(block);

                CBlockIndex index{
                    block};

                uint256 block_hash{
                    block.GetHash()};

                index.pprev =
                    &parent;
                index.nHeight =
                    candidate_height;
                index.phashBlock =
                    &block_hash;

                const auto emission_state{
                    DeriveMcaEmissionState(
                        &*parent
                              .m_mca_emission_state,
                        candidate_height,
                        GetBlockProof(block))};

                BOOST_REQUIRE(
                    emission_state.has_value());

                index.m_mca_emission_state =
                    *emission_state;

                BOOST_CHECK_EQUAL(
                    GetMcaBlockSubsidy(index)
                        .value(),
                    *subsidy);

                // ConnectBlock requires the supplied UTXO view to identify the
                // exact synthetic parent as its current best block. The block
                // is coinbase-only, so no historical UTXOs are required.
                CCoinsViewCache view{
                    &chainstate.CoinsTip()};

                view.SetBestBlock(
                    parent.GetBlockHash());

                BlockValidationState state;

                const bool accepted{
                    chainstate.ConnectBlock(
                        block,
                        state,
                        &index,
                        view,
                        /*fJustCheck=*/true)};

                BOOST_CHECK(!accepted);

                BOOST_CHECK(
                    state.IsInvalid());

                BOOST_CHECK(
                    state.GetResult() ==
                    BlockValidationResult::
                        BLOCK_CONSENSUS);

                BOOST_CHECK_EQUAL(
                    state.GetRejectReason(),
                    "bad-cb-amount");
            };

        // Representative bootstrap block: exactly one base unit too much.
        CheckPlusOneOverclaim(
            *bootstrap_parent,
            BOOTSTRAP_TEST_HEIGHT);

        // First SP-LT-driven adaptive block: exactly one base unit too much.
        CheckPlusOneOverclaim(
            *adaptive_parent,
            ADAPTIVE_TEST_HEIGHT);
    }
}

BOOST_AUTO_TEST_CASE(mercatura_adaptive_fee_underclaim_preserves_controller)
{
    using namespace Consensus;

    auto& chainman{
        *Assert(m_node.chainman)};

    auto& chainstate{
        chainman.ActiveChainstate()};

    constexpr int ADAPTIVE_TEST_HEIGHT{
        MERCATURA_ADAPTIVE_ACTIVATION_HEIGHT + 1};

    constexpr CAmount INPUT_VALUE{1000};
    constexpr CAmount TX_FEE{10};
    constexpr CAmount UNDERCLAIM{3};

    CBlockIndex* adaptive_parent{nullptr};

    {
        LOCK(::cs_main);

        const CBlock& genesis{
            ::Params().GenesisBlock()};

        CBlockIndex* prev{
            chainman.m_blockman.LookupBlockIndex(
                genesis.GetHash())};

        if (!prev) {
            prev =
                chainman.m_blockman.AddToBlockIndex(
                    genesis,
                    chainman.m_best_header);
        }

        BOOST_REQUIRE(prev);
        BOOST_REQUIRE_EQUAL(
            prev->nHeight,
            0);

        prev->m_mca_emission_state.reset();

        // Construct only lightweight header/index ancestry through the parent
        // of the first SP-LT-driven adaptive block.
        for (int height = 1;
             height < ADAPTIVE_TEST_HEIGHT;
             ++height) {
            CBlockHeader header;

            header.nVersion =
                genesis.nVersion;
            header.hashPrevBlock =
                prev->GetBlockHash();
            header.hashMerkleRoot =
                uint256::ZERO;
            header.nTime =
                prev->nTime + 1;
            header.nBits =
                genesis.nBits;
            header.nNonce =
                static_cast<uint32_t>(height);

            CBlockIndex* index{
                chainman.m_blockman.AddToBlockIndex(
                    header,
                    chainman.m_best_header)};

            BOOST_REQUIRE(index);
            BOOST_REQUIRE(
                index->pprev == prev);
            BOOST_REQUIRE_EQUAL(
                index->nHeight,
                height);

            const McaEmissionState* emission_parent{
                nullptr};

            if (height > 1) {
                BOOST_REQUIRE(
                    prev->m_mca_emission_state.has_value());

                emission_parent =
                    &*prev->m_mca_emission_state;
            }

            const auto emission_state{
                DeriveMcaEmissionState(
                    emission_parent,
                    height,
                    GetBlockProof(*index))};

            BOOST_REQUIRE(
                emission_state.has_value());

            index->m_mca_emission_state =
                *emission_state;

            prev = index;
        }

        adaptive_parent = prev;

        BOOST_REQUIRE(adaptive_parent);
        BOOST_REQUIRE_EQUAL(
            adaptive_parent->nHeight,
            ADAPTIVE_TEST_HEIGHT - 1);

        BOOST_REQUIRE(
            adaptive_parent
                ->m_mca_emission_state
                .has_value());

        BOOST_REQUIRE(
            adaptive_parent
                ->m_mca_emission_state
                ->controller_initialized);

        const McaEmissionState parent_before{
            *adaptive_parent
                 ->m_mca_emission_state};

        const auto subsidy{
            GetNextMcaBlockSubsidy(
                *adaptive_parent)};

        BOOST_REQUIRE(
            subsidy.has_value());

        BOOST_REQUIRE_GT(
            *subsidy,
            UNDERCLAIM);

        const COutPoint funding_outpoint{
            Txid::FromUint256(uint256::ONE),
            0};

        McaEmissionState full_claim_state;
        McaEmissionState underclaim_state;

        auto CheckCandidate =
            [&](CAmount coinbase_claim,
                uint32_t nonce,
                McaEmissionState& observed_state) {
                CMutableTransaction spend;

                spend.version = 2;
                spend.vin.emplace_back(
                    funding_outpoint);

                spend.vout.emplace_back(
                    INPUT_VALUE - TX_FEE,
                    CScript{} << OP_TRUE);

                BOOST_CHECK_EQUAL(
                    INPUT_VALUE -
                        spend.vout[0].nValue,
                    TX_FEE);

                CMutableTransaction coinbase;

                coinbase.version = 2;
                coinbase.vin.resize(1);
                coinbase.vin[0].prevout.SetNull();
                coinbase.vin[0].scriptSig =
                    CScript{}
                    << ADAPTIVE_TEST_HEIGHT
                    << OP_0;

                coinbase.vout.emplace_back(
                    coinbase_claim,
                    CScript{} << OP_TRUE);

                CBlock block;

                block.nVersion =
                    genesis.nVersion;
                block.hashPrevBlock =
                    adaptive_parent
                        ->GetBlockHash();
                block.nTime =
                    adaptive_parent->nTime + 1;
                block.nBits =
                    adaptive_parent->nBits;
                block.nNonce =
                    nonce;

                block.vtx.push_back(
                    MakeTransactionRef(
                        std::move(coinbase)));

                block.vtx.push_back(
                    MakeTransactionRef(
                        std::move(spend)));

                block.hashMerkleRoot =
                    BlockMerkleRoot(block);

                CBlockIndex index{
                    block};

                uint256 block_hash{
                    block.GetHash()};

                index.pprev =
                    adaptive_parent;
                index.nHeight =
                    ADAPTIVE_TEST_HEIGHT;
                index.phashBlock =
                    &block_hash;

                const auto emission_state{
                    DeriveMcaEmissionState(
                        &*adaptive_parent
                              ->m_mca_emission_state,
                        ADAPTIVE_TEST_HEIGHT,
                        GetBlockProof(block))};

                BOOST_REQUIRE(
                    emission_state.has_value());

                index.m_mca_emission_state =
                    *emission_state;

                BOOST_REQUIRE(
                    GetMcaBlockSubsidy(index)
                        .has_value());

                BOOST_CHECK_EQUAL(
                    *GetMcaBlockSubsidy(index),
                    *subsidy);

                observed_state =
                    *emission_state;

                // Give this candidate an isolated UTXO view whose best block
                // is the exact synthetic adaptive parent.
                CCoinsViewCache view{
                    &chainstate.CoinsTip()};

                view.SetBestBlock(
                    adaptive_parent
                        ->GetBlockHash());

                view.AddCoin(
                    funding_outpoint,
                    Coin{
                        CTxOut{
                            INPUT_VALUE,
                            CScript{} << OP_TRUE},
                        adaptive_parent->nHeight,
                        false},
                    /*possible_overwrite=*/false);

                BlockValidationState state;

                const bool accepted{
                    chainstate.ConnectBlock(
                        block,
                        state,
                        &index,
                        view,
                        /*fJustCheck=*/true)};

                BOOST_CHECK_MESSAGE(
                    accepted,
                    state.ToString());

                BOOST_CHECK(
                    state.IsValid());
            };

        // Fees raise the maximum allowed coinbase claim.
        CheckCandidate(
            *subsidy + TX_FEE,
            /*nonce=*/1,
            full_claim_state);

        // A miner may voluntarily leave part of that available reward
        // unclaimed. Here 3 base units are deliberately not claimed.
        CheckCandidate(
            *subsidy + TX_FEE - UNDERCLAIM,
            /*nonce=*/2,
            underclaim_state);

        BOOST_CHECK_EQUAL(
            full_claim_state.height,
            underclaim_state.height);
        BOOST_CHECK_EQUAL(
            full_claim_state.s_q48,
            underclaim_state.s_q48);
        BOOST_CHECK_EQUAL(
            full_claim_state.l_q48,
            underclaim_state.l_q48);
        BOOST_CHECK_EQUAL(
            full_claim_state.q_q48,
            underclaim_state.q_q48);
        BOOST_CHECK_EQUAL(
            full_claim_state.r_q48,
            underclaim_state.r_q48);
        BOOST_CHECK_EQUAL(
            full_claim_state.subsidy,
            underclaim_state.subsidy);
        BOOST_CHECK_EQUAL(
            full_claim_state.controller_initialized,
            underclaim_state.controller_initialized);

        // Neither fee collection nor miner underclaiming may feed back into or
        // mutate the parent controller state.
        BOOST_CHECK_EQUAL(
            adaptive_parent
                ->m_mca_emission_state
                ->height,
            parent_before.height);
        BOOST_CHECK_EQUAL(
            adaptive_parent
                ->m_mca_emission_state
                ->s_q48,
            parent_before.s_q48);
        BOOST_CHECK_EQUAL(
            adaptive_parent
                ->m_mca_emission_state
                ->l_q48,
            parent_before.l_q48);
        BOOST_CHECK_EQUAL(
            adaptive_parent
                ->m_mca_emission_state
                ->q_q48,
            parent_before.q_q48);
        BOOST_CHECK_EQUAL(
            adaptive_parent
                ->m_mca_emission_state
                ->r_q48,
            parent_before.r_q48);
        BOOST_CHECK_EQUAL(
            adaptive_parent
                ->m_mca_emission_state
                ->subsidy,
            parent_before.subsidy);
        BOOST_CHECK_EQUAL(
            adaptive_parent
                ->m_mca_emission_state
                ->controller_initialized,
            parent_before.controller_initialized);
    }
}

BOOST_AUTO_TEST_CASE(mercatura_launch_capacity_just_below_limit_accepted)
{
    using namespace Consensus;

    auto& chainman{
        *Assert(m_node.chainman)};

    auto& chainstate{
        chainman.ActiveChainstate()};

    LOCK(::cs_main);

    const CBlock& genesis{
        ::Params().GenesisBlock()};

    CBlockIndex* parent{
        chainman.m_blockman.LookupBlockIndex(
            genesis.GetHash())};

    if (!parent) {
        parent =
            chainman.m_blockman.AddToBlockIndex(
                genesis,
                chainman.m_best_header);
    }

    BOOST_REQUIRE(parent);
    BOOST_REQUIRE_EQUAL(parent->nHeight, 0);

    constexpr int TEST_HEIGHT{1};
    constexpr CAmount INPUT_VALUE{1000};
    constexpr size_t LARGE_SCRIPT_BYTES{900'000};
    constexpr size_t ADJUSTABLE_SCRIPT_BYTES{100'000};
    constexpr uint64_t TARGET_CAPACITY{
        MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES - 1};

    CBlock block;
    block.nVersion = genesis.nVersion;
    block.hashPrevBlock = parent->GetBlockHash();
    block.nTime = parent->nTime + 1;
    block.nBits = genesis.nBits;
    block.nNonce = 1;

    const auto emission_state{
        DeriveMcaEmissionState(
            /*parent=*/nullptr,
            TEST_HEIGHT,
            GetBlockProof(block))};

    BOOST_REQUIRE(emission_state.has_value());

    CMutableTransaction coinbase;
    coinbase.version = 2;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig =
        CScript{} << TEST_HEIGHT << OP_0;
    coinbase.vout.emplace_back(
        emission_state->subsidy,
        CScript{} << OP_TRUE);

    block.vtx.push_back(
        MakeTransactionRef(std::move(coinbase)));

    const Txid funding_txid{
        Txid::FromUint256(uint256::ONE)};

    auto MakePaddingSpend =
        [&](uint32_t output_index,
            size_t script_bytes) {
            CMutableTransaction tx;
            tx.version = 2;
            tx.vin.emplace_back(
                COutPoint{funding_txid, output_index});

            const std::vector<unsigned char> output_bytes(
                script_bytes,
                static_cast<unsigned char>(OP_TRUE));

            CScript output_script(
                output_bytes.begin(),
                output_bytes.end());

            tx.vout.emplace_back(
                INPUT_VALUE,
                std::move(output_script));

            return tx;
        };

    CMutableTransaction large_tx{
        MakePaddingSpend(
            /*output_index=*/0,
            LARGE_SCRIPT_BYTES)};

    CMutableTransaction adjustable_tx{
        MakePaddingSpend(
            /*output_index=*/1,
            ADJUSTABLE_SCRIPT_BYTES)};

    block.vtx.push_back(
        MakeTransactionRef(std::move(large_tx)));

    block.vtx.push_back(
        MakeTransactionRef(std::move(adjustable_tx)));

    const uint64_t initial_capacity{
        GetBlockCapacityBytes(block)};

    BOOST_REQUIRE_LT(
        initial_capacity,
        TARGET_CAPACITY);

    const uint64_t additional_bytes{
        TARGET_CAPACITY - initial_capacity};

    adjustable_tx =
        MakePaddingSpend(
            /*output_index=*/1,
            ADJUSTABLE_SCRIPT_BYTES +
                additional_bytes);

    block.vtx.back() =
        MakeTransactionRef(
            std::move(adjustable_tx));

    BOOST_REQUIRE_EQUAL(
        GetBlockCapacityBytes(block),
        TARGET_CAPACITY);

    BOOST_CHECK(
        !block.vtx[1]->HasWitness());
    BOOST_CHECK(
        !block.vtx[2]->HasWitness());

    block.hashMerkleRoot =
        BlockMerkleRoot(block);

    CBlockIndex index{block};

    uint256 block_hash{
        block.GetHash()};

    index.pprev = parent;
    index.nHeight = TEST_HEIGHT;
    index.phashBlock = &block_hash;
    index.m_mca_emission_state =
        *emission_state;

    CCoinsViewCache view{
        &chainstate.CoinsTip()};

    view.SetBestBlock(
        parent->GetBlockHash());

    view.AddCoin(
        COutPoint{funding_txid, 0},
        Coin{
            CTxOut{
                INPUT_VALUE,
                CScript{} << OP_TRUE},
            parent->nHeight,
            false},
        /*possible_overwrite=*/false);

    view.AddCoin(
        COutPoint{funding_txid, 1},
        Coin{
            CTxOut{
                INPUT_VALUE,
                CScript{} << OP_TRUE},
            parent->nHeight,
            false},
        /*possible_overwrite=*/false);

    BlockValidationState state;

    const bool accepted{
        chainstate.ConnectBlock(
            block,
            state,
            &index,
            view,
            /*fJustCheck=*/true)};

    BOOST_CHECK_MESSAGE(
        accepted,
        state.ToString());

    BOOST_CHECK(
        state.IsValid());

    BOOST_CHECK_EQUAL(
        GetBlockCapacityBytes(block),
        MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES - 1);
}

BOOST_AUTO_TEST_CASE(mercatura_launch_capacity_exact_limit_accepted)
{
    using namespace Consensus;

    auto& chainman{
        *Assert(m_node.chainman)};

    auto& chainstate{
        chainman.ActiveChainstate()};

    LOCK(::cs_main);

    const CBlock& genesis{
        ::Params().GenesisBlock()};

    CBlockIndex* parent{
        chainman.m_blockman.LookupBlockIndex(
            genesis.GetHash())};

    if (!parent) {
        parent =
            chainman.m_blockman.AddToBlockIndex(
                genesis,
                chainman.m_best_header);
    }

    BOOST_REQUIRE(parent);
    BOOST_REQUIRE_EQUAL(parent->nHeight, 0);

    constexpr int TEST_HEIGHT{1};
    constexpr CAmount INPUT_VALUE{1000};
    constexpr size_t LARGE_SCRIPT_BYTES{900'000};
    constexpr size_t ADJUSTABLE_SCRIPT_BYTES{100'000};
    constexpr uint64_t TARGET_CAPACITY{
        MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES};

    CBlock block;
    block.nVersion = genesis.nVersion;
    block.hashPrevBlock = parent->GetBlockHash();
    block.nTime = parent->nTime + 1;
    block.nBits = genesis.nBits;
    block.nNonce = 1;

    const auto emission_state{
        DeriveMcaEmissionState(
            /*parent=*/nullptr,
            TEST_HEIGHT,
            GetBlockProof(block))};

    BOOST_REQUIRE(emission_state.has_value());

    CMutableTransaction coinbase;
    coinbase.version = 2;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig =
        CScript{} << TEST_HEIGHT << OP_0;
    coinbase.vout.emplace_back(
        emission_state->subsidy,
        CScript{} << OP_TRUE);

    block.vtx.push_back(
        MakeTransactionRef(std::move(coinbase)));

    const Txid funding_txid{
        Txid::FromUint256(uint256::ONE)};

    auto MakePaddingSpend =
        [&](uint32_t output_index,
            size_t script_bytes) {
            CMutableTransaction tx;
            tx.version = 2;
            tx.vin.emplace_back(
                COutPoint{funding_txid, output_index});

            const std::vector<unsigned char> output_bytes(
                script_bytes,
                static_cast<unsigned char>(OP_TRUE));

            CScript output_script(
                output_bytes.begin(),
                output_bytes.end());

            tx.vout.emplace_back(
                INPUT_VALUE,
                std::move(output_script));

            return tx;
        };

    CMutableTransaction large_tx{
        MakePaddingSpend(
            /*output_index=*/0,
            LARGE_SCRIPT_BYTES)};

    CMutableTransaction adjustable_tx{
        MakePaddingSpend(
            /*output_index=*/1,
            ADJUSTABLE_SCRIPT_BYTES)};

    block.vtx.push_back(
        MakeTransactionRef(std::move(large_tx)));

    block.vtx.push_back(
        MakeTransactionRef(std::move(adjustable_tx)));

    const uint64_t initial_capacity{
        GetBlockCapacityBytes(block)};

    BOOST_REQUIRE_LT(
        initial_capacity,
        TARGET_CAPACITY);

    const uint64_t additional_bytes{
        TARGET_CAPACITY - initial_capacity};

    adjustable_tx =
        MakePaddingSpend(
            /*output_index=*/1,
            ADJUSTABLE_SCRIPT_BYTES +
                additional_bytes);

    block.vtx.back() =
        MakeTransactionRef(
            std::move(adjustable_tx));

    BOOST_REQUIRE_EQUAL(
        GetBlockCapacityBytes(block),
        TARGET_CAPACITY);

    BOOST_CHECK(
        !block.vtx[1]->HasWitness());
    BOOST_CHECK(
        !block.vtx[2]->HasWitness());

    block.hashMerkleRoot =
        BlockMerkleRoot(block);

    CBlockIndex index{block};

    uint256 block_hash{
        block.GetHash()};

    index.pprev = parent;
    index.nHeight = TEST_HEIGHT;
    index.phashBlock = &block_hash;
    index.m_mca_emission_state =
        *emission_state;

    CCoinsViewCache view{
        &chainstate.CoinsTip()};

    view.SetBestBlock(
        parent->GetBlockHash());

    view.AddCoin(
        COutPoint{funding_txid, 0},
        Coin{
            CTxOut{
                INPUT_VALUE,
                CScript{} << OP_TRUE},
            parent->nHeight,
            false},
        /*possible_overwrite=*/false);

    view.AddCoin(
        COutPoint{funding_txid, 1},
        Coin{
            CTxOut{
                INPUT_VALUE,
                CScript{} << OP_TRUE},
            parent->nHeight,
            false},
        /*possible_overwrite=*/false);

    BlockValidationState state;

    const bool accepted{
        chainstate.ConnectBlock(
            block,
            state,
            &index,
            view,
            /*fJustCheck=*/true)};

    BOOST_CHECK_MESSAGE(
        accepted,
        state.ToString());

    BOOST_CHECK(
        state.IsValid());

    BOOST_CHECK_EQUAL(
        GetBlockCapacityBytes(block),
        MERCATURA_INITIAL_BLOCK_CAPACITY_BYTES);
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
BOOST_FIXTURE_TEST_SUITE(
    validation_block_main_dgw_tests,
    validation_block_tests::MainDgwReorgTestingSetup)

BOOST_AUTO_TEST_CASE(mercatura_dgw_reorg_uses_winning_branch_history)
{
    bool ignored;

    const auto& consensus = Params().GetConsensus();

    BOOST_REQUIRE(!consensus.fPowNoRetargeting);
    BOOST_REQUIRE_EQUAL(consensus.nDGWPastBlocks, 24);
    BOOST_REQUIRE_EQUAL(consensus.nDGWTargetTimespan, 3600);

    auto ProcessBlock =
        [&](const std::shared_ptr<const CBlock>& block) -> bool {
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

    auto LookupIndex =
        [&](const uint256& hash) -> const CBlockIndex* {
            return WITH_LOCK(
                ::cs_main,
                return m_node.chainman->m_blockman.LookupBlockIndex(hash));
        };

    auto MakeTimedBlock =
        [&](const uint256& prev_hash,
            uint32_t block_time) -> std::shared_ptr<const CBlock> {
            auto block = Block(prev_hash);
            block->nTime = block_time;
            block->nNonce = 0;

            return FinalizeBlock(std::move(block));
        };

    // Ensure the genesis block is available to the active chainstate.
    BOOST_REQUIRE(
        ProcessBlock(
            std::make_shared<CBlock>(
                Params().GenesisBlock())));

    const uint256 genesis_hash{
        Params().GenesisBlock().GetHash()};
    const uint32_t genesis_time{
        Params().GenesisBlock().nTime};

    // -----------------------------------------------------------------
    // Branch A: 25 slow blocks, each 300 seconds apart.
    //
    // This branch becomes active first. Its DGW history wants an easier
    // target, but the result is capped at mainnet powLimit.
    //
    // genesis -- A1 -- ... -- A25
    // -----------------------------------------------------------------
    std::vector<std::shared_ptr<const CBlock>> branch_a;
    branch_a.reserve(25);

    uint256 prev_hash{genesis_hash};
    uint32_t block_time{genesis_time};

    for (int i = 0; i < 25; ++i) {
        block_time += 300;

        const auto block{
            MakeTimedBlock(
                prev_hash,
                block_time)};

        BOOST_REQUIRE(ProcessBlock(block));

        branch_a.push_back(block);
        prev_hash = block->GetHash();
    }

    BOOST_REQUIRE_EQUAL(
        ActiveTipHash(),
        branch_a.back()->GetHash());

    const CBlockIndex* const a_tip{
        LookupIndex(branch_a.back()->GetHash())};
    BOOST_REQUIRE(a_tip);

    CBlockHeader a_next;
    a_next.nTime =
        branch_a.back()->GetBlockTime() + 300;

    const uint32_t a_next_bits{
        GetNextWorkRequired(
            a_tip,
            &a_next,
            consensus)};

    const uint32_t pow_limit_bits{
        UintToArith256(consensus.powLimit).GetCompact()};

    BOOST_CHECK_EQUAL(
        a_next_bits,
        pow_limit_bits);

    // -----------------------------------------------------------------
    // Branch B: independently build 25 fast blocks, each 60 seconds
    // apart, from genesis.
    //
    // FinalizeBlock() submits each valid solved header, so the entire B
    // ancestry exists branch-locally before its blocks are connected.
    //
    // genesis -- B1 -- ... -- B25
    // -----------------------------------------------------------------
    std::vector<std::shared_ptr<const CBlock>> branch_b;
    branch_b.reserve(25);

    prev_hash = genesis_hash;
    block_time = genesis_time;

    for (int i = 0; i < 25; ++i) {
        block_time += 60;

        const auto block{
            MakeTimedBlock(
                prev_hash,
                block_time)};

        branch_b.push_back(block);
        prev_hash = block->GetHash();
    }

    // B25 is the first block whose nBits is derived from B's complete
    // 24-interval DGW history:
    //
    //     24 * 60 = 1440 seconds
    //
    // powLimit * 1440 / 3600 gives canonical compact 0x20333332.
    constexpr uint32_t EXPECTED_B25_BITS{
        0x20333332U};

    BOOST_CHECK_EQUAL(
        branch_b.back()->nBits,
        EXPECTED_B25_BITS);

    const CBlockIndex* const b_tip_before_activation{
        LookupIndex(branch_b.back()->GetHash())};
    BOOST_REQUIRE(b_tip_before_activation);

    // Calculate B26 while B is still only a header/side branch.
    //
    // Its 24-target window contains B25's harder target followed by
    // B24..B2 at powLimit. Applying the established DGW recurrence and
    // B25-B1 = 1440 second span gives canonical compact 0x2030be0d.
    CBlockHeader b_next_before;
    b_next_before.nTime =
        branch_b.back()->GetBlockTime() + 60;

    const uint32_t b_next_bits_before{
        GetNextWorkRequired(
            b_tip_before_activation,
            &b_next_before,
            consensus)};

    constexpr uint32_t EXPECTED_B_NEXT_BITS{
        0x2030be0dU};

    BOOST_CHECK_EQUAL(
        b_next_bits_before,
        EXPECTED_B_NEXT_BITS);

    // The competing histories must genuinely imply different next work.
    BOOST_CHECK(
        a_next_bits != b_next_bits_before);

    // Connect B1..B24. A25 must remain active because B has not yet
    // accumulated enough work to replace it.
    for (int i = 0; i < 24; ++i) {
        BOOST_REQUIRE(
            ProcessBlock(branch_b[i]));
    }

    BOOST_CHECK_EQUAL(
        ActiveTipHash(),
        branch_a.back()->GetHash());

    // Connecting B25 adds the harder DGW work and forces the real
    // active-chain reorganization from A to B.
    BOOST_REQUIRE(
        ProcessBlock(branch_b.back()));

    BOOST_CHECK_EQUAL(
        ActiveTipHash(),
        branch_b.back()->GetHash());

    const CBlockIndex* const active_b_tip{
        LookupIndex(branch_b.back()->GetHash())};
    BOOST_REQUIRE(active_b_tip);

    CBlockHeader b_next_after;
    b_next_after.nTime =
        branch_b.back()->GetBlockTime() + 60;

    const uint32_t b_next_bits_after{
        GetNextWorkRequired(
            active_b_tip,
            &b_next_after,
            consensus)};

    // Reorganization must not leak A's old DGW history into B.
    // The active B branch must produce exactly the same next target it
    // produced branch-locally before activation.
    BOOST_CHECK_EQUAL(
        b_next_bits_after,
        b_next_bits_before);

    BOOST_CHECK_EQUAL(
        b_next_bits_after,
        EXPECTED_B_NEXT_BITS);

    BOOST_CHECK(
        b_next_bits_after != a_next_bits);
}

BOOST_AUTO_TEST_SUITE_END()
