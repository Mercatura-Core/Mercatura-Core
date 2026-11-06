// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <chainparams.h>
#include <coins.h>
#include <common/args.h>
#include <consensus/mercaweave.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <crypto/common.h>
#include <policy/policy.h>
#include <script/pqgroup_context.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

namespace {
struct MercaWeaveSetup : BasicTestingSetup {
    MercaWeaveSetup() : BasicTestingSetup(ChainType::REGTEST) {}
};

struct GroupFixture {
    mercaweave::B32 genesis{}, seed{}, nonce0{}, nonce1{};
    mercaweave::KeyPair key;
    CMutableTransaction tx;
    std::vector<CTxOut> spent;
    CCoinsView base;
    CCoinsViewCache coins{&base};

    GroupFixture() : key(seed)
    {
        const auto hash = Params().GetConsensus().hashGenesisBlock;
        std::copy(hash.begin(), hash.end(), genesis.begin());
        nonce1[0] = 1;
        tx.vin.resize(2);
        for (uint32_t i = 0; i < 2; ++i) {
            mercaweave::B32 id{};
            WriteLE32(id.data(), i + 1);
            tx.vin[i].prevout = COutPoint{Txid::FromUint256(uint256{std::span<const unsigned char>{id}}), 0};
            spent.emplace_back(100, mercaweave::Output(genesis, key.pk, i == 0 ? nonce0 : nonce1));
            coins.AddCoin(tx.vin[i].prevout, Coin{CTxOut{spent.back()}, 1, false}, false);
        }
        tx.vout.emplace_back(190, CScript{} << OP_TRUE);
        tx.vin[0].scriptWitness.stack = {mercaweave::Bytes(3309), mercaweave::Bytes(key.pk.begin(), key.pk.end()), mercaweave::Bytes(nonce0.begin(), nonce0.end())};
        tx.vin[1].scriptWitness.stack = {mercaweave::Bytes(nonce1.begin(), nonce1.end()), mercaweave::Bytes(4)};
        // Public deterministic unit-test key/randomness, never wallet entropy.
        mercaweave::Sign(tx, spent, genesis, {std::cref(key)}, [](auto& randomness) { randomness.fill(0); return true; });
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(mercaweave_activation_tests, MercaWeaveSetup)

BOOST_AUTO_TEST_CASE(public_schedules_unset_and_regtest_boundaries)
{
    for (const auto chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET, ChainType::REGTEST}) {
        ArgsManager args;
        const auto params = CreateChainParams(args, chain);
        BOOST_CHECK(!params->GetConsensus().nMercaWeaveHeight);
        BOOST_CHECK(!mercaweave::IsolatedExecution::ForConsensus(params->GetConsensus(), 1'000'000));
    }
    CChainParams::RegTestOptions options;
    options.mercaweave_height = 20;
    const auto delayed = CChainParams::RegTest(options);
    BOOST_CHECK(!Consensus::MercaWeaveActive(delayed->GetConsensus(), 19));
    BOOST_CHECK(Consensus::MercaWeaveActive(delayed->GetConsensus(), 20));
    BOOST_CHECK(Consensus::MercaWeaveActive(delayed->GetConsensus(), 21));
    options.mercaweave_height = 0;
    const auto immediate = CChainParams::RegTest(options);
    BOOST_CHECK(Consensus::MercaWeaveActive(immediate->GetConsensus(), 0));
    BOOST_CHECK(!Consensus::MercaWeaveActive(immediate->GetConsensus(), -1));

    ArgsManager forbidden;
    forbidden.ForceSetArg("-mercaweaveheight", "0");
    for (const auto chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET}) {
        BOOST_CHECK_THROW(CreateChainParams(forbidden, chain), std::runtime_error);
    }
}

BOOST_AUTO_TEST_CASE(unknown_witness_control_and_context_required)
{
    GroupFixture f;
    const auto flags = MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_MERCAWEAVE;
    ScriptError error;
    auto corrupted = f.tx;
    corrupted.vin[0].scriptWitness.stack[0][0] ^= 1;
    // This is the real preactivation anyone-can-spend control. It must not be
    // advertised as MercaWeave authorization by a wallet or coordinator.
    BOOST_CHECK(VerifyScript({}, f.spent[0].scriptPubKey, &corrupted.vin[0].scriptWitness,
                             MANDATORY_SCRIPT_VERIFY_FLAGS, BaseSignatureChecker{}, &error));
    BOOST_CHECK(!VerifyScript({}, f.spent[0].scriptPubKey, &f.tx.vin[0].scriptWitness,
                              flags, BaseSignatureChecker{}, &error));
    BOOST_CHECK(error == SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH);
    BOOST_CHECK(!VerifyScript({}, f.spent[0].scriptPubKey, &f.tx.vin[0].scriptWitness,
                              SCRIPT_VERIFY_MERCAWEAVE, BaseSignatureChecker{}, &error));
    LOCK(cs_main);
    ValidationCache cache{1 << 20, 1 << 20};
    PrecomputedTransactionData native;
    TxValidationState state;
    const auto tx = MakeTransactionRef(f.tx);
    std::vector<CScriptCheck> checks;
    BOOST_REQUIRE(CheckInputScripts(*tx, state, f.coins, flags, true, true, native,
                                   cache, &checks, Params().GetConsensus().hashGenesisBlock));
    BOOST_REQUIRE_EQUAL(checks.size(), 2U);
    BOOST_CHECK(!checks[1]()); // Followers can execute before anchors.
    BOOST_CHECK(!checks[0]());
    BOOST_CHECK_EQUAL(GetTransactionSigOpCost(*tx, f.coins, flags), 6);
    TxValidationState missing;
    BOOST_CHECK(!CheckInputScripts(*tx, missing, f.coins, flags, true, true, native,
                                   cache, nullptr, std::nullopt));
}

BOOST_AUTO_TEST_CASE(poisoned_legacy_cache_cannot_authorize_group)
{
    GroupFixture f;
    f.tx.vin[0].scriptWitness.stack[0][0] ^= 1;
    const auto tx = MakeTransactionRef(f.tx);
    const auto flags = MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_MERCAWEAVE;
    const auto genesis = Params().GetConsensus().hashGenesisBlock;
    LOCK(cs_main);
    ValidationCache cache{1 << 20, 1 << 20};
    auto hasher = cache.ScriptExecutionCacheHasher();
    hasher.Write(UCharCast(tx->GetWitnessHash().begin()), 32);
    hasher.Write(reinterpret_cast<const unsigned char*>(&flags), sizeof(flags));
    const unsigned char present{1};
    hasher.Write(&present, 1).Write(genesis.begin(), 32);
    uint256 cache_key;
    hasher.Finalize(cache_key.begin());
    cache.m_script_execution_cache.insert(cache_key);
    PrecomputedTransactionData native;
    TxValidationState state;
    BOOST_CHECK(!CheckInputScripts(*tx, state, f.coins, flags, true, true, native,
                                  cache, nullptr, genesis));
    BOOST_CHECK(cache.m_script_execution_cache.contains(cache_key, false));
}

BOOST_AUTO_TEST_CASE(malformed_anchors_followers_and_programs)
{
    GroupFixture f;
    const auto flags = MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_MERCAWEAVE;
    const auto genesis = Params().GetConsensus().hashGenesisBlock;
    LOCK(cs_main);
    ValidationCache cache{1 << 20, 1 << 20};
    const auto reject = [&](CMutableTransaction candidate) {
        PrecomputedTransactionData native;
        TxValidationState state;
        return !CheckInputScripts(CTransaction{candidate}, state, f.coins, flags, true,
                                 true, native, cache, nullptr, genesis);
    };
    auto wrong_nonce = f.tx;
    wrong_nonce.vin[1].scriptWitness.stack[0][0] ^= 1;
    BOOST_CHECK(reject(wrong_nonce));
    auto wrong_anchor = f.tx;
    WriteLE32(wrong_anchor.vin[1].scriptWitness.stack[1].data(), 1);
    BOOST_CHECK(reject(wrong_anchor));
    auto wrong_key = f.tx;
    wrong_key.vin[0].scriptWitness.stack[1][0] ^= 1;
    BOOST_CHECK(reject(wrong_key));
    auto wrong_signature = f.tx;
    wrong_signature.vin[0].scriptWitness.stack[0].resize(3308);
    BOOST_CHECK(reject(wrong_signature));
    BOOST_CHECK(GetTransactionSigOpCost(CTransaction{wrong_signature}, f.coins, flags) > MAX_BLOCK_SIGOPS_COST);
    for (const auto length : {2U, 20U, 31U, 33U, 40U}) {
        const auto script = CScript{} << OP_3 << std::vector<unsigned char>(length);
        ScriptError error;
        BOOST_CHECK(!VerifyScript({}, script, &f.tx.vin[0].scriptWitness, flags, BaseSignatureChecker{}, &error));
    }
}

BOOST_AUTO_TEST_CASE(offline_buffer_limit_is_not_a_consensus_rule)
{
    GroupFixture f;
    mercaweave::B32 id{}, program{};
    id[0] = 100;
    program[0] = 1;
    const COutPoint point{Txid::FromUint256(uint256{std::span<const unsigned char>{id}}), 0};
    f.tx.vin.emplace_back(point);
    // Existing permissive future witness semantics are deliberately retained.
    // This case exercises Script, not a claim that a current 4 MiB block fits it.
    f.spent.emplace_back(100, CScript{} << OP_4 << mercaweave::Bytes(program.begin(), program.end()));
    f.coins.AddCoin(point, Coin{CTxOut{f.spent.back()}, 1, false}, false);
    f.tx.vin.back().scriptWitness.stack.emplace_back(MAX_SIZE, 0x55);
    mercaweave::Sign(f.tx, f.spent, f.genesis, {std::cref(f.key)}, [](auto& randomness) { randomness.fill(0); return true; });
    BOOST_CHECK(GetSerializeSize(TX_WITH_WITNESS(f.tx)) > MAX_SIZE);
    LOCK(cs_main);
    ValidationCache cache{1 << 20, 1 << 20};
    PrecomputedTransactionData native;
    TxValidationState state;
    const auto tx = MakeTransactionRef(f.tx);
    const bool accepted = CheckInputScripts(*tx, state, f.coins, MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_MERCAWEAVE,
                                           true, true, native, cache, nullptr, Params().GetConsensus().hashGenesisBlock);
    BOOST_CHECK_MESSAGE(accepted, state.ToString());
    const auto wire = mercaweave::Wire(f.tx);
    BOOST_CHECK_THROW(mercaweave::DecodeTransaction(wire), mercaweave::Failure);
}

BOOST_AUTO_TEST_SUITE_END()
