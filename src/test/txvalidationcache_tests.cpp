// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/validation.h>
#include <key.h>
#include <random.h>
#include <script/sigcache.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <test/util/script.h>
#include <test/util/setup_common.h>
#include <txmempool.h>
#include <util/chaintype.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

struct Dersig100Setup : public TestChain100Setup {
    Dersig100Setup()
        : TestChain100Setup{ChainType::REGTEST, {.extra_args = {"-testactivationheight=dersig@102"}}} {}
};

bool CheckInputScripts(const CTransaction& tx, TxValidationState& state,
                       const CCoinsViewCache& inputs, script_verify_flags flags, bool cacheSigStore,
                       bool cacheFullScriptStore, PrecomputedTransactionData& txdata,
                       ValidationCache& validation_cache,
                       std::vector<CScriptCheck>* pvChecks,
                       std::optional<uint256> pq_genesis_hash = std::nullopt)
                       EXCLUSIVE_LOCKS_REQUIRED(cs_main);

BOOST_AUTO_TEST_SUITE(txvalidationcache_tests)

BOOST_FIXTURE_TEST_CASE(tx_mempool_block_doublespend, Dersig100Setup)
{
    // Make sure skipping validation of transactions that were
    // validated going into the memory pool does not allow
    // double-spends in blocks to pass validation when they should not.

    // Mercatura disables classical ECDSA/Schnorr ownership. Preserve this
    // validation-cache double-spend test with a signature-free P2WSH OP_TRUE
    // fixture instead of the inherited P2PK coinbase spend.
    const CScript scriptPubKey{P2WSH_OP_TRUE};

    {
        LOCK(cs_main);

        auto& coins{
            m_node.chainman->ActiveChainstate().CoinsTip()
        };

        const COutPoint funding_outpoint{
            m_coinbase_txns[0]->GetHash(),
            0
        };

        const auto coin{coins.GetCoin(funding_outpoint)};
        BOOST_REQUIRE(coin.has_value());

        Coin adapted_coin{*coin};
        adapted_coin.out.scriptPubKey = scriptPubKey;

        BOOST_REQUIRE(coins.SpendCoin(funding_outpoint));
        coins.AddCoin(
            funding_outpoint,
            std::move(adapted_coin),
            /*possible_overwrite=*/false);
    }

    const auto ToMemPool = [this](const CMutableTransaction& tx) {
        LOCK(cs_main);

        const MempoolAcceptResult result = m_node.chainman->ProcessTransaction(MakeTransactionRef(tx));
        return result.m_result_type == MempoolAcceptResult::ResultType::VALID;
    };

    // Create a double-spend of mature coinbase txn:
    std::vector<CMutableTransaction> spends;
    spends.resize(2);
    for (int i = 0; i < 2; i++)
    {
        spends[i].version = 1;
        spends[i].vin.resize(1);
        spends[i].vin[0].prevout.hash = m_coinbase_txns[0]->GetHash();
        spends[i].vin[0].prevout.n = 0;
        spends[i].vout.resize(1);
        // Keep the two conflicting spends distinct while spending the same
        // confirmed outpoint.
        spends[i].vout[0].nValue = (11 + i) * CENT;
        spends[i].vout[0].scriptPubKey = scriptPubKey;

        spends[i].vin[0].scriptWitness.stack.push_back(
            WITNESS_STACK_ELEM_OP_TRUE);
    }

    CBlock block;

    // Test 1: block with both of those transactions should be rejected.
    block = CreateAndProcessBlock(spends, scriptPubKey);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != block.GetHash());
    }

    // Test 2: ... and should be rejected if spend1 is in the memory pool
    BOOST_CHECK(ToMemPool(spends[0]));
    block = CreateAndProcessBlock(spends, scriptPubKey);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != block.GetHash());
    }
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 1U);
    WITH_LOCK(m_node.mempool->cs, m_node.mempool->removeRecursive(CTransaction{spends[0]}, MemPoolRemovalReason::CONFLICT));
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);

    // Test 3: ... and should be rejected if spend2 is in the memory pool
    BOOST_CHECK(ToMemPool(spends[1]));
    block = CreateAndProcessBlock(spends, scriptPubKey);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != block.GetHash());
    }
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 1U);
    WITH_LOCK(m_node.mempool->cs, m_node.mempool->removeRecursive(CTransaction{spends[1]}, MemPoolRemovalReason::CONFLICT));
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);

    // Final sanity test: first spend in *m_node.mempool, second in block, that's OK:
    std::vector<CMutableTransaction> oneSpend;
    oneSpend.push_back(spends[0]);
    BOOST_CHECK(ToMemPool(spends[1]));
    block = CreateAndProcessBlock(oneSpend, scriptPubKey);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() == block.GetHash());
    }
    // spends[1] should have been removed from the mempool when the
    // block with spends[0] is accepted:
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);
}

// Run CheckInputScripts (using CoinsTip()) on the given transaction, for all script
// flags.  Test that CheckInputScripts passes for all flags that don't overlap with
// the failing_flags argument, but otherwise fails.
// CHECKLOCKTIMEVERIFY and CHECKSEQUENCEVERIFY (and future NOP codes that may
// get reassigned) have an interaction with DISCOURAGE_UPGRADABLE_NOPS: if
// the script flags used contain DISCOURAGE_UPGRADABLE_NOPS but don't contain
// CHECKLOCKTIMEVERIFY (or CHECKSEQUENCEVERIFY), but the script does contain
// OP_CHECKLOCKTIMEVERIFY (or OP_CHECKSEQUENCEVERIFY), then script execution
// should fail.
// Capture this interaction with the upgraded_nop argument: set it when evaluating
// any script flag that is implemented as an upgraded NOP code.
static void ValidateCheckInputsForAllFlags(const CTransaction &tx, script_verify_flags failing_flags, bool add_to_cache, CCoinsViewCache& active_coins_tip, ValidationCache& validation_cache) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    PrecomputedTransactionData txdata;

    FastRandomContext insecure_rand(true);

    for (int count = 0; count < 10000; ++count) {
        TxValidationState state;

        // Randomly selects flag combinations
        script_verify_flags test_flags = script_verify_flags::from_int(insecure_rand.randrange(MAX_SCRIPT_VERIFY_FLAGS));

        // Filter out incompatible flag choices
        if ((test_flags & SCRIPT_VERIFY_CLEANSTACK)) {
            // CLEANSTACK requires P2SH and WITNESS, see VerifyScript() in
            // script/interpreter.cpp
            test_flags |= SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS;
        }
        if ((test_flags & SCRIPT_VERIFY_WITNESS)) {
            // WITNESS requires P2SH
            test_flags |= SCRIPT_VERIFY_P2SH;
        }
        bool ret = CheckInputScripts(tx, state, &active_coins_tip, test_flags, true, add_to_cache, txdata, validation_cache, nullptr);
        // CheckInputScripts should succeed iff test_flags doesn't intersect with
        // failing_flags
        bool expected_return_value = !(test_flags & failing_flags);
        BOOST_CHECK_EQUAL(ret, expected_return_value);

        // Test the caching
        if (ret && add_to_cache) {
            // Check that we get a cache hit if the tx was valid
            std::vector<CScriptCheck> scriptchecks;
            BOOST_CHECK(CheckInputScripts(tx, state, &active_coins_tip, test_flags, true, add_to_cache, txdata, validation_cache, &scriptchecks));
            BOOST_CHECK(scriptchecks.empty());
        } else {
            // Check that we get script executions to check, if the transaction
            // was invalid, or we didn't add to cache.
            std::vector<CScriptCheck> scriptchecks;
            BOOST_CHECK(CheckInputScripts(tx, state, &active_coins_tip, test_flags, true, add_to_cache, txdata, validation_cache, &scriptchecks));
            BOOST_CHECK_EQUAL(scriptchecks.size(), tx.vin.size());
        }
    }
}

BOOST_FIXTURE_TEST_CASE(checkinputs_test, Dersig100Setup)
{
    // Mercatura disables inherited ECDSA/Schnorr ownership authorization.
    // Preserve this test's validation-cache and script-flag coverage with
    // signature-free scripts instead of Bitcoin P2PK/P2PKH/P2WPKH fixtures.
    //
    // This script is consensus-valid but policy-invalid when
    // SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is enabled.
    const CScript cache_probe_script{
        CScript() << OP_NOP4 << OP_TRUE
    };

    // P2SH fixture: without SCRIPT_VERIFY_P2SH the hash check succeeds;
    // with P2SH enabled the OP_FALSE redeem script executes and fails.
    const CScript p2sh_redeem_script{
        CScript() << OP_FALSE
    };
    const CScript p2sh_scriptPubKey{
        GetScriptForDestination(ScriptHash(p2sh_redeem_script))
    };

    // Native signature-free witness fixture.
    const CScript witness_scriptPubKey{P2WSH_OP_TRUE};

    // Signature-free CLTV and CSV fixtures. The stack value is supplied by
    // scriptSig, then OP_TRUE leaves the script successful when the lock
    // condition itself succeeds.
    const CScript cltv_scriptPubKey{
        CScript() << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_TRUE
    };
    const CScript csv_scriptPubKey{
        CScript() << OP_CHECKSEQUENCEVERIFY << OP_DROP << OP_TRUE
    };

    // The inherited TestChain100Setup coinbase output is classical P2PK.
    // Adapt this one test-local UTXO to the signature-free cache probe script.
    {
        LOCK(cs_main);

        auto& coins{
            m_node.chainman->ActiveChainstate().CoinsTip()
        };

        const COutPoint funding_outpoint{
            m_coinbase_txns[0]->GetHash(),
            0
        };

        const auto coin{coins.GetCoin(funding_outpoint)};
        BOOST_REQUIRE(coin.has_value());

        Coin adapted_coin{*coin};
        adapted_coin.out.scriptPubKey = cache_probe_script;

        BOOST_REQUIRE(coins.SpendCoin(funding_outpoint));
        coins.AddCoin(
            funding_outpoint,
            std::move(adapted_coin),
            /*possible_overwrite=*/false);
    }

    // Create outputs used by the flag-specific and cache tests below.
    CMutableTransaction spend_tx;
    spend_tx.version = 1;
    spend_tx.vin.resize(1);
    spend_tx.vin[0].prevout.hash = m_coinbase_txns[0]->GetHash();
    spend_tx.vin[0].prevout.n = 0;

    // Two witness outputs are provided so the final multi-input cache test
    // can use two independently valid signature-free inputs.
    spend_tx.vout.resize(5);

    spend_tx.vout[0].nValue = 11 * CENT;
    spend_tx.vout[0].scriptPubKey = p2sh_scriptPubKey;

    spend_tx.vout[1].nValue = 11 * CENT;
    spend_tx.vout[1].scriptPubKey = witness_scriptPubKey;

    spend_tx.vout[2].nValue = 11 * CENT;
    spend_tx.vout[2].scriptPubKey = cltv_scriptPubKey;

    spend_tx.vout[3].nValue = 11 * CENT;
    spend_tx.vout[3].scriptPubKey = csv_scriptPubKey;

    spend_tx.vout[4].nValue = 11 * CENT;
    spend_tx.vout[4].scriptPubKey = witness_scriptPubKey;

    // Test that invalidity under a stricter policy-only flag does not
    // preclude validity under other flag combinations.
    {
        LOCK(cs_main);

        TxValidationState state;
        PrecomputedTransactionData ptd_spend_tx;

        BOOST_CHECK(!CheckInputScripts(
            CTransaction(spend_tx),
            state,
            &m_node.chainman->ActiveChainstate().CoinsTip(),
            SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS,
            true,
            true,
            ptd_spend_tx,
            m_node.chainman->m_validation_cache,
            nullptr));

        // Invalid results are not cached. Asking ConnectBlock-style for
        // script checks must still return the script check object.
        std::vector<CScriptCheck> scriptchecks;
        BOOST_CHECK(CheckInputScripts(
            CTransaction(spend_tx),
            state,
            &m_node.chainman->ActiveChainstate().CoinsTip(),
            SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS,
            true,
            true,
            ptd_spend_tx,
            m_node.chainman->m_validation_cache,
            &scriptchecks));
        BOOST_CHECK_EQUAL(scriptchecks.size(), 1U);

        ValidateCheckInputsForAllFlags(
            CTransaction(spend_tx),
            SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS,
            false,
            m_node.chainman->ActiveChainstate().CoinsTip(),
            m_node.chainman->m_validation_cache);
    }

    // A block containing the transaction must still be consensus-valid,
    // because DISCOURAGE_UPGRADABLE_NOPS is a policy flag rather than a
    // consensus requirement.
    CBlock block;
    block = CreateAndProcessBlock({spend_tx}, P2WSH_OP_TRUE);

    LOCK(cs_main);
    BOOST_CHECK(
        m_node.chainman->ActiveChain().Tip()->GetBlockHash() ==
        block.GetHash());
    BOOST_CHECK(
        m_node.chainman->ActiveChainstate().CoinsTip().GetBestBlock() ==
        block.GetHash());

    // Test P2SH: valid without P2SH execution, invalid when the OP_FALSE
    // redeem script is executed.
    {
        CMutableTransaction invalid_under_p2sh_tx;
        invalid_under_p2sh_tx.version = 1;
        invalid_under_p2sh_tx.vin.resize(1);
        invalid_under_p2sh_tx.vin[0].prevout.hash = spend_tx.GetHash();
        invalid_under_p2sh_tx.vin[0].prevout.n = 0;
        invalid_under_p2sh_tx.vout.resize(1);
        invalid_under_p2sh_tx.vout[0].nValue = 11 * CENT;
        invalid_under_p2sh_tx.vout[0].scriptPubKey = P2WSH_OP_TRUE;

        std::vector<unsigned char> redeem{
            p2sh_redeem_script.begin(),
            p2sh_redeem_script.end()
        };
        invalid_under_p2sh_tx.vin[0].scriptSig << redeem;

        ValidateCheckInputsForAllFlags(
            CTransaction(invalid_under_p2sh_tx),
            SCRIPT_VERIFY_P2SH,
            true,
            m_node.chainman->ActiveChainstate().CoinsTip(),
            m_node.chainman->m_validation_cache);
    }

    // Test CHECKLOCKTIMEVERIFY.
    {
        CMutableTransaction invalid_with_cltv_tx;
        invalid_with_cltv_tx.version = 1;
        invalid_with_cltv_tx.nLockTime = 100;
        invalid_with_cltv_tx.vin.resize(1);
        invalid_with_cltv_tx.vin[0].prevout.hash = spend_tx.GetHash();
        invalid_with_cltv_tx.vin[0].prevout.n = 2;
        invalid_with_cltv_tx.vin[0].nSequence = 0;
        invalid_with_cltv_tx.vout.resize(1);
        invalid_with_cltv_tx.vout[0].nValue = 11 * CENT;
        invalid_with_cltv_tx.vout[0].scriptPubKey = P2WSH_OP_TRUE;

        invalid_with_cltv_tx.vin[0].scriptSig =
            CScript() << 101;

        ValidateCheckInputsForAllFlags(
            CTransaction(invalid_with_cltv_tx),
            SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY,
            true,
            m_node.chainman->ActiveChainstate().CoinsTip(),
            m_node.chainman->m_validation_cache);

        // Make it valid and verify it under CLTV enforcement.
        invalid_with_cltv_tx.vin[0].scriptSig =
            CScript() << 100;

        TxValidationState state;
        PrecomputedTransactionData txdata;
        BOOST_CHECK(CheckInputScripts(
            CTransaction(invalid_with_cltv_tx),
            state,
            &m_node.chainman->ActiveChainstate().CoinsTip(),
            SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY,
            true,
            true,
            txdata,
            m_node.chainman->m_validation_cache,
            nullptr));
    }

    // Test CHECKSEQUENCEVERIFY.
    {
        CMutableTransaction invalid_with_csv_tx;
        invalid_with_csv_tx.version = 2;
        invalid_with_csv_tx.vin.resize(1);
        invalid_with_csv_tx.vin[0].prevout.hash = spend_tx.GetHash();
        invalid_with_csv_tx.vin[0].prevout.n = 3;
        invalid_with_csv_tx.vin[0].nSequence = 100;
        invalid_with_csv_tx.vout.resize(1);
        invalid_with_csv_tx.vout[0].nValue = 11 * CENT;
        invalid_with_csv_tx.vout[0].scriptPubKey = P2WSH_OP_TRUE;

        invalid_with_csv_tx.vin[0].scriptSig =
            CScript() << 101;

        ValidateCheckInputsForAllFlags(
            CTransaction(invalid_with_csv_tx),
            SCRIPT_VERIFY_CHECKSEQUENCEVERIFY,
            true,
            m_node.chainman->ActiveChainstate().CoinsTip(),
            m_node.chainman->m_validation_cache);

        // Make it valid and verify it under CSV enforcement.
        invalid_with_csv_tx.vin[0].scriptSig =
            CScript() << 100;

        TxValidationState state;
        PrecomputedTransactionData txdata;
        BOOST_CHECK(CheckInputScripts(
            CTransaction(invalid_with_csv_tx),
            state,
            &m_node.chainman->ActiveChainstate().CoinsTip(),
            SCRIPT_VERIFY_CHECKSEQUENCEVERIFY,
            true,
            true,
            txdata,
            m_node.chainman->m_validation_cache,
            nullptr));
    }

    // Test that caching a valid witness does not imply success for the same
    // txid after its witness is removed.
    {
        CMutableTransaction valid_with_witness_tx;
        valid_with_witness_tx.version = 1;
        valid_with_witness_tx.vin.resize(1);
        valid_with_witness_tx.vin[0].prevout.hash = spend_tx.GetHash();
        valid_with_witness_tx.vin[0].prevout.n = 1;
        valid_with_witness_tx.vout.resize(1);
        valid_with_witness_tx.vout[0].nValue = 11 * CENT;
        valid_with_witness_tx.vout[0].scriptPubKey = P2WSH_OP_TRUE;

        valid_with_witness_tx.vin[0].scriptWitness.stack.push_back(
            WITNESS_STACK_ELEM_OP_TRUE);

        ValidateCheckInputsForAllFlags(
            CTransaction(valid_with_witness_tx),
            0,
            true,
            m_node.chainman->ActiveChainstate().CoinsTip(),
            m_node.chainman->m_validation_cache);

        valid_with_witness_tx.vin[0].scriptWitness.SetNull();

        ValidateCheckInputsForAllFlags(
            CTransaction(valid_with_witness_tx),
            SCRIPT_VERIFY_WITNESS,
            true,
            m_node.chainman->ActiveChainstate().CoinsTip(),
            m_node.chainman->m_validation_cache);
    }

    // Test whole-transaction caching with multiple inputs.
    {
        CMutableTransaction tx;
        tx.version = 1;
        tx.vin.resize(2);

        tx.vin[0].prevout.hash = spend_tx.GetHash();
        tx.vin[0].prevout.n = 1;

        tx.vin[1].prevout.hash = spend_tx.GetHash();
        tx.vin[1].prevout.n = 4;

        tx.vout.resize(1);
        tx.vout[0].nValue = 22 * CENT;
        tx.vout[0].scriptPubKey = P2WSH_OP_TRUE;

        for (auto& input : tx.vin) {
            input.scriptWitness.stack.push_back(
                WITNESS_STACK_ELEM_OP_TRUE);
        }

        ValidateCheckInputsForAllFlags(
            CTransaction(tx),
            0,
            true,
            m_node.chainman->ActiveChainstate().CoinsTip(),
            m_node.chainman->m_validation_cache);

        // Invalidate only the second input.
        tx.vin[1].scriptWitness.SetNull();

        TxValidationState state;
        PrecomputedTransactionData txdata;

        BOOST_CHECK(!CheckInputScripts(
            CTransaction(tx),
            state,
            &m_node.chainman->ActiveChainstate().CoinsTip(),
            SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS,
            true,
            true,
            txdata,
            m_node.chainman->m_validation_cache,
            nullptr));

        std::vector<CScriptCheck> scriptchecks;

        BOOST_CHECK(CheckInputScripts(
            CTransaction(tx),
            state,
            &m_node.chainman->ActiveChainstate().CoinsTip(),
            SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS,
            true,
            true,
            txdata,
            m_node.chainman->m_validation_cache,
            &scriptchecks));

        // Cache entries are whole-transaction based, so both script checks
        // must be returned rather than caching only the first valid input.
        BOOST_CHECK_EQUAL(scriptchecks.size(), 2U);
    }
}

BOOST_AUTO_TEST_SUITE_END()
