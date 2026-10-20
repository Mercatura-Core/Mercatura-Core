// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <addresstype.h>
#include <arith_uint256.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <interfaces/handler.h>
#include <interfaces/mining.h>
#include <mining/cpu_miner.h>
#include <mining/solo.h>
#include <node/kernel_notifications.h>
#include <pow.h>
#include <script/solver.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>
#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>

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
