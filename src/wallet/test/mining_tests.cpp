// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <chainparams.h>
#include <consensus/mercatura_controller.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <crypto/mercahash.h>
#include <hash.h>
#include <interfaces/chain.h>
#include <interfaces/handler.h>
#include <interfaces/mining.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <mining/cpu_miner.h>
#include <mining/solo.h>
#include <node/blockstorage.h>
#include <node/kernel_notifications.h>
#include <pow.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <validation.h>
#include <validationinterface.h>
#include <wallet/coincontrol.h>
#include <wallet/context.h>
#include <wallet/mining.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

using namespace wallet;

BOOST_FIXTURE_TEST_SUITE(mining_tests_wallet, BasicTestingSetup)

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
    BOOST_REQUIRE(api->setAddressBook(*first, "My mining rewards", AddressPurpose::RECEIVE));
    const auto renamed{GetMiningDestination(*api)};
    BOOST_REQUIRE(renamed);
    BOOST_CHECK(*renamed == *first);
    BOOST_CHECK_EQUAL(api->getAddresses().size(), 1U);
    BOOST_REQUIRE(api->setAddressBook(*first, MINING_REWARD_LABEL, AddressPurpose::RECEIVE));
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
    explicit SoloTestingSetup(TestOpts opts = {}) : TestingSetup{ChainType::REGTEST, std::move(opts)} { m_node.notifications->setChainstateLoaded(true); }
};
BOOST_FIXTURE_TEST_SUITE(mining_tests_solo, SoloTestingSetup)

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
    while (controller.GetStats().accepted == 0 && controller.GetStats().state != mining::MiningState::FAILED && std::chrono::steady_clock::now() < deadline) {
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

BOOST_AUTO_TEST_CASE(gpu_controller_accepts_native_pq_block_with_cpu_reference_test_double)
{
    // Exercise the real GPU worker, candidate double-check, Solo submission and
    // block validation without a GPU. This is explicitly not CUDA execution.
    struct ReferenceBackend final : mining::GpuBackend {
        std::vector<unsigned char> scratch{std::vector<unsigned char>(mercahash::SCRATCHPAD_BYTES)};
        unsigned int Contexts() const override { return 2; }
        uint64_t MemoryBytes() const override { return 2 * mercahash::SCRATCHPAD_BYTES; }
        std::vector<mining::GpuHash> Hash(std::span<const mining::GpuHeader> headers,
                                        const std::function<bool()>& cancelled) override
        {
            std::vector<mining::GpuHash> hashes(headers.size());
            for (size_t slot{0}; slot < headers.size(); ++slot) {
                if (cancelled()) return {};
                mercahash::HashV1(headers[slot], scratch, hashes[slot]);
            }
            return cancelled() ? std::vector<mining::GpuHash>{} : hashes;
        }
    };
    WalletContext context;
    context.args = &m_args;
    auto wallet{TestCreateWallet(CreateMockableWalletDatabase(), context, WALLET_FLAG_DESCRIPTORS)};
    auto api{interfaces::MakeWallet(context, wallet)};
    const auto destination{GetMiningDestination(*api)};
    BOOST_REQUIRE(destination);
    const auto script{GetScriptForDestination(*destination)};
    mining::GpuDevice device{mining::GpuVendor::NVIDIA_CUDA};
    device.id = "cuda:cpu-reference-test-double";
    device.name = "CPU reference test double (no GPU execution)";
    device.supported = true;
    mining::MiningController controller{
        {}, [] { return mining::CalculateWorkerLimits(4, 2ULL << 30); }, {},
        [device] { return mining::GpuDiscovery{{device}, {}}; },
        [](const auto&, unsigned) { return std::make_unique<ReferenceBackend>(); }};
    mining::GpuConfig hardware;
    hardware.cpu = false;
    hardware.gpu = true;
    hardware.yield_ms = 0;
    BOOST_REQUIRE(controller.Start([&] {
        return std::make_unique<mining::SoloWorkProvider>(interfaces::MakeMining(m_node, false), script,
                                                         EncodeDestination(*destination), true);
    }, 0, hardware));
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{120}};
    while (controller.GetStats().accepted == 0 && controller.GetStats().busy && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    controller.RequestStop();
    controller.Wait();
    const auto stats{controller.GetStats()};
    BOOST_REQUIRE_MESSAGE(stats.accepted > 0, stats.status);
    BOOST_CHECK_EQUAL(stats.cpu_hashes, 0U);
    BOOST_REQUIRE_EQUAL(stats.devices.size(), 1U);
    BOOST_CHECK(stats.devices[0].hashes > 0);
    BOOST_CHECK_EQUAL(stats.devices[0].memory_bytes, 0U);
    BOOST_CHECK_EQUAL(stats.active_workers, 0U);
    auto& chainman{*m_node.chainman};
    LOCK(chainman.GetMutex());
    CBlock block;
    BOOST_REQUIRE(chainman.m_blockman.ReadBlock(block, *chainman.ActiveChain()[1]));
    BOOST_CHECK(block.vtx[0]->vout[0].scriptPubKey == script);
    const auto command{Consensus::GetMcaEmissionCommand(nullptr, 1)};
    BOOST_REQUIRE(command);
    BOOST_CHECK_EQUAL(block.vtx[0]->GetValueOut(), command->subsidy);
    PoWHashContext pow;
    BOOST_CHECK(CheckProofOfWork(block, Params().GetConsensus(), pow));
}

BOOST_AUTO_TEST_CASE(solo_template_binding_wrong_work_hash_and_duplicate)
{
    WalletContext context;
    context.args = &m_args;
    auto wallet{TestCreateWallet(CreateMockableWalletDatabase(), context, WALLET_FLAG_DESCRIPTORS)};
    auto api{interfaces::MakeWallet(context, wallet)};
    const auto destination{GetMiningDestination(*api)};
    BOOST_REQUIRE(destination);
    const auto script{GetScriptForDestination(*destination)};
    mining::SoloWorkProvider provider{interfaces::MakeMining(m_node, false), script, "", true};
    auto old{provider.GetJob()};
    auto job{provider.GetJob()};
    BOOST_REQUIRE(old && job);
    BOOST_CHECK(!*old->valid);
    BOOST_CHECK(!provider.IsCurrent(*old));
    BOOST_CHECK(provider.Submit(*old, old->header).stale);
    auto forged{*job};
    forged.target = ~arith_uint256{};
    BOOST_CHECK(!provider.IsCurrent(forged));
    forged = *job;
    CMutableTransaction coinbase{*job->coinbase};
    coinbase.vout[0].scriptPubKey = CScript{} << OP_TRUE;
    forged.coinbase = MakeTransactionRef(coinbase);
    BOOST_CHECK(!provider.IsCurrent(forged));
    auto solution{job->header};
    ++solution.nTime;
    BOOST_CHECK_THROW(provider.Submit(*job, solution), std::runtime_error);
    PoWHashContext hash;
    solution = job->header;
    // A small SHA256d identifier is not evidence of valid MercaHash work.
    while (!(UintToArith256(solution.GetHash()) <= job->target &&
             UintToArith256(hash.GetHash(solution)) > job->target) &&
           solution.nNonce < 1000)
        ++solution.nNonce;
    BOOST_REQUIRE(solution.nNonce < 1000);
    const auto rejected{provider.Submit(*job, solution)};
    BOOST_CHECK(!rejected.processed && !rejected.active_chain);
    while (UintToArith256(hash.GetHash(solution)) > job->target && solution.nNonce < 1000)
        ++solution.nNonce;
    BOOST_REQUIRE(solution.nNonce < 1000);
    BOOST_CHECK(provider.Submit(*job, solution).active_chain);
    BOOST_CHECK(provider.Submit(*job, solution).stale);
    provider.Interrupt();
    BOOST_CHECK(provider.IsCancelled());
    BOOST_CHECK(!provider.GetJob());
}

BOOST_AUTO_TEST_CASE(native_rewards_maturity_fees_and_pq_spend)
{
    WalletContext context;
    context.args = &m_args;
    context.chain = m_node.chain.get();
    auto wallet{TestCreateWallet(CreateMockableWalletDatabase(), context, WALLET_FLAG_DESCRIPTORS)};
    auto api{interfaces::MakeWallet(context, wallet)};
    auto receiver{TestCreateWallet(CreateMockableWalletDatabase(), context, WALLET_FLAG_DESCRIPTORS)};
    auto receive_api{interfaces::MakeWallet(context, receiver)};
    const auto destination{GetMiningDestination(*api)};
    const auto receive_destination{GetMiningDestination(*receive_api)};
    BOOST_REQUIRE(destination && receive_destination);
    const auto script{GetScriptForDestination(*destination)};
    uint64_t accepted{0};
    struct BoundedSolo : mining::WorkProvider {
        mining::SoloWorkProvider solo;
        std::atomic<unsigned int> remaining;
        BoundedSolo(std::unique_ptr<interfaces::Mining> node, CScript payout, unsigned int count)
            : solo{std::move(node), std::move(payout), "", true}, remaining{count} {}
        std::optional<mining::MiningJob> GetJob() override { return IsCancelled() ? std::nullopt : solo.GetJob(); }
        bool IsCurrent(const mining::MiningJob& job) override { return !IsCancelled() && solo.IsCurrent(job); }
        mining::SubmissionResult Submit(const mining::MiningJob& job, const CBlockHeader& header) override
        {
            auto result{solo.Submit(job, header)};
            if (result.active_chain) --remaining;
            return result;
        }
        void Interrupt() override { solo.Interrupt(); }
        bool IsCancelled() const override { return remaining == 0 || solo.IsCancelled(); }
    };
    auto mine = [&](unsigned int count, unsigned int workers) {
        // Bound the isolated integration test's host memory and CPU budget.
        mining::MiningController controller{{}, [] { return mining::CalculateWorkerLimits(4, 2ULL << 30); }};
        BOOST_REQUIRE(controller.Start([&] { return std::make_unique<BoundedSolo>(interfaces::MakeMining(m_node, false), script, count); }, workers));
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{180}};
        while (controller.GetStats().busy && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        controller.RequestStop();
        controller.Wait();
        const auto stats{controller.GetStats()};
        BOOST_REQUIRE_MESSAGE(stats.accepted == count, stats.status);
        BOOST_CHECK_EQUAL(stats.rejected, 0U);
        BOOST_CHECK_EQUAL(stats.active_workers, 0U);
        BOOST_CHECK_EQUAL(stats.hashes_per_second, 0);
        accepted += stats.accepted;
        m_node.validation_signals->SyncWithValidationInterfaceQueue();
    };
    mine(1, 1);
    auto& chainman{*m_node.chainman};
    CBlock first;
    {
        LOCK(chainman.GetMutex());
        BOOST_REQUIRE(chainman.m_blockman.ReadBlock(first, *chainman.ActiveChain()[1]));
    }
    const auto first_reward{first.vtx[0]->vout[0].nValue};
    BOOST_CHECK_EQUAL(api->getBalances().balance, 0);
    BOOST_CHECK_EQUAL(api->getBalances().immature_balance, first_reward);
    CCoinControl control;
    control.Select(COutPoint{first.vtx[0]->GetHash(), 0});
    control.m_allow_other_inputs = false;
    control.m_feerate = CFeeRate{1};
    control.fOverrideFeeRate = true;
    const CRecipient recipient{*receive_destination, COIN, false};
    CCoinControl automatic;
    automatic.m_feerate = CFeeRate{1};
    automatic.fOverrideFeeRate = true;
    BOOST_CHECK(!CreateTransaction(*wallet, {recipient}, std::nullopt, automatic, true));
    // Explicit coin control can pre-sign an immature output; ordinary wallet
    // selection excludes it, and normal node admission must enforce maturity.
    const auto premature{CreateTransaction(*wallet, {recipient}, std::nullopt, control, true)};
    BOOST_REQUIRE(premature);
    std::string premature_error;
    BOOST_CHECK(!m_node.chain->broadcastTransaction(premature->tx, COIN, node::TxBroadcast::MEMPOOL_NO_BROADCAST, premature_error));
    BOOST_CHECK(premature_error.find("bad-txns-premature-spend-of-coinbase") != std::string::npos);
    mine(99, 2);
    BOOST_CHECK_EQUAL(api->getBalances().balance, 0); // Depth 100 remains immature.
    mine(1, 1);
    BOOST_CHECK_EQUAL(api->getBalances().balance, first_reward);
    auto spend{CreateTransaction(*wallet, {recipient}, std::nullopt, control, true)};
    const std::string spend_error{spend ? std::string{} : util::ErrorString(spend).original};
    BOOST_REQUIRE_MESSAGE(static_cast<bool>(spend), spend_error);
    BOOST_REQUIRE_EQUAL(spend->tx->vin.size(), 1U);
    BOOST_CHECK(spend->tx->vin[0].prevout == COutPoint(first.vtx[0]->GetHash(), 0));
    BOOST_CHECK(!spend->tx->vin[0].scriptWitness.IsNull());
    std::string error;
    BOOST_REQUIRE_MESSAGE(m_node.chain->broadcastTransaction(spend->tx, COIN, node::TxBroadcast::MEMPOOL_NO_BROADCAST, error), error);
    mine(1, 2);
    BOOST_CHECK_EQUAL(receive_api->getBalances().balance, COIN);
    UniValue proof{UniValue::VOBJ}, blocks{UniValue::VARR};
    {
        LOCK(chainman.GetMutex());
        BOOST_REQUIRE_EQUAL(chainman.ActiveHeight(), 102);
        for (int height{1}; height <= chainman.ActiveHeight(); ++height) {
            const auto* index{chainman.ActiveChain()[height]};
            CBlock block;
            BOOST_REQUIRE(chainman.m_blockman.ReadBlock(block, *index));
            BOOST_CHECK(block.hashMerkleRoot == BlockMerkleRoot(block));
            BOOST_CHECK(block.GetHash() == index->GetBlockHash());
            PoWHashContext pow;
            BOOST_CHECK(CheckProofOfWork(block, Params().GetConsensus(), pow));
            const auto& parent_state{index->pprev->m_mca_emission_state};
            const auto command{Consensus::GetMcaEmissionCommand(parent_state ? &*parent_state : nullptr, height)};
            BOOST_REQUIRE(command);
            const CAmount fees{height == 102 ? spend->fee : 0};
            BOOST_CHECK_EQUAL(block.vtx[0]->GetValueOut(), command->subsidy + fees);
            BOOST_CHECK(block.vtx[0]->vout[0].scriptPubKey == script);
            for (size_t output{1}; output < block.vtx[0]->vout.size(); ++output)
                BOOST_CHECK_EQUAL(block.vtx[0]->vout[output].nValue, 0); // No fee/diversion output.
            if (height == 102) {
                BOOST_REQUIRE_EQUAL(block.vtx.size(), 2U);
                BOOST_CHECK(block.vtx[1]->GetHash() == spend->tx->GetHash());
                const auto commitment{GetWitnessCommitmentIndex(block)};
                BOOST_REQUIRE(commitment >= 0);
                const auto& reserved{block.vtx[0]->vin[0].scriptWitness.stack};
                BOOST_REQUIRE_EQUAL(reserved.size(), 1U);
                BOOST_REQUIRE_EQUAL(reserved[0].size(), 32U);
                uint256 witness_hash;
                CHash256{}.Write(BlockWitnessMerkleRoot(block)).Write(reserved[0]).Finalize(witness_hash);
                const auto& output{block.vtx[0]->vout[commitment].scriptPubKey};
                BOOST_CHECK(std::equal(witness_hash.begin(), witness_hash.end(), output.begin() + 6));
            }
            DataStream serialized;
            serialized << TX_WITH_WITNESS(block);
            blocks.push_back(HexStr(serialized));
        }
    }
    proof.pushKV("blocks", blocks);
    proof.pushKV("accepted", accepted);
    proof.pushKV("destination", EncodeDestination(*destination));
    proof.pushKV("first_coinbase", first.vtx[0]->GetHash().GetHex());
    proof.pushKV("spend", spend->tx->GetHash().GetHex());
    proof.pushKV("fee_base_units", spend->fee);
    // Test-only proof export lets a separate RPC node validate the exact native
    // controller-produced chain. It contains public blocks/addresses, no keys.
    if (const char* path{std::getenv("MERCATURA_SOLO_QUALIFICATION_OUTPUT")}) {
        std::ofstream file{path};
        file << proof.write(2) << '\n';
        BOOST_REQUIRE(file.good());
    }
}

BOOST_AUTO_TEST_CASE(public_readiness_cooldown_and_interrupt)
{
    struct State {
        bool ibd{true}, missing_tip{false}, unavailable{false}, cooldown{false}, block{false};
        std::atomic<bool> entered{false}, interrupted{false};
    } state;
    struct ReadinessMining : interfaces::Mining {
        std::unique_ptr<interfaces::Mining> node;
        State& state;
        ReadinessMining(std::unique_ptr<interfaces::Mining> value, State& flags) : node{std::move(value)}, state{flags} {}
        bool isTestChain() override { return node->isTestChain(); }
        bool isInitialBlockDownload() override { return state.ibd; }
        std::optional<interfaces::BlockRef> getTip() override { return state.missing_tip ? std::nullopt : node->getTip(); }
        std::optional<interfaces::BlockRef> waitTipChanged(uint256 hash, MillisecondsDouble timeout) override { return node->waitTipChanged(hash, timeout); }
        std::unique_ptr<interfaces::BlockTemplate> createNewBlock(const node::BlockCreateOptions& options, bool cooldown) override
        {
            state.cooldown = cooldown;
            if (state.block) {
                state.entered = true;
                while (!state.interrupted)
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                return {};
            }
            // This regtest fixture controls public readiness independently of
            // its old genesis timestamp. Record the public cooldown request.
            return state.unavailable ? nullptr : node->createNewBlock(options, false);
        }
        void interrupt() override
        {
            state.interrupted = true;
            node->interrupt();
        }
        bool checkBlock(const CBlock& block, const node::BlockCheckOptions& options, std::string& reason, std::string& debug) override { return node->checkBlock(block, options, reason, debug); }
        node::NodeContext* context() override { return node->context(); }
    };
    const CScript script{GetScriptForDestination(WitnessV2MercaturaPQ{uint256{1}})};
    mining::SoloWorkProvider provider{std::make_unique<ReadinessMining>(interfaces::MakeMining(m_node, false), state), script, "", false};
    BOOST_CHECK(!provider.GetJob());
    state.ibd = false;
    state.missing_tip = true;
    BOOST_CHECK(!provider.GetJob());
    state.missing_tip = false;
    state.unavailable = true;
    BOOST_CHECK(!provider.GetJob());
    state.unavailable = false;
    auto job{provider.GetJob()};
    BOOST_REQUIRE(job);
    BOOST_CHECK(state.cooldown);
    mining::SoloWorkProvider competitor{interfaces::MakeMining(m_node, false), script, "", true};
    const auto competing{competitor.GetJob()};
    BOOST_REQUIRE(competing);
    auto header{competing->header};
    PoWHashContext hash;
    while (UintToArith256(hash.GetHash(header)) > competing->target && header.nNonce < 1000)
        ++header.nNonce;
    BOOST_REQUIRE(header.nNonce < 1000);
    BlockValidationState validation;
    BOOST_REQUIRE(m_node.chainman->ProcessNewBlockHeaders(std::span{&header, 1}, true, validation));
    BOOST_REQUIRE(m_node.chainman->BlocksAheadOfTip());
    BOOST_CHECK(!provider.IsCurrent(*job));
    BOOST_CHECK(!provider.GetJob()); // Later headers pause work even after IBD.
    BOOST_REQUIRE(competitor.Submit(*competing, header).active_chain);
    job = provider.GetJob();
    BOOST_REQUIRE(job);
    state.ibd = true;
    BOOST_CHECK(!provider.IsCurrent(*job));
    state.ibd = false;
    state.block = true;
    mining::MiningController controller;
    BOOST_REQUIRE(controller.Start([&] {
        return std::make_unique<mining::SoloWorkProvider>(std::make_unique<ReadinessMining>(interfaces::MakeMining(m_node, false), state), script, "", false);
    },
                                   1));
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{10}};
    while (!state.entered && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    const bool entered{state.entered};
    controller.RequestStop();
    controller.Wait();
    BOOST_CHECK(entered && state.interrupted);
    BOOST_CHECK(!controller.GetStats().busy);
    BOOST_CHECK_EQUAL(controller.GetStats().hashes, 0U);
}

BOOST_AUTO_TEST_CASE(real_competing_tip_reorg_and_new_template)
{
    auto observer{interfaces::MakeMining(m_node, false)};
    const CScript script{GetScriptForDestination(WitnessV2MercaturaPQ{uint256{2}})};
    mining::SoloWorkProvider provider{interfaces::MakeMining(m_node, false), script, "", true};
    mining::SoloWorkProvider competitor{interfaces::MakeMining(m_node, false), script, "", true};
    const auto old{provider.GetJob()};
    auto competing{competitor.GetJob()};
    BOOST_REQUIRE(old && competing);
    PoWHashContext hash;
    auto solve = [&](const mining::MiningJob& job) {
        auto header{job.header};
        while (UintToArith256(hash.GetHash(header)) > job.target && header.nNonce < 1000)
            ++header.nNonce;
        BOOST_REQUIRE(header.nNonce < 1000);
        return header;
    };
    BOOST_REQUIRE(competitor.Submit(*competing, solve(*competing)).active_chain);
    BOOST_CHECK(!provider.IsCurrent(*old));
    BOOST_CHECK(provider.Submit(*old, solve(*old)).stale);
    auto next{provider.GetJob()};
    BOOST_REQUIRE(next);
    BOOST_CHECK(next->header.hashPrevBlock == observer->getTip()->hash);
    auto& chainman{*m_node.chainman};
    auto* tip{WITH_LOCK(chainman.GetMutex(), return chainman.ActiveChain().Tip())};
    BlockValidationState state;
    BOOST_REQUIRE(chainman.ActiveChainstate().InvalidateBlock(state, tip));
    BOOST_CHECK_EQUAL(observer->getTip()->height, 0);
    BOOST_CHECK(!provider.IsCurrent(*next));
    auto replacement{provider.GetJob()};
    BOOST_REQUIRE(replacement);
    BOOST_CHECK(replacement->header.hashPrevBlock == old->header.hashPrevBlock);
    BOOST_CHECK(replacement->header.hashMerkleRoot != old->header.hashMerkleRoot);
    BOOST_REQUIRE(provider.Submit(*replacement, solve(*replacement)).active_chain);
    BOOST_CHECK_EQUAL(observer->getTip()->height, 1);
}
BOOST_AUTO_TEST_SUITE_END()


struct V2SoloTestingSetup : SoloTestingSetup {
    V2SoloTestingSetup() : SoloTestingSetup{TestOpts{.extra_args={"-mercahashv2height=0"}}} {}
};
BOOST_FIXTURE_TEST_SUITE(mining_tests_v2, V2SoloTestingSetup)
BOOST_AUTO_TEST_CASE(gpu_worker_and_combined_mode_submit_genuine_v2_blocks_with_cpu_reference_device)
{
    // The device is a CPU reference test double. Jobs, hashing, independent
    // candidate rehashing, rewards and Core consensus validation are genuine.
    struct Reference : mining::GpuBackend {
        std::vector<unsigned char> scratch{std::vector<unsigned char>(mercahash::V2_SCRATCHPAD_BYTES)};
        unsigned int Contexts() const override { return 1; }
        mercahash::Version PoWVersion() const override { return mercahash::Version::V2; }
        uint64_t MemoryBytes() const override { return scratch.size(); }
        std::vector<mining::GpuHash> Hash(std::span<const mining::GpuHeader> headers, const std::function<bool()>& cancelled) override {
            std::vector<mining::GpuHash> hashes(headers.size());
            for (size_t i{0}; i < headers.size(); ++i) {
                if (cancelled()) return {};
                mercahash::HashV2(headers[i], scratch, hashes[i]);
            }
            return cancelled() ? std::vector<mining::GpuHash>{} : hashes;
        }
    };
    WalletContext context;
    context.args = &m_args;
    auto wallet{TestCreateWallet(CreateMockableWalletDatabase(), context, WALLET_FLAG_DESCRIPTORS)};
    auto api{interfaces::MakeWallet(context, wallet)};
    const auto destination{GetMiningDestination(*api)};
    BOOST_REQUIRE(destination);
    const auto script{GetScriptForDestination(*destination)};
    for (const bool cpu : {false, true}) {
        mining::MiningController controller{{}, {}, {}, [] {
            mining::GpuDevice d; d.id = "cpu-reference-v2"; d.name = "CPU reference device; no GPU execution"; d.supported = true;
            return mining::GpuDiscovery{{d}, {}};
        }, [](const auto&, unsigned) { return std::make_unique<Reference>(); }};
        mining::GpuConfig hardware; hardware.cpu = cpu; hardware.gpu = true; hardware.yield_ms = 0;
        BOOST_REQUIRE(controller.Start([&] { return std::make_unique<mining::SoloWorkProvider>(
            interfaces::MakeMining(m_node, false), script, EncodeDestination(*destination), true); }, 1, hardware));
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{30}};
        while (std::chrono::steady_clock::now() < deadline) {
            const auto stats{controller.GetStats()};
            if (!stats.busy || (stats.accepted >= 2 && !stats.devices.empty() && stats.devices[0].hashes > 0 && (!cpu || stats.cpu_hashes > 0))) break;
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        controller.RequestStop(); controller.Wait();
        const auto stats{controller.GetStats()};
        BOOST_REQUIRE_MESSAGE(stats.accepted >= 2, stats.status);
        BOOST_REQUIRE_EQUAL(stats.devices.size(), 1U);
        BOOST_CHECK(stats.devices[0].hashes > 0 && !stats.devices[0].failed);
        BOOST_CHECK(cpu ? stats.cpu_hashes > 0 : stats.cpu_hashes == 0);
        BOOST_CHECK_EQUAL(stats.active_workers, 0U);
        BOOST_CHECK_EQUAL(stats.rejected, 0U);
        CBlock block;
        int height;
        {
            LOCK(cs_main);
            const auto* tip{m_node.chainman->ActiveChain().Tip()};
            height = tip->nHeight;
            BOOST_REQUIRE(m_node.chainman->m_blockman.ReadBlock(block, *tip));
        }
        BOOST_CHECK(block.vtx[0]->vout[0].scriptPubKey == script);
        PoWHashContext pow;
        BOOST_CHECK(CheckProofOfWork(block, Params().GetConsensus(), pow, height));
    }
}
BOOST_AUTO_TEST_SUITE_END()
