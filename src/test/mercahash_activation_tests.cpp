// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <boost/test/unit_test.hpp>
#include <chainparams.h>
#include <addresstype.h>
#include <consensus/validation.h>
#include <headerssync.h>
#include <interfaces/mining.h>
#include <interfaces/handler.h>
#include <mining/cpu_miner.h>
#include <mining/solo.h>
#include <node/kernel_notifications.h>
#include <script/solver.h>
#include <chrono>
#include <limits>
#include <thread>
#include <string>
#include <pow.h>
#include <test/util/setup_common.h>
#include <util/string.h>
#include <validation.h>
#include <array>

namespace {
struct ActivationSetup : TestingSetup {
    ActivationSetup() : TestingSetup{ChainType::REGTEST, {.extra_args={"-mercahashv2height=3"}}} {
        m_node.notifications->setChainstateLoaded(true);
    }
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
    BOOST_CHECK(CreateChainParams(public_args, ChainType::MAIN)->GetConsensus().nMercaHashV2Height == 0);
    for (const auto type : {ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET}) {
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
    CBlock cached_wrong{wrong};
    cached_wrong.fChecked = true;
    cached_wrong.m_checked_pow_version = static_cast<uint8_t>(mercahash::Version::V1);
    BlockValidationState cache_state;
    BOOST_CHECK(!CheckBlock(cached_wrong, cache_state, params, true, true, PoWCheckStatus::UNCHECKED, &context, 3));
    BOOST_CHECK_EQUAL(cache_state.GetRejectReason(), "high-hash");
    public_args.ForceSetArg("-mercahashv2height", "3");
    BOOST_CHECK_THROW(CreateChainParams(public_args, ChainType::TESTNET), std::runtime_error);
    BOOST_CHECK_THROW(CreateChainParams(public_args, ChainType::MAIN), std::runtime_error);
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
    const CBlockIndex* start{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Genesis())};
    BOOST_REQUIRE(start);
    HeadersSyncState sync{0, m_node.chainman->GetConsensus(), {.commitment_period=1, .redownload_buffer_size=2}, *start, arith_uint256{10}};
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
BOOST_AUTO_TEST_CASE(native_solo_jobs_and_controller_cross_activation_and_reorg)
{
    const auto script{GetScriptForDestination(WitnessV2MercaturaPQ{uint256::ONE})};
    auto observer{interfaces::MakeMining(m_node, false)};
    mining::SoloWorkProvider provider{interfaces::MakeMining(m_node, false), script, "", true};
    std::optional<mining::MiningJob> previous;
    for (int height{1}; height <= 4; ++height) {
        auto job{provider.GetJob()};
        BOOST_REQUIRE(job);
        BOOST_CHECK_EQUAL(job->height, height);
        BOOST_CHECK(job->pow_version == (height < 3 ? mercahash::Version::V1 : mercahash::Version::V2));
        if (previous) BOOST_CHECK(!provider.IsCurrent(*previous));
        auto header{job->header};
        while (UintToArith256(miner.GetHash(header, job->pow_version)) > job->target) ++header.nNonce;
        BOOST_REQUIRE(provider.Submit(*job, header).active_chain);
        previous = job;
    }
    auto& chainman{*m_node.chainman};
    auto* at_two{WITH_LOCK(cs_main, return chainman.ActiveChain()[2])};
    BlockValidationState state;
    BOOST_REQUIRE(chainman.ActiveChainstate().InvalidateBlock(state, at_two));
    BOOST_CHECK_EQUAL(observer->getTip()->height, 1);
    auto replacement{provider.GetJob()};
    BOOST_REQUIRE(replacement);
    BOOST_CHECK_EQUAL(replacement->height, 2);
    BOOST_CHECK(replacement->pow_version == mercahash::Version::V1);
    mining::MiningController controller;
    BOOST_REQUIRE(controller.Start([&] {
        return std::make_unique<mining::SoloWorkProvider>(interfaces::MakeMining(m_node, false), script, "", true);
    }, 1));
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{60}};
    while (observer->getTip()->height < 4 && controller.GetStats().state != mining::MiningState::FAILED && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    controller.RequestStop(); controller.Wait();
    BOOST_CHECK_MESSAGE(observer->getTip()->height >= 4, controller.GetStats().status);
    BOOST_CHECK(controller.GetStats().accepted >= 3);
    BOOST_CHECK_EQUAL(controller.GetStats().active_workers, 0U);
}
BOOST_AUTO_TEST_CASE(regtest_option_bounds_and_disabled_representation)
{
    ArgsManager args;
    BOOST_CHECK(!CreateChainParams(args, ChainType::REGTEST)->GetConsensus().nMercaHashV2Height);
    args.ForceSetArg("-mercahashv2height", "-1");
    BOOST_CHECK(!CreateChainParams(args, ChainType::REGTEST)->GetConsensus().nMercaHashV2Height);
    for (const int height : {0, 1, 3, 100}) {
        args.ForceSetArg("-mercahashv2height", util::ToString(height));
        BOOST_CHECK(CreateChainParams(args, ChainType::REGTEST)->GetConsensus().nMercaHashV2Height == height);
    }
    for (const char* invalid : {"-2", "2147483647", "2147483648", "invalid"}) {
        args.ForceSetArg("-mercahashv2height", invalid);
        BOOST_CHECK_THROW(CreateChainParams(args, ChainType::REGTEST), std::runtime_error);
    }
    for (const char* height : {"-1", "0", "3"}) {
        args.ForceSetArg("-mercahashv2height", height);
        for (const auto chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET}) {
            BOOST_CHECK_THROW(CreateChainParams(args, chain), std::runtime_error);
        }
    }
}
BOOST_AUTO_TEST_SUITE_END()

namespace {
struct MainnetGenesisSetup : TestingSetup {
    MainnetGenesisSetup() : TestingSetup{ChainType::MAIN} {
        m_node.notifications->setChainstateLoaded(true);
    }
};
}

BOOST_FIXTURE_TEST_SUITE(mercahash_network_schedule_tests, MainnetGenesisSetup)
BOOST_AUTO_TEST_CASE(network_schedules_and_genesis_fixtures)
{
    const auto& main{m_node.chainman->GetParams()};
    BOOST_REQUIRE(main.GetConsensus().nMercaHashV2Height == 0);
    BOOST_CHECK(main.HasProvisionalGenesis());
    PoWHashContext context;
    for (const int64_t height : {0, 1, 2, 100, std::numeric_limits<int>::max()}) {
        BOOST_CHECK(GetPoWVersion(main.GetConsensus(), height) == mercahash::Version::V2);
    }
    BOOST_CHECK_EQUAL(context.GetHash(main.GenesisBlock(), mercahash::Version::V2).GetHex(),
                      "36a5c0d8c3ba98b9349cde92ef20fd98ceaf5e91b38ecd55afbca2d0b87d588e");
    BOOST_CHECK(CheckProofOfWork(main.GenesisBlock(), main.GetConsensus(), context, 0));
    BOOST_CHECK_EQUAL(context.ScratchpadSize(), mercahash::V2_SCRATCHPAD_BYTES);
    // Missing ancestry may fail closed, but must never authorize a V1 fallback.
    const auto count{context.HashesComputed()};
    BOOST_CHECK(!CheckProofOfWork(main.GenesisBlock(), main.GetConsensus(), context));
    BOOST_CHECK_EQUAL(context.HashesComputed(), count);
    BOOST_CHECK_THROW(GetPoWVersion(main.GetConsensus(), -1), std::invalid_argument);

    ArgsManager args;
    for (const auto chain : {ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET}) {
        const auto params{CreateChainParams(args, chain)};
        BOOST_CHECK(!params->HasProvisionalGenesis());
        BOOST_CHECK(!params->GetConsensus().nMercaHashV2Height);
        BOOST_CHECK(GetPoWVersion(params->GetConsensus(), 0) == mercahash::Version::V1);
        BOOST_CHECK(GetPoWVersion(params->GetConsensus(), std::numeric_limits<int>::max()) == mercahash::Version::V1);
    }
    const auto legacy{CreateChainParams(args, ChainType::REGTEST)};
    BOOST_CHECK_EQUAL(legacy->GenesisBlock().GetHash().GetHex(),
                      "8e2308efb3a16b126e69444329cc0ed81bea0596e99db1032ccd750e7028f685");
    args.ForceSetArg("-mercahashv2height", "0");
    const auto v2{CreateChainParams(args, ChainType::REGTEST)};
    BOOST_CHECK_EQUAL(v2->GenesisBlock().GetHash().GetHex(),
                      "f7086cc3933445280f0e03607dc8b1199a010111f161f11a0c86e0a6f76a9ffe");
    BOOST_CHECK_EQUAL(context.GetHash(v2->GenesisBlock(), mercahash::Version::V2).GetHex(),
                      "65ae7d866f23a2dafe3fc406a024f9342673fe04ac7083bce28abc3b90460d02");
    BOOST_CHECK(CheckProofOfWork(v2->GenesisBlock(), v2->GetConsensus(), context, 0));
    BOOST_CHECK(!CheckProofOfWork(legacy->GenesisBlock(), v2->GetConsensus(), context, 0));
    args.ForceSetArg("-mercahashv2height", "3");
    const auto transition{CreateChainParams(args, ChainType::REGTEST)};
    BOOST_CHECK(transition->GenesisBlock().GetHash() == legacy->GenesisBlock().GetHash());
    BOOST_CHECK(GetPoWVersion(transition->GetConsensus(), 0) == mercahash::Version::V1);
    BOOST_CHECK(GetPoWVersion(transition->GetConsensus(), 2) == mercahash::Version::V1);
    BOOST_CHECK(GetPoWVersion(transition->GetConsensus(), 3) == mercahash::Version::V2);
}
BOOST_AUTO_TEST_CASE(mainnet_first_native_job_requires_v2)
{
    const auto script{GetScriptForDestination(WitnessV2MercaturaPQ{uint256::ONE})};
    // Disable peer-readiness/cooldown only in this isolated fixture. The chain
    // and all template, mining, and validation rules are actual MAIN parameters.
    mining::SoloWorkProvider provider{interfaces::MakeMining(m_node, false), script, "", true};
    auto observer{interfaces::MakeMining(m_node, false)};
    auto job{provider.GetJob()};
    BOOST_REQUIRE(job);
    BOOST_CHECK_EQUAL(job->height, 1);
    BOOST_REQUIRE(job->pow_version == mercahash::Version::V2);
    PoWHashContext context;
    auto wrong{job->header};
    while (UintToArith256(context.GetHash(wrong, mercahash::Version::V1)) > job->target ||
           UintToArith256(context.GetHash(wrong, mercahash::Version::V2)) <= job->target) ++wrong.nNonce;
    BlockValidationState state;
    BOOST_CHECK(!m_node.chainman->ProcessNewBlockHeaders({{wrong}}, true, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "high-hash");
    CBlock cached_wrong{wrong};
    cached_wrong.fChecked = true;
    cached_wrong.m_checked_pow_version = static_cast<uint8_t>(mercahash::Version::V1);
    BlockValidationState cached_state;
    BOOST_CHECK(!CheckBlock(cached_wrong, cached_state, m_node.chainman->GetConsensus(), true, true,
                           PoWCheckStatus::UNCHECKED, &context, 1));
    BOOST_CHECK_EQUAL(cached_state.GetRejectReason(), "high-hash");
    mining::MiningController controller;
    BOOST_REQUIRE(controller.Start([&] {
        return std::make_unique<mining::SoloWorkProvider>(interfaces::MakeMining(m_node, false), script, "", true);
    }, 1));
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{60}};
    while (observer->getTip()->height < 2 && controller.GetStats().state != mining::MiningState::FAILED && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    controller.RequestStop(); controller.Wait();
    BOOST_CHECK_MESSAGE(observer->getTip()->height >= 2, controller.GetStats().status);
    BOOST_CHECK(controller.GetStats().accepted >= 2);
    BOOST_CHECK_EQUAL(controller.GetStats().active_workers, 0U);
    auto next{provider.GetJob()};
    BOOST_REQUIRE(next);
    BOOST_CHECK(next->pow_version == mercahash::Version::V2);
}
BOOST_AUTO_TEST_SUITE_END()
