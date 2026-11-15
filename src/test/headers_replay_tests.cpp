// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <net_processing.h>
#include <node/kernel_notifications.h>
#include <pow.h>
#include <test/util/logging.h>
#include <test/util/net.h>
#include <test/util/setup_common.h>
#include <tinyformat.h>
#include <validation.h>

#include <array>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace {
struct HeaderReplaySetup : RegTestingSetup {
    PoWHashContext m_miner;

    CBlockHeader Header(const CBlockHeader& parent, uint64_t tag)
    {
        CBlockHeader header;
        header.nVersion = 0x20000000;
        header.hashPrevBlock = parent.GetHash();
        header.hashMerkleRoot = ArithToUint256(arith_uint256{tag});
        header.nTime = parent.nTime + 1;
        header.nBits = parent.nBits;
        while (!CheckProofOfWork(header, m_node.chainman->GetConsensus(), m_miner)) ++header.nNonce;
        return header;
    }

    void Accept(const CBlockHeader& header)
    {
        BlockValidationState state;
        BOOST_REQUIRE(m_node.chainman->ProcessNewBlockHeaders({{header}}, true, state));
    }

    bool Check(std::span<const CBlockHeader> headers, PoWHashContext& context)
    {
        return HasValidProofOfWork(headers, m_node.chainman->GetConsensus(), context, m_node.chainman->m_blockman);
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(headers_replay_tests, HeaderReplaySetup)

BOOST_AUTO_TEST_CASE(new_known_and_mixed_headers)
{
    const auto first{Header(Params().GenesisBlock(), 1)};
    PoWHashContext context;
    BOOST_REQUIRE(Check({&first, 1}, context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), 1);
    // A successful precheck alone must not create reusable validity state.
    BOOST_REQUIRE(Check({&first, 1}, context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), 2);
    Accept(first);

    const auto count{WITH_LOCK(cs_main, return m_node.chainman->m_blockman.m_block_index.size())};
    PoWHashContext other_peer;
    BOOST_REQUIRE(Check({&first, 1}, context));
    BOOST_REQUIRE(Check({&first, 1}, other_peer));
    BOOST_CHECK_EQUAL(context.HashesComputed(), 2);
    BOOST_CHECK_EQUAL(other_peer.HashesComputed(), 0);

    const auto second{Header(first, 2)};
    const std::array batch{first, second};
    BOOST_REQUIRE(Check(batch, other_peer));
    BOOST_CHECK_EQUAL(other_peer.HashesComputed(), 1);
    Accept(second);
    BOOST_REQUIRE(Check(batch, other_peer));
    BOOST_CHECK_EQUAL(other_peer.HashesComputed(), 1);
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return m_node.chainman->m_blockman.m_block_index.size()), count + 1);
}

BOOST_AUTO_TEST_CASE(all_header_fields_and_invalid_pow)
{
    const auto original{Header(Params().GenesisBlock(), 3)};
    Accept(original);
    std::array<CBlockHeader, 6> altered;
    altered.fill(original);
    ++altered[0].nVersion;
    altered[1].hashPrevBlock.SetNull();
    altered[2].hashMerkleRoot.SetNull();
    ++altered[3].nTime;
    altered[4].nBits = 0; // Always invalid, regardless of the MercaHash output.
    ++altered[5].nNonce;
    PoWHashContext reference;
    PoWHashContext context;
    for (const auto& header : altered) {
        const bool valid{CheckProofOfWork(header, m_node.chainman->GetConsensus(), reference)};
        const auto before{context.HashesComputed()};
        BOOST_CHECK_EQUAL(Check({&header, 1}, context), valid);
        BOOST_CHECK_EQUAL(context.HashesComputed(), before + 1);
    }
    BlockValidationState state;
    BOOST_CHECK(!m_node.chainman->ProcessNewBlockHeaders({{altered[4]}}, true, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "high-hash");
    BOOST_CHECK(WITH_LOCK(cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(altered[4].GetHash())) == nullptr);
    const auto before{context.HashesComputed()};
    BOOST_CHECK(Check({&original, 1}, context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), before);
}

BOOST_AUTO_TEST_CASE(index_presence_and_claimed_identity_are_insufficient)
{
    const auto header{Header(Params().GenesisBlock(), 4)};
    auto& blockman{m_node.chainman->m_blockman};
    PoWHashContext context;
    {
        LOCK(cs_main);
        // Model an unresolved index entry, not a validated header.
        auto [it, inserted]{blockman.m_block_index.try_emplace(header.GetHash(), header)};
        BOOST_REQUIRE(inserted);
        it->second.phashBlock = &it->first;
        it->second.pprev = m_node.chainman->ActiveChain().Genesis();
    }
    BOOST_CHECK(Check({&header, 1}, context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), 1);
    WITH_LOCK(cs_main, blockman.LookupBlockIndex(header.GetHash())->nStatus |= BLOCK_FAILED_VALID);
    BOOST_CHECK(Check({&header, 1}, context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), 2); // A failure flag alone proves no PoW.
    {
        LOCK(cs_main);
        auto* entry{blockman.LookupBlockIndex(header.GetHash())};
        entry->nStatus &= ~BLOCK_FAILED_VALID;
        entry->RaiseValidity(BLOCK_VALID_TREE);
        // Even a map key claiming the original identity cannot authorize a
        // different stored header. Exercise each of the six exact comparisons.
    }
    for (int field{0}; field < 6; ++field) {
        {
            LOCK(cs_main);
            auto* entry{blockman.LookupBlockIndex(header.GetHash())};
            entry->nVersion = header.nVersion;
            entry->pprev = m_node.chainman->ActiveChain().Genesis();
            entry->hashMerkleRoot = header.hashMerkleRoot;
            entry->nTime = header.nTime;
            entry->nBits = header.nBits;
            entry->nNonce = header.nNonce;
            switch (field) {
            case 0: ++entry->nVersion; break;
            case 1: entry->pprev = nullptr; break;
            case 2: entry->hashMerkleRoot.SetNull(); break;
            case 3: ++entry->nTime; break;
            case 4: entry->nBits = 0; break;
            case 5: ++entry->nNonce; break;
            }
        }
        const auto before{context.HashesComputed()};
        BOOST_CHECK(Check({&header, 1}, context));
        BOOST_CHECK_EQUAL(context.HashesComputed(), before + 1);
    }
    WITH_LOCK(cs_main, blockman.m_block_index.erase(header.GetHash()));
}

BOOST_AUTO_TEST_CASE(failed_entries_remain_rejected)
{
    const auto header{Header(Params().GenesisBlock(), 5)};
    Accept(header);
    auto* index{WITH_LOCK(cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(header.GetHash()))};
    BlockValidationState invalidated;
    BOOST_REQUIRE(m_node.chainman->ActiveChainstate().InvalidateBlock(invalidated, index));
    PoWHashContext context;
    BOOST_CHECK(Check({&header, 1}, context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), 0); // The previously verified header PoW is unchanged.
    BlockValidationState state;
    BOOST_CHECK(!m_node.chainman->ProcessNewBlockHeaders({{header}}, true, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "duplicate-invalid");
}

BOOST_AUTO_TEST_CASE(side_branches_and_concurrent_processing)
{
    const auto first{Header(Params().GenesisBlock(), 6)};
    const auto fork{Header(Params().GenesisBlock(), 7)};
    const auto fork_tip{Header(fork, 8)};
    Accept(first);
    Accept(fork);
    Accept(fork_tip);
    const auto new_tip{Header(fork_tip, 9)};
    const std::array known_fork{fork, fork_tip};
    const auto size{WITH_LOCK(cs_main, return m_node.chainman->m_blockman.m_block_index.size())};
    std::array<uint64_t, 4> hashes{};
    std::array<bool, 4> accepted{};
    std::vector<std::thread> workers;
    for (size_t i{0}; i < hashes.size(); ++i) {
        workers.emplace_back([&, i] {
            // The network message lock serializes its shared context. Other
            // concurrent callers must each own a context, as documented.
            PoWHashContext context;
            bool valid{true};
            for (int replay{0}; replay < 4; ++replay) {
                // Each wire batch must be contiguous, even when replaying
                // several independently validated branches concurrently.
                valid &= Check({&first, 1}, context);
                valid &= Check(known_fork, context);
            }
            BlockValidationState state;
            accepted[i] = valid && m_node.chainman->ProcessNewBlockHeaders({{new_tip}}, true, state);
            hashes[i] = context.HashesComputed();
        });
    }
    for (auto& worker : workers) worker.join();
    for (size_t i{0}; i < hashes.size(); ++i) {
        BOOST_CHECK(accepted[i]);
        BOOST_CHECK_EQUAL(hashes[i], 0);
    }
    LOCK(cs_main);
    BOOST_CHECK_EQUAL(m_node.chainman->m_blockman.m_block_index.size(), size + 1);
    BOOST_CHECK(m_node.chainman->m_best_header->GetBlockHash() == new_tip.GetHash());
    BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Height(), 0); // Headers alone cannot activate blocks.
}

BOOST_AUTO_TEST_SUITE_END()

namespace {
template<int Activation>
struct HeaderAdmissionSetup : TestingSetup {
    HeaderAdmissionSetup()
        : TestingSetup{ChainType::REGTEST, {.extra_args={Activation < 0 ? "-mercahashv2height=-1" :
              Activation == 0 ? "-mercahashv2height=0" : "-mercahashv2height=3", "-debug=net"}}}
    {
        // A small work threshold and message limit exercise the real network
        // presync/redownload path without mining thousands of memory-hard PoWs.
        m_node.peerman.reset();
        m_node.chainman.reset();
        ChainstateManager::Options options{
            .chainparams = Params(), .datadir = m_args.GetDataDirNet(),
            .minimum_chain_work = arith_uint256{18},
            .notifications = *m_node.notifications, .signals = m_node.validation_signals.get(),
        };
        node::BlockManager::Options block_options{
            .chainparams = Params(), .blocks_dir = m_args.GetBlocksDirPath(),
            .notifications = *m_node.notifications,
            .block_tree_db_params = {.path = m_args.GetDataDirNet() / "blocks" / "index",
                                    .cache_bytes = 1 << 20, .memory_only = true},
        };
        m_node.chainman = std::make_unique<ChainstateManager>(*m_node.shutdown_signal, options, block_options);
        LoadVerifyActivateChainstate();
        m_node.peerman = PeerManager::make(*m_node.connman, *m_node.addrman, m_node.banman.get(),
            *m_node.chainman, *m_node.mempool, *m_node.warnings,
            {.deterministic_rng = true, .max_headers_result = 4});
        static_cast<ConnmanTestMsg&>(*m_node.connman).SetMsgProc(m_node.peerman.get());
    }

    ~HeaderAdmissionSetup()
    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        for (const auto& peer : peers) m_node.peerman->FinalizeNode(*peer);
    }

    std::vector<std::unique_ptr<CNode>> peers;
    PoWHashContext miner;

    CNode& AddPeer()
    {
        peers.push_back(std::make_unique<CNode>(peers.size(), nullptr, CAddress{}, 0, 0,
            CAddress{}, "", ConnectionType::INBOUND, false, 0));
        auto& peer{*peers.back()};
        static_cast<ConnmanTestMsg&>(*m_node.connman).Handshake(peer, true,
            ServiceFlags(NODE_NETWORK | NODE_WITNESS), ServiceFlags(NODE_NETWORK | NODE_WITNESS),
            PROTOCOL_VERSION, true);
        return peer;
    }

    void Solve(CBlockHeader& header, int height)
    {
        for (unsigned attempts{0}; attempts < 64; ++attempts, ++header.nNonce) {
            if (CheckProofOfWork(header, Params().GetConsensus(), miner, height)) return;
        }
        BOOST_FAIL("Regtest header did not solve within 64 hashes");
    }

    void Send(CNode& peer, std::span<const CBlockHeader> headers, std::optional<unsigned> checks)
    {
        std::vector<CBlock> blocks;
        for (const auto& header : headers) blocks.emplace_back(header);
        std::vector<std::string> logs;
        DebugLogHelper log{"headers proof-of-work:", [&](const std::string* line) {
            if (line) logs.push_back(*line);
            return false;
        }};
        auto& connman{static_cast<ConnmanTestMsg&>(*m_node.connman)};
        connman.FlushSendBuffer(peer);
        peer.fPauseSend = false;
        BOOST_REQUIRE(connman.ReceiveMsgFrom(peer, NetMsg::Make(NetMsgType::HEADERS, TX_WITH_WITNESS(blocks))));
        connman.ProcessMessagesOnce(peer);
        if (checks) {
            BOOST_REQUIRE_EQUAL(logs.size(), 1);
            BOOST_CHECK(logs[0].find(strprintf("checked=%u total=%u", *checks, headers.size())) != std::string::npos);
        } else {
            BOOST_CHECK(logs.empty());
        }
    }

    void CheckAdmission()
    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        auto& first{AddPeer()};
        auto& second{AddPeer()};
        std::vector<CBlockHeader> headers;
        CBlockHeader parent{Params().GenesisBlock()};
        for (int height{1}; height <= 8; ++height) {
            CBlockHeader header{parent};
            header.nVersion = 0x20000000;
            header.hashPrevBlock = parent.GetHash();
            header.nTime += 150;
            header.nNonce = 0;
            Solve(header, height);
            headers.push_back(header);
            parent = header;
        }
        auto unknown{headers[0]};
        unknown.hashPrevBlock = uint256::ONE;
        Solve(unknown, 1);
        for (int replay{0}; replay < 3; ++replay) Send(first, {&unknown, 1}, std::nullopt);

        const std::span batch{headers};
        Send(first, batch.first(4), 4);
        BOOST_CHECK(WITH_LOCK(cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(headers[3].GetHash())) == nullptr);
        for (int replay{0}; replay < 3; ++replay) Send(first, batch.first(4), 0);
        Send(first, {}, std::nullopt); // Clearing sync state must not clear PoW knowledge.
        Send(first, batch.first(4), 0);
        Send(second, batch.first(4), 0); // Shared across peers and batch boundaries.
        Send(second, batch.first(2), 0);
        Send(second, batch.first(3), 0);

        CNodeStateStats stats;
        BOOST_REQUIRE(m_node.peerman->GetNodeStateStats(first.GetId(), stats));
        BOOST_CHECK_EQUAL(stats.presync_height, 4);
        Send(first, batch.subspan(4), 4); // Reach the work threshold.
        Send(first, batch.first(4), 0); // Legitimate redownload uses the same PoW.
        Send(first, batch.subspan(4), 0);
        {
            LOCK(cs_main);
            BOOST_REQUIRE(m_node.chainman->m_best_header);
            BOOST_CHECK(m_node.chainman->m_best_header->GetBlockHash() == headers.back().GetHash());
            BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Height(), 0);
        }
        // Cached proof never authorizes altered fields or bypasses contextual checks.
        auto invalid{headers[0]};
        invalid.nBits = 0;
        Send(second, {&invalid, 1}, 1);
        auto& third{AddPeer()};
        Send(third, {&invalid, 1}, 1); // Invalid PoW is never cached as success.
        const std::array broken{headers[0], headers[2]};
        Send(third, broken, std::nullopt); // Cached entries cannot bypass linkage.
        BOOST_CHECK(WITH_LOCK(cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(invalid.GetHash())) == nullptr);
        auto too_old{headers.back()};
        too_old.hashPrevBlock = headers.back().GetHash();
        too_old.nTime = Params().GenesisBlock().nTime;
        too_old.nNonce = 0;
        Solve(too_old, 9);
        Send(first, {&too_old, 1}, 1);
        Send(first, {&too_old, 1}, 0);
        BOOST_CHECK(WITH_LOCK(cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(too_old.GetHash())) == nullptr);
    }
};
} // namespace

BOOST_FIXTURE_TEST_CASE(header_admission_v1, HeaderAdmissionSetup<-1>) { CheckAdmission(); }
BOOST_FIXTURE_TEST_CASE(header_admission_v2_genesis, HeaderAdmissionSetup<0>) { CheckAdmission(); }
BOOST_FIXTURE_TEST_CASE(header_admission_v2_transition, HeaderAdmissionSetup<3>) { CheckAdmission(); }
