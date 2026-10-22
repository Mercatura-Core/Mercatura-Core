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
#include <thread>
#include <string>
#include <pow.h>
#include <test/util/setup_common.h>
#include <util/string.h>
#include <validation.h>
#include <array>
#include <limits>
#include <stdexcept>

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
    for (const int h : {1, 2}) BOOST_CHECK(GetPoWVersion(params, h) == mercahash::Version::V1);
    for (const int h : {3, 4, 103}) BOOST_CHECK(GetPoWVersion(params, h) == mercahash::Version::V2);
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
BOOST_AUTO_TEST_CASE(regtest_option_bounds_and_disabled_representation)
{
    ArgsManager args;
    BOOST_CHECK(!CreateChainParams(args, ChainType::REGTEST)->GetConsensus().nMercaHashV2Height);
    args.ForceSetArg("-mercahashv2height", "-1");
    BOOST_CHECK(!CreateChainParams(args, ChainType::REGTEST)->GetConsensus().nMercaHashV2Height);
    for (const int height : {0, 1, 3, 20, 100}) {
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
BOOST_AUTO_TEST_CASE(network_defaults_and_v2_genesis_are_explicit)
{
    ArgsManager args;
    PoWHashContext context;
    for (const auto chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET}) {
        const auto network{CreateChainParams(args, chain)};
        BOOST_CHECK(!network->GetConsensus().nMercaHashV2Height);
        for (const int64_t height : {0, 1, 100, std::numeric_limits<int>::max()})
            BOOST_CHECK(GetPoWVersion(network->GetConsensus(), height) == mercahash::Version::V1);
    }
    const auto historical{CreateChainParams(args, ChainType::REGTEST)};
    BOOST_CHECK_EQUAL(historical->GenesisBlock().GetHash().GetHex(), "8e2308efb3a16b126e69444329cc0ed81bea0596e99db1032ccd750e7028f685");
    args.ForceSetArg("-mercahashv2height", "0");
    const auto v2{CreateChainParams(args, ChainType::REGTEST)};
    BOOST_CHECK_EQUAL(v2->GenesisBlock().GetHash().GetHex(), "f7086cc3933445280f0e03607dc8b1199a010111f161f11a0c86e0a6f76a9ffe");
    BOOST_CHECK_EQUAL(context.GetHash(v2->GenesisBlock(), mercahash::Version::V2).GetHex(), "65ae7d866f23a2dafe3fc406a024f9342673fe04ac7083bce28abc3b90460d02");
    BOOST_CHECK(CheckProofOfWork(v2->GenesisBlock(), v2->GetConsensus(), context, 0));
    BOOST_CHECK(!CheckProofOfWork(historical->GenesisBlock(), v2->GetConsensus(), context, 0));
    const auto hashes{context.HashesComputed()};
    BOOST_CHECK(!CheckProofOfWork(v2->GenesisBlock(), v2->GetConsensus(), context));
    BOOST_CHECK(!CheckProofOfWork(v2->GenesisBlock(), v2->GetConsensus(), context, -1));
    BOOST_CHECK(!CheckProofOfWork(v2->GenesisBlock(), v2->GetConsensus(), context, int64_t{std::numeric_limits<int>::max()} + 1));
    BOOST_CHECK_EQUAL(context.HashesComputed(), hashes);
    BOOST_CHECK_THROW(context.GetHash(v2->GenesisBlock(), static_cast<mercahash::Version>(3)), std::invalid_argument);
    args.ForceSetArg("-mercahashv2height", "3");
    const auto transition{CreateChainParams(args, ChainType::REGTEST)};
    BOOST_CHECK(transition->GenesisBlock().GetHash() == historical->GenesisBlock().GetHash());
}
BOOST_AUTO_TEST_CASE(harder_regtest_fixture_and_public_override_rejection)
{
    ArgsManager args;
    args.ForceSetArg("-regtestminingstress", "1");
    BOOST_CHECK_THROW(CreateChainParams(args, ChainType::REGTEST), std::runtime_error);
    args.ForceSetArg("-mercahashv2height", "20");
    BOOST_CHECK_THROW(CreateChainParams(args, ChainType::REGTEST), std::runtime_error);
    args.ForceSetArg("-mercahashv2height", "0");
    const auto chain{CreateChainParams(args, ChainType::REGTEST)};
    const auto& genesis{chain->GenesisBlock()};
    BOOST_CHECK_EQUAL(genesis.nBits, 0x1f0fffffU);
    BOOST_CHECK_EQUAL(genesis.GetHash().GetHex(), "9336d4e8d61fa82a08386748ac6c1020e200a3e9f0ce303253a1ea3322686802");
    PoWHashContext context;
    BOOST_CHECK_EQUAL(context.GetHash(genesis, mercahash::Version::V2).GetHex(), "00096c8fef58c1c0c6833cfd49df8523228bb6709e90eab76c9b70bf18d78093");
    BOOST_CHECK(CheckProofOfWork(genesis, chain->GetConsensus(), context, 0));
    CBlockIndex index{genesis};
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&index, &genesis, chain->GetConsensus()), genesis.nBits);
    ArgsManager public_args;
    for (const char* enabled : {"0", "1"}) {
        public_args.ForceSetArg("-regtestminingstress", enabled);
        for (const auto type : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET})
            BOOST_CHECK_THROW(CreateChainParams(public_args, type), std::runtime_error);
    }
}
BOOST_AUTO_TEST_SUITE_END()

namespace {
struct Transition20Setup : TestingSetup {
    Transition20Setup() : TestingSetup{ChainType::REGTEST, {.extra_args={"-mercahashv2height=20"}}} {
        m_node.notifications->setChainstateLoaded(true);
    }
};
}
BOOST_FIXTURE_TEST_SUITE(mercahash_activation_tests_transition, Transition20Setup)
BOOST_AUTO_TEST_CASE(live_worker_crosses_twenty_and_reorgs_back_without_restart)
{
    const auto script{GetScriptForDestination(WitnessV2MercaturaPQ{uint256::ONE})};
    std::atomic<int> ceiling{22};
    struct Gate : mining::WorkProvider {
        Gate(std::unique_ptr<mining::SoloWorkProvider> p, std::atomic<int>& c) : provider{std::move(p)}, ceiling{c} {}
        std::optional<mining::MiningJob> GetJob() override {
            auto job{provider->GetJob()};
            return job && job->height <= ceiling ? job : std::nullopt;
        }
        bool IsCurrent(const mining::MiningJob& job) override { return provider->IsCurrent(job); }
        mining::SubmissionResult Submit(const mining::MiningJob& job, const CBlockHeader& h) override { return provider->Submit(job, h); }
        void Interrupt() override { provider->Interrupt(); }
        std::unique_ptr<mining::SoloWorkProvider> provider;
        std::atomic<int>& ceiling;
    };
    mining::MiningController controller;
    BOOST_REQUIRE(controller.Start([&] {
        return std::make_unique<Gate>(std::make_unique<mining::SoloWorkProvider>(interfaces::MakeMining(m_node, false), script, "", true), ceiling);
    }, 1));
    auto height = [&] { return WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Height()); };
    auto wait_height = [&](int wanted, uint64_t accepted) {
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{30}};
        // Core publishes its tip before Solo Submit checks active-chain
        // acceptance and the controller records the result. Await that
        // acknowledgement before the test can invalidate the submitted block.
        const auto acknowledged = [&] { return height() == wanted && controller.GetStats().accepted >= accepted; };
        while (!acknowledged() && controller.GetStats().state != mining::MiningState::FAILED && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        const auto stats{controller.GetStats()};
        BOOST_TEST_MESSAGE("Transition height=" << height() << " wanted=" << wanted <<
            " accepted=" << stats.accepted << " expected=" << accepted << " status=" << stats.status);
        return acknowledged();
    };
    const bool first{wait_height(22, 22)};
    const auto before{controller.GetStats()};
    ceiling = 18;
    BlockValidationState state;
    auto* disconnect{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain()[19])};
    const bool invalidated{disconnect && m_node.chainman->ActiveChainstate().InvalidateBlock(state, disconnect)};
    ceiling = 19;
    const bool back{wait_height(19, 23)};
    const auto v1{controller.GetStats()};
    ceiling = 22;
    const bool forward{wait_height(22, 26)};
    controller.RequestStop(); controller.Wait();
    BOOST_CHECK_MESSAGE(first && invalidated && back && forward, controller.GetStats().status);
    BOOST_CHECK(before.pow_version == mercahash::Version::V2);
    BOOST_CHECK(v1.pow_version == mercahash::Version::V1);
    BOOST_CHECK(controller.GetStats().pow_version == mercahash::Version::V2);
    BOOST_CHECK_EQUAL(controller.GetStats().active_workers, 0U);
    BOOST_CHECK_EQUAL(controller.GetStats().rejected, 0U);
    BOOST_CHECK(controller.GetStats().accepted >= 26);
}
BOOST_AUTO_TEST_SUITE_END()
