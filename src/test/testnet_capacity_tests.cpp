// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <arith_uint256.h>
#include <blockchunk.h>
#include <chainparams.h>
#include <consensus/mercatura_controller.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <crypto/mercatura_mldsa.h>
#include <crypto/mercatura_pqkey.h>
#include <node/miner.h>
#include <node/blockstorage.h>
#include <pow.h>
#include <script/interpreter.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <test/util/net.h>
#include <test/util/logging.h>
#include <util/time.h>
#include <validation.h>

#include <array>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace {
/** Synthetic ancestry bounds this test independently of public-testnet mining
 * difficulty. TestBlockValidity checks real testnet header context, commitments,
 * UTXOs and ML-DSA scripts, with only cryptographic PoW disabled. This fixture is
 * not historical chain data, and never supplies an activation-height override. */
struct TestnetCapacitySetup : TestingSetup {
    std::vector<CBlockIndex> ancestry{8'002};
    std::vector<uint256> hashes{8'002};
    CBlockIndex* original_tip;
    CTransactionRef pq_spend;
    const Txid funding{Txid::FromUint256(uint256{1})};

    TestnetCapacitySetup() : TestingSetup{ChainType::TESTNET}
    {
        LOCK(cs_main);
        auto& chainstate = m_node.chainman->ActiveChainstate();
        original_tip = chainstate.m_chain.Tip();
        const auto& genesis = Params().GenesisBlock();
        for (size_t i{1}; i < ancestry.size(); ++i) {
            auto& index = ancestry[i];
            hashes[i] = ArithToUint256(arith_uint256{i + 1});
            index.phashBlock = &hashes[i];
            index.pprev = i == 1 ? original_tip : &ancestry[i - 1];
            index.nHeight = static_cast<int>(i);
            index.nVersion = 0x20000000;
            index.nBits = genesis.nBits;
            index.nTime = genesis.nTime + static_cast<uint32_t>(150 * i);
            index.nChainWork = index.pprev->nChainWork + GetBlockProof(index);
            index.BuildSkip();
            index.m_mca_emission_state = Consensus::DeriveMcaEmissionState(
                i == 1 ? nullptr : &*index.pprev->m_mca_emission_state,
                index.nHeight, GetBlockProof(index));
            BOOST_REQUIRE(index.m_mca_emission_state.has_value());
        }
        for (uint32_t i{0}; i < 5; ++i) {
            chainstate.CoinsTip().AddCoin(COutPoint{funding, i},
                Coin{CTxOut{1'000, CScript{} << OP_TRUE}, 1, false}, false);
        }
        std::array<uint8_t, MERCATURA_MLDSA65_SEED_SIZE> seed{};
        std::array<uint8_t, MERCATURA_MLDSA65_RANDOM_SIZE> randomness{};
        std::array<uint8_t, MERCATURA_MLDSA65_PUBLIC_KEY_SIZE> public_key{};
        std::array<uint8_t, MERCATURA_MLDSA65_SECRET_KEY_SIZE> secret_key{};
        std::array<uint8_t, MERCATURA_MLDSA65_SIGNATURE_SIZE> signature{};
        BOOST_REQUIRE(mercatura_mldsa65_keypair_from_seed(public_key.data(), public_key.size(),
            secret_key.data(), secret_key.size(), seed.data(), seed.size()));
        MercaturaPQKeyCommitment commitment{};
        BOOST_REQUIRE(ComputeMercaturaPQKeyCommitmentV1(commitment, public_key));
        const CScript owned{CScript{} << OP_2 << std::vector<unsigned char>(commitment.begin(), commitment.end())};
        const CTxOut coin{1'000, owned};
        chainstate.CoinsTip().AddCoin(COutPoint{funding, 5}, Coin{CTxOut{coin}, 1, false}, false);
        CMutableTransaction tx;
        tx.vin.emplace_back(COutPoint{funding, 5});
        tx.vin[0].scriptWitness.stack = {{0}};
        tx.vout.emplace_back(900, owned);
        PrecomputedTransactionData data;
        data.Init(tx, {coin});
        BOOST_REQUIRE(data.m_pq_ready);
        MercaturaPQHash384 digest{};
        BOOST_REQUIRE(ComputeMercaturaPQAuthDigestV1(digest, tx, 0, genesis.GetHash(), data));
        static constexpr uint8_t context[]{'M','e','r','c','a','t','u','r','a','/','P','Q','A','u','t','h','/','v','1'};
        BOOST_REQUIRE(mercatura_mldsa65_sign(signature.data(), signature.size(), digest.data(), digest.size(),
            context, sizeof(context), randomness.data(), randomness.size(), secret_key.data(), secret_key.size()));
        tx.vin[0].scriptWitness.stack = {
            {signature.begin(), signature.end()}, {public_key.begin(), public_key.end()}};
        pq_spend = MakeTransactionRef(tx);
    }

    ~TestnetCapacitySetup()
    {
        LOCK(cs_main);
        auto& chainstate = m_node.chainman->ActiveChainstate();
        chainstate.m_chain.SetTip(*original_tip);
        chainstate.CoinsTip().SetBestBlock(original_tip->GetBlockHash());
    }

    void Tip(int next_height) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        auto& chainstate = m_node.chainman->ActiveChainstate();
        chainstate.m_chain.SetTip(ancestry.at(next_height - 1));
        chainstate.CoinsTip().SetBestBlock(chainstate.m_chain.Tip()->GetBlockHash());
        SetMockTime(chainstate.m_chain.Tip()->Time() + std::chrono::seconds{150});
    }

    void Commitments(CBlock& block) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        const int position{GetWitnessCommitmentIndex(block)};
        if (position >= 0) {
            CMutableTransaction cb{*block.vtx[0]};
            cb.vout.erase(cb.vout.begin() + position);
            block.vtx[0] = MakeTransactionRef(cb);
        }
        auto& chainman = *m_node.chainman;
        chainman.GenerateCoinbaseCommitment(block, chainman.ActiveChain().Tip());
        block.hashMerkleRoot = BlockMerkleRoot(block);
        block.fChecked = block.m_checked_witness_commitment = block.m_checked_merkle_root = false;
    }

    CBlock Block(size_t target) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        auto& chainman = *m_node.chainman;
        node::BlockAssembler::Options options;
        options.coinbase_output_script = CScript{} << OP_TRUE;
        options.include_dummy_extranonce = true;
        CBlock block = node::BlockAssembler{chainman.ActiveChainstate(), nullptr, options}.CreateNewBlock()->block;
        CMutableTransaction cb{*block.vtx[0]};
        cb.vout[0].nValue = 0;
        block.vtx[0] = MakeTransactionRef(cb);
        block.vtx.push_back(pq_spend);
        auto padding = [&](uint32_t index, size_t bytes) {
            CMutableTransaction tx;
            tx.vin.emplace_back(COutPoint{funding, index});
            tx.vout.emplace_back(0, CScript{} << OP_RETURN << std::vector<unsigned char>(bytes));
            BOOST_REQUIRE(GetSerializeSize(TX_NO_WITNESS(tx)) < 1'000'000);
            return MakeTransactionRef(tx);
        };
        const size_t count{(target + 899'999) / 900'000};
        for (uint32_t i{0}; i < count; ++i) {
            block.vtx.push_back(padding(i, i + 1 == count ? 100'000 : 900'000));
        }
        Commitments(block);
        const size_t size{GetBlockCapacityBytes(block)};
        BOOST_REQUIRE(size <= target);
        block.vtx.back() = padding(static_cast<uint32_t>(count - 1), 100'000 + target - size);
        Commitments(block);
        BOOST_REQUIRE_EQUAL(GetBlockCapacityBytes(block), target);
        return block;
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(testnet_capacity_tests, TestnetCapacitySetup)

BOOST_AUTO_TEST_CASE(activation_validation_templates_and_branch_changes)
{
    LOCK(cs_main);
    auto& chainman = *m_node.chainman;
    auto& chainstate = chainman.ActiveChainstate();
    // Revisiting each side models winning-branch height changes, not work-based
    // fork selection. No 8,000-block memory-hard chain is mined by this unit test.
    for (const int height : {7'999, 8'000, 8'001, 7'999, 8'000}) {
        Tip(height);
        const size_t limit{height < 8'000 ? 1'048'576U : 4'194'304U};
        node::BlockAssembler::Options options;
        BOOST_CHECK_EQUAL(node::GetBlockTemplateMaxCapacityBytes(chainman.GetConsensus(), height, options), limit);
        auto valid = Block(limit);
        auto result = TestBlockValidity(chainstate, valid, false, true);
        BOOST_REQUIRE_MESSAGE(result.IsValid(), result.ToString());
        auto over = Block(limit + 1);
        result = TestBlockValidity(chainstate, over, false, true);
        BOOST_CHECK_EQUAL(result.GetRejectReason(), "bad-blk-capacity");
        if (height < 8'000) {
            auto future = Block(4'194'304);
            result = TestBlockValidity(chainstate, future, false, true);
            BOOST_CHECK_EQUAL(result.GetRejectReason(), "bad-blk-capacity");
        }
        // Direct connection (-reindex-chainstate's path) must make the same decision.
        for (const auto* block : {&valid, &over}) {
            CBlockIndex index{*block};
            const uint256 hash{block->GetHash()};
            index.phashBlock = &hash;
            index.pprev = chainstate.m_chain.Tip();
            index.nHeight = height;
            index.m_mca_emission_state = Consensus::DeriveMcaEmissionState(
                &*index.pprev->m_mca_emission_state, height, GetBlockProof(index));
            CCoinsViewCache view{&chainstate.CoinsTip()};
            BlockValidationState state;
            const bool ok{chainstate.ConnectBlock(*block, state, &index, view, true)};
            BOOST_CHECK_EQUAL(ok, block == &valid);
            if (!ok) BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-blk-capacity");
        }
        // Real signature authorization remains authoritative on both sides.
        CBlock corrupt{valid};
        CMutableTransaction tx{*corrupt.vtx[1]};
        tx.vin[0].scriptWitness.stack[0][0] ^= 1;
        corrupt.vtx[1] = MakeTransactionRef(tx);
        Commitments(corrupt);
        result = TestBlockValidity(chainstate, corrupt, false, true);
        BOOST_CHECK(!result.IsValid());
        BOOST_CHECK(result.GetRejectReason().find("script-verify") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(requested_chunk_admission_across_activation)
{
    LOCK(NetEventsInterface::g_msgproc_mutex);
    auto& connman = static_cast<ConnmanTestMsg&>(*m_node.connman);
    auto& chainman = *m_node.chainman;
    CBlockIndex* const saved_header{WITH_LOCK(cs_main, return chainman.m_best_header)};
    NodeId id{0};
    for (const int height : {7'999, 8'000, 8'001, 7'999}) {
        CBlock block;
        CBlockIndex* index;
        {
            LOCK(cs_main);
            Tip(height);
            block = Block(4'194'304);
            // Register a synthetic known header for the requested-block handler;
            // this fixture does not claim that its public-difficulty PoW is mined.
            index = chainman.m_blockman.AddToBlockIndex(block, chainman.m_best_header);
            index->pprev = chainman.ActiveChain().Tip();
            index->nHeight = height;
            index->BuildSkip();
        }
        auto* peer = new CNode{id++, nullptr, CAddress{}, 0, 0, CAddress{}, "",
            ConnectionType::OUTBOUND_FULL_RELAY, false, 0};
        connman.AddTestNode(*peer);
        connman.Handshake(*peer, true, ServiceFlags(NODE_NETWORK | NODE_WITNESS),
            ServiceFlags(NODE_NETWORK | NODE_WITNESS), PROTOCOL_VERSION, true);
        BOOST_REQUIRE(chainman.GetConsensus().nWitnessResourceHeight < 0);
        BOOST_REQUIRE(m_node.peerman->FetchBlock(peer->GetId(), *index).has_value());
        MercaturaBlockMeta meta;
        meta.block_hash = block.GetHash();
        meta.total_size = GetBlockCapacityBytes(block);
        meta.chunk_size = static_cast<uint32_t>(MERCATURA_BLOCK_CHUNK_SIZE);
        meta.chunk_count = static_cast<uint32_t>(GetMercaturaBlockChunkCount(meta.total_size));
        auto send = [&](CSerializedNetMsg message) {
            connman.FlushSendBuffer(*peer);
            BOOST_REQUIRE(connman.ReceiveMsgFrom(*peer, std::move(message)));
            peer->fPauseSend = false;
            connman.ProcessMessagesOnce(*peer);
        };
        if (height < 8'000) {
            ASSERT_DEBUG_LOG("above Mercatura capacity");
            send(NetMsg::Make(NetMsgType::BLKMETA, meta));
            BOOST_CHECK(peer->fDisconnect);
        } else {
            send(NetMsg::Make(NetMsgType::BLKMETA, meta));
            BOOST_REQUIRE(!peer->fDisconnect);
            DataStream wire;
            wire << TX_WITH_WITNESS(block);
            MercaturaBlockChunk chunk;
            chunk.block_hash = meta.block_hash;
            const auto bytes = std::span{wire}.first(MERCATURA_BLOCK_CHUNK_SIZE);
            chunk.data.assign(reinterpret_cast<const unsigned char*>(bytes.data()),
                reinterpret_cast<const unsigned char*>(bytes.data()) + bytes.size());
            ASSERT_DEBUG_LOG("requesting next");
            send(NetMsg::Make(NetMsgType::BLKCHUNK, chunk));
            BOOST_CHECK(!peer->fDisconnect);
        }
        m_node.peerman->FinalizeNode(*peer);
        connman.ClearTestNodes();
    }
    WITH_LOCK(cs_main, chainman.m_best_header = saved_header);
}

BOOST_AUTO_TEST_SUITE_END()
