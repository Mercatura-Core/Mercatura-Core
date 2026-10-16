// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <pow.h>
#include <test/util/setup_common.h>
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
    const std::array known{first, fork, fork_tip};
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
            for (int replay{0}; replay < 4; ++replay) valid &= Check(known, context);
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
