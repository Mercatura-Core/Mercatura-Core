// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <boost/test/unit_test.hpp>
#include <chainparams.h>
#include <consensus/validation.h>
#include <headerssync.h>
#include <pow.h>
#include <test/util/setup_common.h>
#include <validation.h>
#include <array>

namespace {
struct ActivationSetup : TestingSetup {
    ActivationSetup() : TestingSetup{ChainType::REGTEST, {.extra_args={"-mercahashv2height=3"}}} {}
    PoWHashContext miner;
    CBlockHeader Header(const CBlockHeader& parent, int height, uint32_t tag, bool wrong = false)
    {
        CBlockHeader h;
        h.nVersion = 0x20000000;
        h.hashPrevBlock = parent.GetHash();
        h.hashMerkleRoot = ArithToUint256(arith_uint256{tag});
        h.nTime = parent.nTime + 150;
        h.nBits = parent.nBits;
        const auto& params{m_node.chainman->GetConsensus()};
        const auto selected{GetPoWVersion(params, height)};
        const auto other{selected == mercahash::Version::V1 ? mercahash::Version::V2 : mercahash::Version::V1};
        for (;;) {
            const bool right{CheckProofOfWorkImpl(miner.GetHash(h, selected), h.nBits, params)};
            if (right != wrong && CheckProofOfWorkImpl(miner.GetHash(h, other), h.nBits, params) == wrong) return h;
            ++h.nNonce;
        }
    }
    void Accept(const CBlockHeader& h)
    {
        BlockValidationState state;
        BOOST_REQUIRE_MESSAGE(m_node.chainman->ProcessNewBlockHeaders({{h}}, true, state), state.ToString());
    }
    bool Check(std::span<const CBlockHeader> h, PoWHashContext& context, std::optional<int64_t> height = {})
    {
        return HasValidProofOfWork(h, m_node.chainman->GetConsensus(), context, m_node.chainman->m_blockman, height);
    }
};
}
BOOST_FIXTURE_TEST_SUITE(mercahash_activation_tests, ActivationSetup)
BOOST_AUTO_TEST_CASE(boundary_wrong_versions_forks_and_cache)
{
    const auto& params{m_node.chainman->GetConsensus()};
    BOOST_REQUIRE(params.nMercaHashV2Height == 3);
    BOOST_CHECK(GetPoWVersion(params, 2) == mercahash::Version::V1);
    BOOST_CHECK(GetPoWVersion(params, 3) == mercahash::Version::V2);
    BOOST_CHECK(GetPoWVersion(params, 4) == mercahash::Version::V2);
    BOOST_CHECK_THROW(GetPoWVersion(params, -1), std::invalid_argument);
    ArgsManager public_args;
    for (const auto type : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET}) {
        BOOST_CHECK(!CreateChainParams(public_args, type)->GetConsensus().nMercaHashV2Height);
    }
    const auto first{Header(Params().GenesisBlock(), 1, 1)};
    const auto before{Header(first, 2, 2)};
    const auto at{Header(before, 3, 3)};
    const auto after{Header(at, 4, 4)};
    const std::array chain{first, before, at, after};
    PoWHashContext context;
    BOOST_REQUIRE(Check(chain, context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), 4);
    for (const auto& h : chain) Accept(h);
    const auto count{context.HashesComputed()};
    BOOST_REQUIRE(Check(chain, context));
    BOOST_REQUIRE(Check({&at, 1}, context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), count);
    BOOST_CHECK(!Check(chain, context, 2)); // Contradicts known parent height.
    BOOST_CHECK(!CheckProofOfWork(at, params, context)); // Unknown height fails closed.
    const auto wrong{Header(before, 3, 5, true)};
    BOOST_CHECK(!Check({&wrong, 1}, context));
    BlockValidationState state;
    BOOST_CHECK(!m_node.chainman->ProcessNewBlockHeaders({{wrong}}, true, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "high-hash");
    const auto fork_before{Header(first, 2, 6)};
    const auto fork_at{Header(fork_before, 3, 7)};
    Accept(fork_before);
    Accept(fork_at);
    BOOST_CHECK(Check({&fork_before, 1}, context));
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(fork_at.GetHash())->nHeight), 3);
    // Target-derived work and difficulty do not depend on hash cost or version.
    BOOST_CHECK(GetBlockProof(before) == GetBlockProof(at));
    BOOST_CHECK(GetBlockProof(at) == GetBlockProof(after));
}
BOOST_AUTO_TEST_CASE(unconnected_malformed_and_presync_continuations)
{
    const auto first{Header(Params().GenesisBlock(), 1, 10)};
    const auto second{Header(first, 2, 11)};
    const auto third{Header(second, 3, 12)};
    const auto fourth{Header(third, 4, 13)};
    PoWHashContext context;
    BOOST_CHECK(!Check({&third, 1}, context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), 0);
    const std::array malformed{first, third};
    BOOST_CHECK(!Check(malformed, context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), 0);
    const auto& start{WITH_LOCK(cs_main, return *m_node.chainman->ActiveChain().Genesis())};
    HeadersSyncState sync{0, m_node.chainman->GetConsensus(), {.commitment_period=1, .redownload_buffer_size=2}, start, arith_uint256{10}};
    const std::array low{first, second};
    BOOST_CHECK(sync.NextHeaderHeight(first) == 1);
    BOOST_REQUIRE(Check(low, context, sync.NextHeaderHeight(first)));
    BOOST_REQUIRE(sync.ProcessNextHeaders(low, true).success);
    BOOST_CHECK(sync.NextHeaderHeight(third) == 3);
    BOOST_CHECK(!sync.NextHeaderHeight(first));
    const std::array high{third, fourth};
    BOOST_REQUIRE(Check(high, context, sync.NextHeaderHeight(third)));
    BOOST_REQUIRE(sync.ProcessNextHeaders(high, true).success);
    BOOST_CHECK(sync.GetState() == HeadersSyncState::State::REDOWNLOAD);
    BOOST_CHECK(sync.NextHeaderHeight(first) == 1);
    BOOST_REQUIRE(Check(low, context, sync.NextHeaderHeight(first)));
    BOOST_REQUIRE(sync.ProcessNextHeaders(low, true).success);
    BOOST_CHECK(sync.NextHeaderHeight(third) == 3);
    BOOST_REQUIRE(Check(high, context, sync.NextHeaderHeight(third)));
    BOOST_REQUIRE(sync.ProcessNextHeaders(high, true).success);
}
BOOST_AUTO_TEST_CASE(context_switches_retain_outputs_and_release_v1_capacity)
{
    const auto h{Header(Params().GenesisBlock(), 1, 20)};
    PoWHashContext context;
    const auto v1{context.GetHash(h)};
    BOOST_CHECK_EQUAL(context.ScratchpadSize(), mercahash::V1_SCRATCHPAD_BYTES);
    const auto v2{context.GetHash(h, mercahash::Version::V2)};
    BOOST_CHECK_EQUAL(context.ScratchpadSize(), mercahash::V2_SCRATCHPAD_BYTES);
    BOOST_CHECK(v1 != v2);
    BOOST_CHECK(context.GetHash(h, mercahash::Version::V2) == v2);
    BOOST_CHECK(context.GetHash(h) == v1);
    BOOST_CHECK_EQUAL(context.ScratchpadSize(), mercahash::V1_SCRATCHPAD_BYTES);
}
BOOST_AUTO_TEST_SUITE_END()
