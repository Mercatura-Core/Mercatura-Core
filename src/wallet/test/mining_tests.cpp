// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <wallet/mining.h>

#include <chainparams.h>
#include <interfaces/handler.h>
#include <interfaces/mining.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <mining/cpu_miner.h>
#include <mining/solo.h>
#include <node/kernel_notifications.h>
#include <test/util/setup_common.h>
#include <wallet/context.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <thread>

using namespace wallet;

BOOST_FIXTURE_TEST_SUITE(wallet_mining_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(pq_destination_persistence_and_locked_reuse)
{
    WalletContext context;
    context.args = &m_args;
    auto wallet{TestCreateWallet(CreateMockableWalletDatabase(), context, WALLET_FLAG_DESCRIPTORS)};
    auto api{interfaces::MakeWallet(context, wallet)};
    const auto first{GetMiningDestination(*api)};
    BOOST_REQUIRE(first);
    BOOST_CHECK(std::holds_alternative<WitnessV2MercaturaPQ>(*first));
    BOOST_CHECK(api->isSpendable(*first));
    auto database{DuplicateMockDatabase(wallet->GetDatabase())};
    // Reload public address-book and ownership records, without an in-memory
    // mining cache, and require reuse of exactly the same destination.
    auto reloaded{TestLoadWallet(std::move(database), context)};
    auto reloaded_api{interfaces::MakeWallet(context, reloaded)};
    const auto persisted{GetMiningDestination(*reloaded_api)};
    BOOST_REQUIRE(persisted);
    BOOST_CHECK(*persisted == *first);
    SecureString passphrase;
    passphrase.assign("mining-test-passphrase");
    BOOST_REQUIRE(wallet->EncryptWallet(passphrase));
    BOOST_CHECK(api->isLocked());
    const auto locked{GetMiningDestination(*api)};
    BOOST_REQUIRE(locked);
    BOOST_CHECK(*locked == *first);
    auto other{TestCreateWallet(CreateMockableWalletDatabase(), context, WALLET_FLAG_DESCRIPTORS)};
    auto other_api{interfaces::MakeWallet(context, other)};
    BOOST_REQUIRE(other->EncryptWallet(passphrase));
    BOOST_CHECK(!GetMiningDestination(*other_api));
    BOOST_REQUIRE(other->Unlock(passphrase));
    const auto other_dest{GetMiningDestination(*other_api)};
    BOOST_REQUIRE(other_dest);
    BOOST_CHECK(*other_dest != *first);
    BOOST_CHECK(!api->isSpendable(*other_dest));
}
BOOST_AUTO_TEST_SUITE_END()

struct SoloTestingSetup : TestingSetup {
    SoloTestingSetup() : TestingSetup{ChainType::REGTEST} { m_node.notifications->setChainstateLoaded(true); }
};
BOOST_FIXTURE_TEST_SUITE(native_solo_tests, SoloTestingSetup)

BOOST_AUTO_TEST_CASE(native_controller_submits_valid_pq_regtest_block)
{
    WalletContext context;
    context.args = &m_args;
    auto wallet{TestCreateWallet(CreateMockableWalletDatabase(), context, WALLET_FLAG_DESCRIPTORS)};
    auto api{interfaces::MakeWallet(context, wallet)};
    const auto destination{GetMiningDestination(*api)};
    BOOST_REQUIRE(destination);
    const CScript script{GetScriptForDestination(*destination)};
    auto observer{interfaces::MakeMining(m_node, false)};
    const auto parent{observer->getTip()};
    BOOST_REQUIRE(parent);
    mining::SoloWorkProvider provider{interfaces::MakeMining(m_node, false), script, EncodeDestination(*destination), true};
    const auto first{provider.GetJob()};
    const auto second{provider.GetJob()};
    BOOST_REQUIRE(first && second);
    BOOST_CHECK(first->header.hashMerkleRoot != second->header.hashMerkleRoot); // extranonce refresh
    BOOST_CHECK(first->coinbase->vout[0].scriptPubKey == script);
    BOOST_CHECK(provider.IsCurrent(*second));
    // No dummy/classical payout script can enter the solo provider.
    BOOST_CHECK_THROW((mining::SoloWorkProvider{interfaces::MakeMining(m_node, false), CScript{} << OP_TRUE, "", true}), std::runtime_error);
    mining::MiningController controller;
    auto unload{api->handleUnload([&] { controller.RequestStop(); })};
    BOOST_REQUIRE(controller.Start([&] {
        return std::make_unique<mining::SoloWorkProvider>(interfaces::MakeMining(m_node, false), script, EncodeDestination(*destination), true);
    }, 1));
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{120}};
    while (controller.GetStats().accepted == 0 && controller.GetStats().state != mining::MiningState::ERROR && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    controller.RequestStop();
    controller.Wait();
    BOOST_REQUIRE_MESSAGE(controller.GetStats().accepted > 0, controller.GetStats().status);
    const auto tip{observer->getTip()};
    BOOST_REQUIRE(tip);
    BOOST_CHECK(tip->height > parent->height);
    BOOST_CHECK(tip->hash != parent->hash);
    BOOST_CHECK(!provider.IsCurrent(*second));
    BOOST_CHECK_EQUAL(controller.GetStats().active_workers, 0U);
    // Core unload notifications must cancel the next session, without any
    // wallet private material in a job or worker.
    BOOST_REQUIRE(controller.Start([&] {
        return std::make_unique<mining::SoloWorkProvider>(interfaces::MakeMining(m_node, false), script, EncodeDestination(*destination), true);
    }, 1));
    wallet->NotifyUnload();
    controller.Wait();
    BOOST_CHECK(!controller.GetStats().busy);
}
BOOST_AUTO_TEST_SUITE_END()
