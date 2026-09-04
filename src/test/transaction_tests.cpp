// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/data/tx_invalid.json.h>
#include <test/data/tx_valid.json.h>
#include <test/util/setup_common.h>

#include <checkqueue.h>
#include <clientversion.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/tx_check.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <crypto/mercatura_mldsa.h>
#include <key.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <primitives/transaction_identifier.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sigcache.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <streams.h>
#include <test/util/common.h>
#include <test/util/json.h>
#include <test/util/random.h>
#include <test/util/script.h>
#include <test/util/transaction_utils.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <validation.h>

#include <functional>
#include <map>
#include <string>

#include <boost/test/unit_test.hpp>

#include <univalue.h>

using namespace util::hex_literals;
using util::SplitString;
using util::ToString;

typedef std::vector<unsigned char> valtype;

static CFeeRate g_dust{DUST_RELAY_TX_FEE};
static bool g_bare_multi{DEFAULT_PERMIT_BAREMULTISIG};

static const std::map<std::string, script_verify_flag_name>& mapFlagNames = ScriptFlagNamesToEnum();

script_verify_flags ParseScriptFlags(std::string strFlags)
{
    script_verify_flags flags = SCRIPT_VERIFY_NONE;
    if (strFlags.empty() || strFlags == "NONE") return flags;

    std::vector<std::string> words = SplitString(strFlags, ',');
    for (const std::string& word : words)
    {
        if (!mapFlagNames.contains(word)) {
            BOOST_ERROR("Bad test: unknown verification flag '" << word << "'");
            continue;
        }
        flags |= mapFlagNames.at(word);
    }
    return flags;
}

// Mercatura permanently disables inherited classical signature
// authorization opcodes. Some upstream Bitcoin invalid-transaction
// vectors therefore remain invalid with SCRIPT_ERR_BAD_OPCODE even
// after the vector's originally-required verification flag is removed.
//
// This helper is used only by the inherited "minimal flags" test logic.
// It does not change transaction or Script validation behavior.
bool TransactionFailsWithBadOpcode(
    const CTransaction& tx,
    const std::map<COutPoint, CScript>& map_prevout_scriptPubKeys,
    const std::map<COutPoint, int64_t>& map_prevout_values,
    script_verify_flags flags,
    const PrecomputedTransactionData& txdata)
{
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const CTxIn& input{
            tx.vin[i]
        };

        const auto script_it{
            map_prevout_scriptPubKeys.find(
                input.prevout)
        };

        if (script_it ==
            map_prevout_scriptPubKeys.end()) {
            return false;
        }

        const CAmount amount{
            map_prevout_values.contains(
                input.prevout)
                ? map_prevout_values.at(
                      input.prevout)
                : 0
        };

        ScriptError error{
            SCRIPT_ERR_UNKNOWN_ERROR
        };

        try {
            const bool valid{
                VerifyScript(
                    input.scriptSig,
                    script_it->second,
                    &input.scriptWitness,
                    flags,
                    TransactionSignatureChecker{
                        &tx,
                        i,
                        amount,
                        txdata,
                        MissingDataBehavior::ASSERT_FAIL},
                    &error)
            };

            if (!valid &&
                error ==
                    SCRIPT_ERR_BAD_OPCODE) {
                return true;
            }
        } catch (...) {
            return false;
        }
    }

    return false;
}

// Check that all flags in STANDARD_SCRIPT_VERIFY_FLAGS are present in mapFlagNames.
bool CheckMapFlagNames()
{
    script_verify_flags standard_flags_missing{STANDARD_SCRIPT_VERIFY_FLAGS};
    for (const auto& pair : mapFlagNames) {
        standard_flags_missing &= ~(pair.second);
    }
    return standard_flags_missing == 0;
}

/*
* Check that the input scripts of a transaction are valid/invalid as expected.
*/
bool CheckTxScripts(const CTransaction& tx, const std::map<COutPoint, CScript>& map_prevout_scriptPubKeys,
    const std::map<COutPoint, int64_t>& map_prevout_values, script_verify_flags flags,
    const PrecomputedTransactionData& txdata, const std::string& strTest, bool expect_valid)
{
    bool tx_valid = true;
    ScriptError err = expect_valid ? SCRIPT_ERR_UNKNOWN_ERROR : SCRIPT_ERR_OK;
    for (unsigned int i = 0; i < tx.vin.size() && tx_valid; ++i) {
        const CTxIn input = tx.vin[i];
        const CAmount amount = map_prevout_values.contains(input.prevout) ? map_prevout_values.at(input.prevout) : 0;
        try {
            tx_valid = VerifyScript(input.scriptSig, map_prevout_scriptPubKeys.at(input.prevout),
                &input.scriptWitness, flags, TransactionSignatureChecker(&tx, i, amount, txdata, MissingDataBehavior::ASSERT_FAIL), &err);
        } catch (...) {
            BOOST_ERROR("Bad test: " << strTest);
            return true; // The test format is bad and an error is thrown. Return true to silence further error.
        }
        if (expect_valid) {
            BOOST_CHECK_MESSAGE(tx_valid, strTest);
            BOOST_CHECK_MESSAGE((err == SCRIPT_ERR_OK), ScriptErrorString(err));
            err = SCRIPT_ERR_UNKNOWN_ERROR;
        }
    }
    if (!expect_valid) {
        BOOST_CHECK_MESSAGE(!tx_valid, strTest);
        BOOST_CHECK_MESSAGE((err != SCRIPT_ERR_OK), ScriptErrorString(err));
    }
    return (tx_valid == expect_valid);
}

/*
 * Trim or fill flags to make the combination valid:
 * WITNESS must be used with P2SH
 * CLEANSTACK must be used WITNESS and P2SH
 */

script_verify_flags TrimFlags(script_verify_flags flags)
{
    // WITNESS requires P2SH
    if (!(flags & SCRIPT_VERIFY_P2SH)) flags &= ~SCRIPT_VERIFY_WITNESS;

    // CLEANSTACK requires WITNESS (and transitively CLEANSTACK requires P2SH)
    if (!(flags & SCRIPT_VERIFY_WITNESS)) flags &= ~SCRIPT_VERIFY_CLEANSTACK;
    Assert(IsValidFlagCombination(flags));
    return flags;
}

script_verify_flags FillFlags(script_verify_flags flags)
{
    // CLEANSTACK implies WITNESS
    if (flags & SCRIPT_VERIFY_CLEANSTACK) flags |= SCRIPT_VERIFY_WITNESS;

    // WITNESS implies P2SH (and transitively CLEANSTACK implies P2SH)
    if (flags & SCRIPT_VERIFY_WITNESS) flags |= SCRIPT_VERIFY_P2SH;
    Assert(IsValidFlagCombination(flags));
    return flags;
}

// Exclude each possible script verify flag from flags. Returns a set of these flag combinations
// that are valid and without duplicates. For example: if flags=1111 and the 4 possible flags are
// 0001, 0010, 0100, and 1000, this should return the set {0111, 1011, 1101, 1110}.
// Assumes that mapFlagNames contains all script verify flags.
std::set<script_verify_flags> ExcludeIndividualFlags(script_verify_flags flags)
{
    std::set<script_verify_flags> flags_combos;
    for (const auto& pair : mapFlagNames) {
        script_verify_flags flags_excluding_one = TrimFlags(flags & ~(pair.second));
        if (flags != flags_excluding_one) {
            flags_combos.insert(flags_excluding_one);
        }
    }
    return flags_combos;
}

BOOST_FIXTURE_TEST_SUITE(transaction_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(tx_valid)
{
    BOOST_CHECK_MESSAGE(CheckMapFlagNames(), "mapFlagNames is missing a script verification flag");
    // Read tests from test/data/tx_valid.json
    UniValue tests = read_json(json_tests::tx_valid);

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test[0].isArray())
        {
            if (test.size() != 3 || !test[1].isStr() || !test[2].isStr())
            {
                BOOST_ERROR("Bad test: " << strTest);
                continue;
            }

            std::map<COutPoint, CScript> mapprevOutScriptPubKeys;
            std::map<COutPoint, int64_t> mapprevOutValues;
            UniValue inputs = test[0].get_array();
            bool fValid = true;
            for (unsigned int inpIdx = 0; inpIdx < inputs.size(); inpIdx++) {
                const UniValue& input = inputs[inpIdx];
                if (!input.isArray()) {
                    fValid = false;
                    break;
                }
                const UniValue& vinput = input.get_array();
                if (vinput.size() < 3 || vinput.size() > 4)
                {
                    fValid = false;
                    break;
                }
                COutPoint outpoint{Txid::FromHex(vinput[0].get_str()).value(), uint32_t(vinput[1].getInt<int>())};
                mapprevOutScriptPubKeys[outpoint] = ParseScript(vinput[2].get_str());
                if (vinput.size() >= 4)
                {
                    mapprevOutValues[outpoint] = vinput[3].getInt<int64_t>();
                }
            }
            if (!fValid)
            {
                BOOST_ERROR("Bad test: " << strTest);
                continue;
            }

            std::string transaction = test[1].get_str();
            DataStream stream(ParseHex(transaction));
            CTransaction tx(deserialize, TX_WITH_WITNESS, stream);

            TxValidationState state;
            BOOST_CHECK_MESSAGE(CheckTransaction(tx, state), strTest);
            BOOST_CHECK(state.IsValid());

            PrecomputedTransactionData txdata(tx);
            script_verify_flags verify_flags = ParseScriptFlags(test[2].get_str());

            // Check that the test gives a valid combination of flags (otherwise VerifyScript will throw). Don't edit the flags.
            if (~verify_flags != FillFlags(~verify_flags)) {
                BOOST_ERROR("Bad test flags: " << strTest);
            }

            const script_verify_flags active_flags{
                ~verify_flags
            };

            // Bitcoin's inherited tx_valid corpus contains many
            // transactions whose validity depends on classical
            // CHECKSIG/CHECKMULTISIG authorization.
            //
            // Mercatura permanently disables those ownership
            // authorization paths. CheckTransaction above still runs
            // for every vector, preserving transaction-structure
            // coverage. If the vector's intended-valid Script
            // execution now fails specifically with BAD_OPCODE, the
            // Bitcoin Script-flag compatibility permutations below
            // are not applicable to Mercatura.
            if (TransactionFailsWithBadOpcode(
                    tx,
                    mapprevOutScriptPubKeys,
                    mapprevOutValues,
                    active_flags,
                    txdata)) {
                continue;
            }

            BOOST_CHECK_MESSAGE(
                CheckTxScripts(
                    tx,
                    mapprevOutScriptPubKeys,
                    mapprevOutValues,
                    active_flags,
                    txdata,
                    strTest,
                    /*expect_valid=*/true),
                "Tx unexpectedly failed: " << strTest);

            // Backwards compatibility of script verification flags:
            // Removing any flag(s) should not invalidate a valid
            // transaction.
            for (const auto& [name, flag] : mapFlagNames) {
                // Removing individual flags
                script_verify_flags flags = TrimFlags(~(verify_flags | flag));
                if (!CheckTxScripts(tx, mapprevOutScriptPubKeys, mapprevOutValues, flags, txdata, strTest, /*expect_valid=*/true)) {
                    BOOST_ERROR("Tx unexpectedly failed with flag " << name << " unset: " << strTest);
                }
                // Removing random combinations of flags
                flags = TrimFlags(~(verify_flags | script_verify_flags::from_int(m_rng.randbits(MAX_SCRIPT_VERIFY_FLAGS_BITS))));
                if (!CheckTxScripts(tx, mapprevOutScriptPubKeys, mapprevOutValues, flags, txdata, strTest, /*expect_valid=*/true)) {
                    BOOST_ERROR("Tx unexpectedly failed with random flags " << ToString(flags.as_int()) << ": " << strTest);
                }
            }

            // Check that flags are maximal: transaction should fail if any unset flags are set.
            for (auto flags_excluding_one : ExcludeIndividualFlags(verify_flags)) {
                if (!CheckTxScripts(tx, mapprevOutScriptPubKeys, mapprevOutValues, ~flags_excluding_one, txdata, strTest, /*expect_valid=*/false)) {
                    BOOST_ERROR("Too many flags unset: " << strTest);
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(tx_invalid)
{
    // Read tests from test/data/tx_invalid.json
    UniValue tests = read_json(json_tests::tx_invalid);

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test[0].isArray())
        {
            if (test.size() != 3 || !test[1].isStr() || !test[2].isStr())
            {
                BOOST_ERROR("Bad test: " << strTest);
                continue;
            }

            std::map<COutPoint, CScript> mapprevOutScriptPubKeys;
            std::map<COutPoint, int64_t> mapprevOutValues;
            UniValue inputs = test[0].get_array();
            bool fValid = true;
            for (unsigned int inpIdx = 0; inpIdx < inputs.size(); inpIdx++) {
                const UniValue& input = inputs[inpIdx];
                if (!input.isArray()) {
                    fValid = false;
                    break;
                }
                const UniValue& vinput = input.get_array();
                if (vinput.size() < 3 || vinput.size() > 4)
                {
                    fValid = false;
                    break;
                }
                COutPoint outpoint{Txid::FromHex(vinput[0].get_str()).value(), uint32_t(vinput[1].getInt<int>())};
                mapprevOutScriptPubKeys[outpoint] = ParseScript(vinput[2].get_str());
                if (vinput.size() >= 4)
                {
                    mapprevOutValues[outpoint] = vinput[3].getInt<int64_t>();
                }
            }
            if (!fValid)
            {
                BOOST_ERROR("Bad test: " << strTest);
                continue;
            }

            std::string transaction = test[1].get_str();
            DataStream stream(ParseHex(transaction));
            CTransaction tx(deserialize, TX_WITH_WITNESS, stream);

            TxValidationState state;
            if (!CheckTransaction(tx, state) || state.IsInvalid()) {
                BOOST_CHECK_MESSAGE(test[2].get_str() == "BADTX", strTest);
                continue;
            }

            PrecomputedTransactionData txdata(tx);
            script_verify_flags verify_flags = ParseScriptFlags(test[2].get_str());

            // Check that the test gives a valid combination of flags (otherwise VerifyScript will throw). Don't edit the flags.
            if (verify_flags != FillFlags(verify_flags)) {
                BOOST_ERROR("Bad test flags: " << strTest);
            }

            // Not using FillFlags() in the main test, in order to detect invalid verifyFlags combination
            BOOST_CHECK_MESSAGE(CheckTxScripts(tx, mapprevOutScriptPubKeys, mapprevOutValues, verify_flags, txdata, strTest, /*expect_valid=*/false),
                                "Tx unexpectedly passed: " << strTest);

            // Backwards compatibility of script verification flags: Adding any flag(s) should not validate an invalid transaction
            for (const auto& [name, flag] : mapFlagNames) {
                script_verify_flags flags = FillFlags(verify_flags | flag);
                // Adding individual flags
                if (!CheckTxScripts(tx, mapprevOutScriptPubKeys, mapprevOutValues, flags, txdata, strTest, /*expect_valid=*/false)) {
                    BOOST_ERROR("Tx unexpectedly passed with flag " << name << " set: " << strTest);
                }
                // Adding random combinations of flags
                flags = FillFlags(verify_flags | script_verify_flags::from_int(m_rng.randbits(MAX_SCRIPT_VERIFY_FLAGS_BITS)));
                if (!CheckTxScripts(tx, mapprevOutScriptPubKeys, mapprevOutValues, flags, txdata, strTest, /*expect_valid=*/false)) {
                    BOOST_ERROR("Tx unexpectedly passed with random flags " << name << ": " << strTest);
                }
            }

            // Check that flags are minimal: transaction should succeed
            // if any set flag is unset.
            //
            // Exception: Mercatura permanently disables inherited
            // classical signature authorization. If removing a Bitcoin
            // verification flag still leaves the transaction failing
            // specifically with SCRIPT_ERR_BAD_OPCODE, the vector is
            // unconditionally invalid under Mercatura and the upstream
            // minimal-flag assumption no longer applies.
            for (auto flags_excluding_one : ExcludeIndividualFlags(verify_flags)) {
                if (TransactionFailsWithBadOpcode(
                        tx,
                        mapprevOutScriptPubKeys,
                        mapprevOutValues,
                        flags_excluding_one,
                        txdata)) {
                    continue;
                }

                if (!CheckTxScripts(
                        tx,
                        mapprevOutScriptPubKeys,
                        mapprevOutValues,
                        flags_excluding_one,
                        txdata,
                        strTest,
                        /*expect_valid=*/true)) {
                    BOOST_ERROR(
                        "Too many flags set: " <<
                        strTest);
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(mercatura_max_money_transaction_boundaries)
{
    auto make_base_transaction = [] {
        CMutableTransaction tx;
        tx.vin.emplace_back(
            COutPoint{Txid::FromUint256(uint256::ONE), 0});
        return tx;
    };

    // A single output exactly at MAX_MONEY is valid.
    {
        CMutableTransaction tx{make_base_transaction()};
        tx.vout.emplace_back(MAX_MONEY, CScript{} << OP_TRUE);

        TxValidationState state;
        BOOST_CHECK(CheckTransaction(CTransaction{tx}, state));
        BOOST_CHECK(state.IsValid());
    }

    // A zero-value companion output does not change the valid total.
    {
        CMutableTransaction tx{make_base_transaction()};
        tx.vout.emplace_back(MAX_MONEY, CScript{} << OP_TRUE);
        tx.vout.emplace_back(0, CScript{} << OP_TRUE);

        TxValidationState state;
        BOOST_CHECK(CheckTransaction(CTransaction{tx}, state));
        BOOST_CHECK(state.IsValid());
    }

    // An individual output one base unit above MAX_MONEY is invalid.
    {
        CMutableTransaction tx{make_base_transaction()};
        tx.vout.emplace_back(MAX_MONEY + 1, CScript{} << OP_TRUE);

        TxValidationState state;
        BOOST_CHECK(!CheckTransaction(CTransaction{tx}, state));
        BOOST_CHECK(state.IsInvalid());
        BOOST_CHECK_EQUAL(
            state.GetRejectReason(),
            "bad-txns-vout-toolarge");
    }

    // Individually valid outputs whose aggregate exceeds MAX_MONEY are
    // invalid. The chosen MAX_MONEY leaves sufficient signed CAmount
    // headroom for this addition to occur without overflow.
    {
        CMutableTransaction tx{make_base_transaction()};
        tx.vout.emplace_back(MAX_MONEY, CScript{} << OP_TRUE);
        tx.vout.emplace_back(1, CScript{} << OP_TRUE);

        TxValidationState state;
        BOOST_CHECK(!CheckTransaction(CTransaction{tx}, state));
        BOOST_CHECK(state.IsInvalid());
        BOOST_CHECK_EQUAL(
            state.GetRejectReason(),
            "bad-txns-txouttotal-toolarge");
    }
}

BOOST_AUTO_TEST_CASE(tx_no_inputs)
{
    CMutableTransaction empty;

    TxValidationState state;
    BOOST_CHECK_MESSAGE(!CheckTransaction(CTransaction(empty), state), "Transaction with no inputs should be invalid.");
    BOOST_CHECK(state.GetRejectReason() == "bad-txns-vin-empty");
}

BOOST_AUTO_TEST_CASE(tx_oversized)
{
    auto createTransaction =[](size_t payloadSize) {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vout.emplace_back(1, CScript() << OP_RETURN << std::vector<unsigned char>(payloadSize));
        return CTransaction(tx);
    };
    const auto maxTransactionSize = MAX_BLOCK_WEIGHT / WITNESS_SCALE_FACTOR;
    const auto oversizedTransactionBaseSize = ::GetSerializeSize(TX_NO_WITNESS(createTransaction(maxTransactionSize))) - maxTransactionSize;

    auto maxPayloadSize = maxTransactionSize - oversizedTransactionBaseSize;
    {
        TxValidationState state;
        CheckTransaction(createTransaction(maxPayloadSize), state);
        BOOST_CHECK(state.GetRejectReason() != "bad-txns-oversize");
    }

    maxPayloadSize += 1;
    {
        TxValidationState state;
        BOOST_CHECK_MESSAGE(!CheckTransaction(createTransaction(maxPayloadSize), state), "Oversized transaction should be invalid");
        BOOST_CHECK(state.GetRejectReason() == "bad-txns-oversize");
    }
}

BOOST_AUTO_TEST_CASE(basic_transaction_tests)
{
    // Random real transaction (e2769b09e784f32f62ef849763d4f45b98e07ba658647343b915ff832b110436)
    unsigned char ch[] = {0x01, 0x00, 0x00, 0x00, 0x01, 0x6b, 0xff, 0x7f, 0xcd, 0x4f, 0x85, 0x65, 0xef, 0x40, 0x6d, 0xd5, 0xd6, 0x3d, 0x4f, 0xf9, 0x4f, 0x31, 0x8f, 0xe8, 0x20, 0x27, 0xfd, 0x4d, 0xc4, 0x51, 0xb0, 0x44, 0x74, 0x01, 0x9f, 0x74, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x8c, 0x49, 0x30, 0x46, 0x02, 0x21, 0x00, 0xda, 0x0d, 0xc6, 0xae, 0xce, 0xfe, 0x1e, 0x06, 0xef, 0xdf, 0x05, 0x77, 0x37, 0x57, 0xde, 0xb1, 0x68, 0x82, 0x09, 0x30, 0xe3, 0xb0, 0xd0, 0x3f, 0x46, 0xf5, 0xfc, 0xf1, 0x50, 0xbf, 0x99, 0x0c, 0x02, 0x21, 0x00, 0xd2, 0x5b, 0x5c, 0x87, 0x04, 0x00, 0x76, 0xe4, 0xf2, 0x53, 0xf8, 0x26, 0x2e, 0x76, 0x3e, 0x2d, 0xd5, 0x1e, 0x7f, 0xf0, 0xbe, 0x15, 0x77, 0x27, 0xc4, 0xbc, 0x42, 0x80, 0x7f, 0x17, 0xbd, 0x39, 0x01, 0x41, 0x04, 0xe6, 0xc2, 0x6e, 0xf6, 0x7d, 0xc6, 0x10, 0xd2, 0xcd, 0x19, 0x24, 0x84, 0x78, 0x9a, 0x6c, 0xf9, 0xae, 0xa9, 0x93, 0x0b, 0x94, 0x4b, 0x7e, 0x2d, 0xb5, 0x34, 0x2b, 0x9d, 0x9e, 0x5b, 0x9f, 0xf7, 0x9a, 0xff, 0x9a, 0x2e, 0xe1, 0x97, 0x8d, 0xd7, 0xfd, 0x01, 0xdf, 0xc5, 0x22, 0xee, 0x02, 0x28, 0x3d, 0x3b, 0x06, 0xa9, 0xd0, 0x3a, 0xcf, 0x80, 0x96, 0x96, 0x8d, 0x7d, 0xbb, 0x0f, 0x91, 0x78, 0xff, 0xff, 0xff, 0xff, 0x02, 0x8b, 0xa7, 0x94, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x19, 0x76, 0xa9, 0x14, 0xba, 0xde, 0xec, 0xfd, 0xef, 0x05, 0x07, 0x24, 0x7f, 0xc8, 0xf7, 0x42, 0x41, 0xd7, 0x3b, 0xc0, 0x39, 0x97, 0x2d, 0x7b, 0x88, 0xac, 0x40, 0x94, 0xa8, 0x02, 0x00, 0x00, 0x00, 0x00, 0x19, 0x76, 0xa9, 0x14, 0xc1, 0x09, 0x32, 0x48, 0x3f, 0xec, 0x93, 0xed, 0x51, 0xf5, 0xfe, 0x95, 0xe7, 0x25, 0x59, 0xf2, 0xcc, 0x70, 0x43, 0xf9, 0x88, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::vector<unsigned char> vch(ch, ch + sizeof(ch) -1);
    CMutableTransaction tx;
    SpanReader{vch} >> TX_WITH_WITNESS(tx);
    TxValidationState state;
    BOOST_CHECK_MESSAGE(CheckTransaction(CTransaction(tx), state) && state.IsValid(), "Simple deserialized transaction should be valid.");

    // Check that duplicate txins fail
    tx.vin.push_back(tx.vin[0]);
    BOOST_CHECK_MESSAGE(!CheckTransaction(CTransaction(tx), state) || !state.IsValid(), "Transaction with duplicate txins should be invalid.");
}

BOOST_AUTO_TEST_CASE(mercatura_pq_standardness)
{
    CCoinsView coins_dummy;
    CCoinsViewCache coins{&coins_dummy};

    std::vector<unsigned char> program(WITNESS_V2_MERCATURA_PQ_SIZE);
    for (size_t i = 0; i < program.size(); ++i) {
        program[i] = static_cast<unsigned char>(i);
    }

    CMutableTransaction funding;
    funding.vin.resize(1);
    funding.vin[0].prevout.SetNull();
    funding.vin[0].scriptSig = CScript() << OP_0 << OP_0;
    funding.vout.resize(1);
    funding.vout[0].nValue = 100;
    funding.vout[0].scriptPubKey = CScript() << OP_2 << program;

    const COutPoint prevout{funding.GetHash(), 0};
    coins.AddCoin(
        prevout,
        Coin{funding.vout[0], /*nHeight=*/1, /*fCoinBase=*/false},
        /*possible_overwrite=*/false);

    CMutableTransaction spend;
    spend.version = 2;
    spend.vin.resize(1);
    spend.vin[0].prevout = prevout;
    spend.vin[0].scriptSig.clear();
    spend.vout.resize(1);
    spend.vout[0].nValue = 99;
    spend.vout[0].scriptPubKey = CScript() << OP_1;

    const auto set_valid_pq_witness = [&] {
        spend.vin[0].scriptWitness.stack = {
            std::vector<unsigned char>(MERCATURA_MLDSA65_SIGNATURE_SIZE, 0),
            std::vector<unsigned char>(MERCATURA_MLDSA65_PUBLIC_KEY_SIZE, 0),
        };
    };

    set_valid_pq_witness();

    // Exact native PQ v1 structure is standard policy.
    BOOST_CHECK(AreInputsStandard(CTransaction{spend}, coins));
    BOOST_CHECK(IsWitnessStandard(CTransaction{spend}, coins));

    // Native PQ spends require an empty scriptSig.
    spend.vin[0].scriptSig = CScript() << OP_0;
    BOOST_CHECK(!AreInputsStandard(CTransaction{spend}, coins));
    spend.vin[0].scriptSig.clear();

    // Missing and extra witness items are nonstandard.
    spend.vin[0].scriptWitness.stack.clear();
    BOOST_CHECK(!IsWitnessStandard(CTransaction{spend}, coins));

    spend.vin[0].scriptWitness.stack = {
        std::vector<unsigned char>(MERCATURA_MLDSA65_SIGNATURE_SIZE, 0),
    };
    BOOST_CHECK(!IsWitnessStandard(CTransaction{spend}, coins));

    set_valid_pq_witness();
    spend.vin[0].scriptWitness.stack.emplace_back(1, 0);
    BOOST_CHECK(!IsWitnessStandard(CTransaction{spend}, coins));

    // Signature size must be exactly 3309 bytes.
    set_valid_pq_witness();
    spend.vin[0].scriptWitness.stack[0].resize(
        MERCATURA_MLDSA65_SIGNATURE_SIZE - 1);
    BOOST_CHECK(!IsWitnessStandard(CTransaction{spend}, coins));

    set_valid_pq_witness();
    spend.vin[0].scriptWitness.stack[0].resize(
        MERCATURA_MLDSA65_SIGNATURE_SIZE + 1);
    BOOST_CHECK(!IsWitnessStandard(CTransaction{spend}, coins));

    // Public key size must be exactly 1952 bytes.
    set_valid_pq_witness();
    spend.vin[0].scriptWitness.stack[1].resize(
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE - 1);
    BOOST_CHECK(!IsWitnessStandard(CTransaction{spend}, coins));

    set_valid_pq_witness();
    spend.vin[0].scriptWitness.stack[1].resize(
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE + 1);
    BOOST_CHECK(!IsWitnessStandard(CTransaction{spend}, coins));

    // P2SH wrapping of PQ witness-v2 is explicitly nonstandard.
    const CScript pq_redeem{CScript() << OP_2 << program};

    CMutableTransaction wrapped_funding;
    wrapped_funding.vin.resize(1);
    wrapped_funding.vin[0].prevout.SetNull();
    wrapped_funding.vin[0].scriptSig = CScript() << OP_0 << OP_0;
    wrapped_funding.vout.resize(1);
    wrapped_funding.vout[0].nValue = 100;
    wrapped_funding.vout[0].scriptPubKey =
        GetScriptForDestination(ScriptHash{pq_redeem});

    const COutPoint wrapped_prevout{wrapped_funding.GetHash(), 0};
    coins.AddCoin(
        wrapped_prevout,
        Coin{wrapped_funding.vout[0], /*nHeight=*/1, /*fCoinBase=*/false},
        /*possible_overwrite=*/false);

    spend.vin[0].prevout = wrapped_prevout;
    spend.vin[0].scriptSig =
        CScript() << std::vector<unsigned char>(
            pq_redeem.begin(), pq_redeem.end());
    set_valid_pq_witness();

    BOOST_CHECK(!IsWitnessStandard(CTransaction{spend}, coins));
}

BOOST_AUTO_TEST_CASE(test_Get)
{
    FillableSigningProvider keystore;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    std::vector<CMutableTransaction> dummyTransactions =
        SetupDummyInputs(keystore, coins, {11*CENT, 50*CENT, 21*CENT, 22*CENT});

    CMutableTransaction t1;
    t1.vin.resize(3);
    t1.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t1.vin[0].prevout.n = 1;
    t1.vin[0].scriptSig << std::vector<unsigned char>(65, 0);
    t1.vin[1].prevout.hash = dummyTransactions[1].GetHash();
    t1.vin[1].prevout.n = 0;
    t1.vin[1].scriptSig << std::vector<unsigned char>(65, 0) << std::vector<unsigned char>(33, 4);
    t1.vin[2].prevout.hash = dummyTransactions[1].GetHash();
    t1.vin[2].prevout.n = 1;
    t1.vin[2].scriptSig << std::vector<unsigned char>(65, 0) << std::vector<unsigned char>(33, 4);
    t1.vout.resize(2);
    t1.vout[0].nValue = 90*CENT;
    t1.vout[0].scriptPubKey << OP_1;

    BOOST_CHECK(AreInputsStandard(CTransaction(t1), coins));
}

BOOST_AUTO_TEST_CASE(test_big_witness_transaction)
{
    CMutableTransaction mtx;
    mtx.version = 1;

    // Mercatura disables inherited ECDSA/Schnorr ownership
    // authorization. Preserve this test's large-witness and concurrent
    // CScriptCheck purpose with a signature-free native P2WSH spend
    // rather than generating thousands of expensive ML-DSA signatures.
    //
    // Each witness supplies one 65-byte stack item followed by:
    //
    //     OP_DROP OP_TRUE
    //
    // This gives every input a genuine witness-v0 execution path while
    // avoiding an obsolete classical ownership assumption.
    const CScript witness_script{
        CScript() << OP_DROP << OP_TRUE
    };

    const CScript scriptPubKey{
        GetScriptForDestination(
            WitnessV0ScriptHash(
                witness_script))
    };

    // Create a large transaction with 4500 witness inputs.
    for (uint32_t ij = 0; ij < 4500; ++ij) {
        const uint32_t i{
            static_cast<uint32_t>(
                mtx.vin.size())
        };

        const COutPoint outpoint{
            Txid{
                "0000000000000000000000000000000000000000000000000000000000000100"
            },
            i
        };

        mtx.vin.resize(
            mtx.vin.size() + 1);

        mtx.vin[i].prevout =
            outpoint;

        mtx.vin[i].scriptSig.clear();

        mtx.vin[i].scriptWitness.stack = {
            std::vector<unsigned char>(
                65,
                0),
            std::vector<unsigned char>(
                witness_script.begin(),
                witness_script.end()),
        };

        mtx.vout.resize(
            mtx.vout.size() + 1);

        mtx.vout[i].nValue =
            1000;

        mtx.vout[i].scriptPubKey =
            CScript() << OP_1;
    }

    DataStream ssout;
    ssout << TX_WITH_WITNESS(mtx);
    CTransaction tx(deserialize, TX_WITH_WITNESS, ssout);

    // check all inputs concurrently, with the cache
    PrecomputedTransactionData txdata(tx);
    CCheckQueue<CScriptCheck> scriptcheckqueue(/*batch_size=*/128, /*worker_threads_num=*/20);
    CCheckQueueControl<CScriptCheck> control(scriptcheckqueue);

    std::vector<Coin> coins;
    for(uint32_t i = 0; i < mtx.vin.size(); i++) {
        Coin coin;
        coin.nHeight = 1;
        coin.fCoinBase = false;
        coin.out.nValue = 1000;
        coin.out.scriptPubKey = scriptPubKey;
        coins.emplace_back(std::move(coin));
    }

    SignatureCache signature_cache{DEFAULT_SIGNATURE_CACHE_BYTES};

    for(uint32_t i = 0; i < mtx.vin.size(); i++) {
        std::vector<CScriptCheck> vChecks;
        vChecks.emplace_back(coins[tx.vin[i].prevout.n].out, tx, signature_cache, i, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, false, &txdata);
        control.Add(std::move(vChecks));
    }

    bool controlCheck = !control.Complete().has_value();
    assert(controlCheck);
}

BOOST_AUTO_TEST_CASE(test_witness)
{
    // Mercatura disables inherited ECDSA/Schnorr ownership
    // authorization, but witness-v0 script execution itself remains
    // useful for non-signature Script functionality.
    //
    // Exercise native and P2SH-wrapped P2WSH using:
    //
    //     <dummy> OP_DROP OP_TRUE
    //
    // This preserves witness-program, witness-stack, scriptSig,
    // serialization, and SCRIPT_VERIFY_WITNESS coverage without
    // depending on classical signatures.
    const CScript witness_script{
        CScript() << OP_DROP << OP_TRUE
    };

    const CScript witness_program{
        GetScriptForDestination(
            WitnessV0ScriptHash{
                witness_script})
    };

    const auto verify{
        [](
            const CMutableTransaction& mutable_tx,
            const CScript& prevout_script,
            script_verify_flags flags) {
            const CTransaction tx{
                mutable_tx
            };

            ScriptError error{
                SCRIPT_ERR_UNKNOWN_ERROR
            };

            return VerifyScript(
                tx.vin.at(0).scriptSig,
                prevout_script,
                &tx.vin.at(0).scriptWitness,
                flags,
                BaseSignatureChecker{},
                &error);
        }
    };

    CMutableTransaction spend;
    spend.version = 2;

    spend.vin.emplace_back(
        COutPoint{
            Txid{
                "0000000000000000000000000000000000000000000000000000000000000200"
            },
            0});

    spend.vout.emplace_back(
        1,
        CScript() << OP_TRUE);

    spend.vin.at(0).scriptSig.clear();

    spend.vin.at(0).scriptWitness.stack = {
        std::vector<unsigned char>(
            65,
            0x42),
        std::vector<unsigned char>(
            witness_script.begin(),
            witness_script.end()),
    };

    // Without SCRIPT_VERIFY_WITNESS the witness program is treated
    // according to legacy Script semantics.
    BOOST_CHECK(
        verify(
            spend,
            witness_program,
            SCRIPT_VERIFY_NONE));

    BOOST_CHECK(
        verify(
            spend,
            witness_program,
            SCRIPT_VERIFY_P2SH));

    // With witness verification enabled, execute the exact P2WSH
    // witness script and require the resulting stack to be true.
    BOOST_CHECK(
        verify(
            spend,
            witness_program,
            SCRIPT_VERIFY_P2SH |
                SCRIPT_VERIFY_WITNESS));

    BOOST_CHECK(
        verify(
            spend,
            witness_program,
            STANDARD_SCRIPT_VERIFY_FLAGS));

    // Wrong witness script must fail once witness verification is
    // enabled, while legacy evaluation does not inspect the witness.
    {
        CMutableTransaction wrong_script{
            spend
        };

        const CScript different_script{
            CScript() << OP_TRUE
        };

        wrong_script.vin.at(0)
            .scriptWitness.stack.back() =
            std::vector<unsigned char>(
                different_script.begin(),
                different_script.end());

        BOOST_CHECK(
            verify(
                wrong_script,
                witness_program,
                SCRIPT_VERIFY_NONE));

        BOOST_CHECK(
            !verify(
                wrong_script,
                witness_program,
                SCRIPT_VERIFY_P2SH |
                    SCRIPT_VERIFY_WITNESS));
    }

    // The witness script requires one item for OP_DROP.
    {
        CMutableTransaction missing_item{
            spend
        };

        missing_item.vin.at(0)
            .scriptWitness.stack.erase(
                missing_item.vin.at(0)
                    .scriptWitness.stack.begin());

        BOOST_CHECK(
            !verify(
                missing_item,
                witness_program,
                SCRIPT_VERIFY_P2SH |
                    SCRIPT_VERIFY_WITNESS));
    }

    // Native witness programs require an empty scriptSig.
    {
        CMutableTransaction nonempty_scriptsig{
            spend
        };

        nonempty_scriptsig.vin.at(0)
            .scriptSig =
            CScript() << OP_0;

        BOOST_CHECK(
            !verify(
                nonempty_scriptsig,
                witness_program,
                SCRIPT_VERIFY_P2SH |
                    SCRIPT_VERIFY_WITNESS));
    }

    // Witness data must survive transaction serialization exactly.
    {
        DataStream encoded;
        encoded << TX_WITH_WITNESS(spend);

        CMutableTransaction decoded;
        encoded >> TX_WITH_WITNESS(decoded);

        BOOST_REQUIRE_EQUAL(
            decoded.vin.size(),
            1U);

        BOOST_CHECK(
            decoded.vin.at(0)
                .scriptWitness.stack ==
            spend.vin.at(0)
                .scriptWitness.stack);

        BOOST_CHECK(
            verify(
                decoded,
                witness_program,
                SCRIPT_VERIFY_P2SH |
                    SCRIPT_VERIFY_WITNESS));
    }

    // P2SH-wrapped witness-v0 remains valid for non-signature Script.
    // Mercatura's P2SH prohibition applies specifically to PQ witness-v2.
    const CScript wrapped_program{
        GetScriptForDestination(
            ScriptHash{
                witness_program})
    };

    CMutableTransaction wrapped{
        spend
    };

    wrapped.vin.at(0).scriptSig =
        CScript() <<
        std::vector<unsigned char>(
            witness_program.begin(),
            witness_program.end());

    BOOST_CHECK(
        verify(
            wrapped,
            wrapped_program,
            SCRIPT_VERIFY_P2SH));

    BOOST_CHECK(
        verify(
            wrapped,
            wrapped_program,
            SCRIPT_VERIFY_P2SH |
                SCRIPT_VERIFY_WITNESS));

    BOOST_CHECK(
        verify(
            wrapped,
            wrapped_program,
            STANDARD_SCRIPT_VERIFY_FLAGS));

    // A different redeem program must fail the P2SH commitment.
    {
        CMutableTransaction wrong_redeem{
            wrapped
        };

        const CScript other_witness_script{
            CScript() << OP_FALSE << OP_TRUE
        };

        const CScript other_witness_program{
            GetScriptForDestination(
                WitnessV0ScriptHash{
                    other_witness_script})
        };

        wrong_redeem.vin.at(0)
            .scriptSig =
            CScript() <<
            std::vector<unsigned char>(
                other_witness_program.begin(),
                other_witness_program.end());

        BOOST_CHECK(
            !verify(
                wrong_redeem,
                wrapped_program,
                SCRIPT_VERIFY_P2SH));
    }
}

BOOST_AUTO_TEST_CASE(MercaturaUndiscountedFeeSizeTest)
{
    CMutableTransaction legacy;
    legacy.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    legacy.vout.emplace_back(1, CScript() << OP_TRUE);

    const CTransaction legacy_tx{legacy};
    const int64_t legacy_full_size{static_cast<int64_t>(GetSerializeSize(TX_WITH_WITNESS(legacy_tx)))};

    BOOST_CHECK_EQUAL(GetTransactionFeeSize(legacy_tx), legacy_full_size);
    BOOST_CHECK_EQUAL(GetTransactionFeeWeight(legacy_tx, 0, 0),
                      legacy_full_size * WITNESS_SCALE_FACTOR);
    BOOST_CHECK_EQUAL(GetTransactionFeeSize(legacy_tx),
                      GetVirtualTransactionSize(legacy_tx));

    CMutableTransaction witness{legacy};
    witness.vin[0].scriptWitness.stack.emplace_back(100, 0x42);

    const CTransaction witness_tx{witness};
    const int64_t witness_full_size{static_cast<int64_t>(GetSerializeSize(TX_WITH_WITNESS(witness_tx)))};

    BOOST_CHECK_EQUAL(GetTransactionFeeSize(witness_tx), witness_full_size);
    BOOST_CHECK_EQUAL(GetTransactionFeeWeight(witness_tx, 0, 0),
                      witness_full_size * WITNESS_SCALE_FACTOR);

    // Bitcoin-style virtual size discounts witness data. Mercatura fee policy does not.
    BOOST_CHECK_GT(GetTransactionFeeSize(witness_tx),
                   GetVirtualTransactionSize(witness_tx));
}

BOOST_AUTO_TEST_CASE(test_IsStandard)
{
    FillableSigningProvider keystore;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    std::vector<CMutableTransaction> dummyTransactions =
        SetupDummyInputs(keystore, coins, {11*CENT, 50*CENT, 21*CENT, 22*CENT});

    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t.vin[0].prevout.n = 1;
    t.vin[0].scriptSig << std::vector<unsigned char>(65, 0);
    t.vout.resize(1);
    t.vout[0].nValue = 90*CENT;

    // Mercatura's normal standard ownership output is native
    // witness-v2 PQ Authorization v1, not inherited P2PKH.
    const CScript standard_output_script{
        GetScriptForDestination(
            WitnessV2MercaturaPQ{})
    };

    // Retain one classical public key only for explicit legacy
    // standardness/classification fixtures later in this test.
    // It is not used as Mercatura's normal ownership destination.
    const CKey legacy_test_key{
        GenerateRandomKey()
    };

    t.vout[0].scriptPubKey =
        standard_output_script;

    constexpr auto CheckIsStandard = [](const auto& t, const unsigned int max_op_return_relay = MAX_OP_RETURN_RELAY) {
        std::string reason;
        BOOST_CHECK(IsStandardTx(CTransaction{t}, max_op_return_relay, g_bare_multi, g_dust, reason));
        BOOST_CHECK(reason.empty());
    };
    constexpr auto CheckIsNotStandard = [](const auto& t, const std::string& reason_in, const unsigned int max_op_return_relay = MAX_OP_RETURN_RELAY) {
        std::string reason;
        BOOST_CHECK(!IsStandardTx(CTransaction{t}, max_op_return_relay, g_bare_multi, g_dust, reason));
        BOOST_CHECK_EQUAL(reason_in, reason);
    };

    CheckIsStandard(t);

    // Mercatura's default dust floor is 0.02 MCA = 2 base units.
    const CAmount nDustThreshold = GetDustThreshold(t.vout[0], g_dust);
    BOOST_CHECK_EQUAL(nDustThreshold, 2);

    // Add dust outputs up to allowed maximum, still standard!
    for (size_t i{0}; i < MAX_DUST_OUTPUTS_PER_TX; ++i) {
        t.vout.emplace_back(0, t.vout[0].scriptPubKey);
        CheckIsStandard(t);
    }

    // dust:
    t.vout[0].nValue = nDustThreshold - 1;
    CheckIsNotStandard(t, "dust");
    // not dust:
    t.vout[0].nValue = nDustThreshold;
    CheckIsStandard(t);

    // Disallowed version
    t.version = std::numeric_limits<uint32_t>::max();
    CheckIsNotStandard(t, "version");

    t.version = 0;
    CheckIsNotStandard(t, "version");

    t.version = TX_MAX_STANDARD_VERSION + 1;
    CheckIsNotStandard(t, "version");

    // Allowed version
    t.version = 1;
    CheckIsStandard(t);

    t.version = 2;
    CheckIsStandard(t);

    // Check the standard PQ output's dust boundary with an odd
    // relay fee. The exact threshold differs from inherited P2PKH
    // because Mercatura accounts for the much larger PQ spend size.
    g_dust = CFeeRate(3702);

    const CAmount odd_fee_pq_dust_threshold{
        GetDustThreshold(
            t.vout[0],
            g_dust)
    };

    BOOST_CHECK_GT(
        odd_fee_pq_dust_threshold,
        0);

    // One base unit below the calculated threshold is dust.
    t.vout[0].nValue =
        odd_fee_pq_dust_threshold - 1;

    CheckIsNotStandard(
        t,
        "dust");

    // The exact threshold is standard.
    t.vout[0].nValue =
        odd_fee_pq_dust_threshold;

    CheckIsStandard(t);

    // Mercatura does not apply Bitcoin's witness discount to dust
    // accounting. Keep this inherited P2WPKH shape only as an
    // accounting fixture; it is not a normal Mercatura ownership
    // destination.
    //
    // P2WPKH output: 31 serialized bytes + 148-byte full input
    // estimate = 179 bytes.
    t.vout[0].scriptPubKey =
        CScript() <<
        OP_0 <<
        std::vector<unsigned char>(
            20,
            0);

    BOOST_CHECK_EQUAL(
        GetDustThreshold(
            t.vout[0],
            g_dust),
        663);

    // Restore Mercatura's standard PQ output and default dust relay fee.
    t.vout[0].scriptPubKey =
        standard_output_script;

    g_dust =
        CFeeRate{
            DUST_RELAY_TX_FEE};

    t.vout[0].scriptPubKey = CScript() << OP_1;
    CheckIsNotStandard(t, "scriptpubkey");

    // Custom 83-byte TxoutType::NULL_DATA (standard with max_op_return_relay of 83)
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38"_hex;
    BOOST_CHECK_EQUAL(83, t.vout[0].scriptPubKey.size());
    CheckIsStandard(t, /*max_op_return_relay=*/83);

    // Non-standard if max_op_return_relay datacarrier arg is one less
    CheckIsNotStandard(t, "datacarrier", /*max_op_return_relay=*/82);

    // Data payload can be encoded in any way...
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ""_hex;
    CheckIsStandard(t);
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << "00"_hex << "01"_hex;
    CheckIsStandard(t);
    // OP_RESERVED *is* considered to be a PUSHDATA type opcode by IsPushOnly()!
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << OP_RESERVED << -1 << 0 << "01"_hex << 2 << 3 << 4 << 5 << 6 << 7 << 8 << 9 << 10 << 11 << 12 << 13 << 14 << 15 << 16;
    CheckIsStandard(t);
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << 0 << "01"_hex << 2 << "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"_hex;
    CheckIsStandard(t);

    // ...so long as it only contains PUSHDATA's
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << OP_RETURN;
    CheckIsNotStandard(t, "scriptpubkey");

    // TxoutType::NULL_DATA w/o PUSHDATA
    t.vout.resize(1);
    t.vout[0].scriptPubKey = CScript() << OP_RETURN;
    CheckIsStandard(t);

    // Multiple TxoutType::NULL_DATA are permitted
    t.vout.resize(2);
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38"_hex;
    t.vout[0].nValue = 0;
    t.vout[1].scriptPubKey = CScript() << OP_RETURN << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38"_hex;
    t.vout[1].nValue = 0;
    CheckIsStandard(t);

    t.vout[0].scriptPubKey = CScript() << OP_RETURN << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38"_hex;
    t.vout[1].scriptPubKey = CScript() << OP_RETURN;
    CheckIsStandard(t);

    t.vout[0].scriptPubKey = CScript() << OP_RETURN;
    t.vout[1].scriptPubKey = CScript() << OP_RETURN;
    CheckIsStandard(t);

    t.vout[0].scriptPubKey = CScript() << OP_RETURN << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38"_hex;
    t.vout[1].scriptPubKey = CScript() << OP_RETURN << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38"_hex;
    const auto datacarrier_size = t.vout[0].scriptPubKey.size() + t.vout[1].scriptPubKey.size();
    CheckIsStandard(t); // Default max relay should never trigger
    CheckIsStandard(t, /*max_op_return_relay=*/datacarrier_size);
    CheckIsNotStandard(t, "datacarrier", /*max_op_return_relay=*/datacarrier_size-1);

    // Check large scriptSig (non-standard if size is >1650 bytes)
    t.vout.resize(1);
    t.vout[0].nValue = MAX_MONEY;
    t.vout[0].scriptPubKey =
        standard_output_script;
    // OP_PUSHDATA2 with len (3 bytes) + data (1647 bytes) = 1650 bytes
    t.vin[0].scriptSig = CScript() << std::vector<unsigned char>(1647, 0); // 1650
    CheckIsStandard(t);

    t.vin[0].scriptSig = CScript() << std::vector<unsigned char>(1648, 0); // 1651
    CheckIsNotStandard(t, "scriptsig-size");

    // Check scriptSig format (non-standard if there are any other ops than just PUSHs)
    t.vin[0].scriptSig = CScript()
        << OP_TRUE << OP_0 << OP_1NEGATE << OP_16 // OP_n (single byte pushes: n = 1, 0, -1, 16)
        << std::vector<unsigned char>(75, 0)      // OP_PUSHx [...x bytes...]
        << std::vector<unsigned char>(235, 0)     // OP_PUSHDATA1 x [...x bytes...]
        << std::vector<unsigned char>(1234, 0)    // OP_PUSHDATA2 x [...x bytes...]
        << OP_9;
    CheckIsStandard(t);

    const std::vector<unsigned char> non_push_ops = { // arbitrary set of non-push operations
        OP_NOP, OP_VERIFY, OP_IF, OP_ROT, OP_3DUP, OP_SIZE, OP_EQUAL, OP_ADD, OP_SUB,
        OP_HASH256, OP_CODESEPARATOR, OP_CHECKSIG, OP_CHECKLOCKTIMEVERIFY };

    CScript::const_iterator pc = t.vin[0].scriptSig.begin();
    while (pc < t.vin[0].scriptSig.end()) {
        opcodetype opcode;
        CScript::const_iterator prev_pc = pc;
        t.vin[0].scriptSig.GetOp(pc, opcode); // advance to next op
        // for the sake of simplicity, we only replace single-byte push operations
        if (opcode >= 1 && opcode <= OP_PUSHDATA4)
            continue;

        int index = prev_pc - t.vin[0].scriptSig.begin();
        unsigned char orig_op = *prev_pc; // save op
        // replace current push-op with each non-push-op
        for (auto op : non_push_ops) {
            t.vin[0].scriptSig[index] = op;
            CheckIsNotStandard(t, "scriptsig-not-pushonly");
        }
        t.vin[0].scriptSig[index] = orig_op; // restore op
        CheckIsStandard(t);
    }

    // Check tx-size (non-standard if transaction weight is > MAX_STANDARD_TX_WEIGHT)
    t.vin.clear();
    t.vin.resize(2438); // size per input (empty scriptSig): 41 bytes
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << std::vector<unsigned char>(19, 0); // output size: 30 bytes
    // tx header:                12 bytes =>     48 weight units
    // 2438 inputs: 2438*41 = 99958 bytes => 399832 weight units
    //    1 output:              30 bytes =>    120 weight units
    //                      ======================================
    //                                total: 400000 weight units
    BOOST_CHECK_EQUAL(GetTransactionWeight(CTransaction(t)), 400000);
    CheckIsStandard(t);

    // increase output size by one byte, so we end up with 400004 weight units
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << std::vector<unsigned char>(20, 0); // output size: 31 bytes
    BOOST_CHECK_EQUAL(GetTransactionWeight(CTransaction(t)), 400004);
    CheckIsNotStandard(t, "tx-size");

    // Check bare multisig (standard if policy flag g_bare_multi is set)
    g_bare_multi = true;
    t.vout[0].scriptPubKey = GetScriptForMultisig(1, {legacy_test_key.GetPubKey()}); // simple 1-of-1
    t.vin.resize(1);
    t.vin[0].scriptSig = CScript() << std::vector<unsigned char>(65, 0);
    CheckIsStandard(t);

    g_bare_multi = false;
    CheckIsNotStandard(t, "bare-multisig");
    g_bare_multi = DEFAULT_PERMIT_BAREMULTISIG;

    // Add dust outputs up to allowed maximum
    assert(t.vout.size() == 1);
    t.vout.insert(t.vout.end(), MAX_DUST_OUTPUTS_PER_TX, {0, t.vout[0].scriptPubKey});

    // Check compressed P2PK outputs use Mercatura's 2-base-unit dust floor.
    t.vout[0].scriptPubKey = CScript() << std::vector<unsigned char>(33, 0x02) << OP_CHECKSIG;
    t.vout[0].nValue = 2;
    CheckIsStandard(t);
    t.vout[0].nValue = 1;
    CheckIsNotStandard(t, "dust");

    // Check uncompressed P2PK outputs use Mercatura's 2-base-unit dust floor.
    t.vout[0].scriptPubKey = CScript() << std::vector<unsigned char>(65, 0x04) << OP_CHECKSIG;
    t.vout[0].nValue = 2;
    CheckIsStandard(t);
    t.vout[0].nValue = 1;
    CheckIsNotStandard(t, "dust");

    // Check P2PKH outputs use Mercatura's 2-base-unit dust floor.
    t.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, 0) << OP_EQUALVERIFY << OP_CHECKSIG;
    t.vout[0].nValue = 2;
    CheckIsStandard(t);
    t.vout[0].nValue = 1;
    CheckIsNotStandard(t, "dust");

    // Check P2SH outputs use Mercatura's 2-base-unit dust floor.
    t.vout[0].scriptPubKey = CScript() << OP_HASH160 << std::vector<unsigned char>(20, 0) << OP_EQUAL;
    t.vout[0].nValue = 2;
    CheckIsStandard(t);
    t.vout[0].nValue = 1;
    CheckIsNotStandard(t, "dust");

    // Check P2WPKH outputs use the same full-byte Mercatura dust policy.
    t.vout[0].scriptPubKey = CScript() << OP_0 << std::vector<unsigned char>(20, 0);
    t.vout[0].nValue = 2;
    CheckIsStandard(t);
    t.vout[0].nValue = 1;
    CheckIsNotStandard(t, "dust");

    // Check P2WSH outputs use the same full-byte Mercatura dust policy.
    t.vout[0].scriptPubKey = CScript() << OP_0 << std::vector<unsigned char>(32, 0);
    t.vout[0].nValue = 2;
    CheckIsStandard(t);
    t.vout[0].nValue = 1;
    CheckIsNotStandard(t, "dust");

    // Check P2TR outputs use the same full-byte Mercatura dust policy.
    t.vout[0].scriptPubKey = CScript() << OP_1 << std::vector<unsigned char>(32, 0);
    t.vout[0].nValue = 2;
    CheckIsStandard(t);
    t.vout[0].nValue = 1;
    CheckIsNotStandard(t, "dust");

    // Check undefined future witness-program versions against
    // Mercatura's dust policy.
    //
    // Witness version 2 is permanently assigned to PQ Authorization v1
    // and requires exactly a 32-byte program, so a 2-byte v2 program is
    // deliberately nonstandard and must not be treated as an undefined
    // future witness version.
    for (int op = OP_1; op <= OP_16; ++op) {
        if (op == OP_2) {
            continue;
        }

        t.vout[0].scriptPubKey =
            CScript() <<
            static_cast<opcodetype>(op) <<
            std::vector<unsigned char>(2, 0);

        t.vout[0].nValue = 2;
        CheckIsStandard(t);

        t.vout[0].nValue = 1;
        CheckIsNotStandard(t, "dust");
    }

    // Witness-v2 is Mercatura PQ Authorization v1 only. Any v2
    // program length other than exactly 32 bytes is nonstandard
    // regardless of output value.
    t.vout[0].scriptPubKey =
        CScript() <<
        OP_2 <<
        std::vector<unsigned char>(2, 0);

    t.vout[0].nValue = 2;
    CheckIsNotStandard(t, "scriptpubkey");

    t.vout[0].nValue = 1;
    CheckIsNotStandard(t, "scriptpubkey");

    // Check anchor outputs
    t.vout[0].scriptPubKey = CScript() << OP_1 << ANCHOR_BYTES;
    BOOST_CHECK(t.vout[0].scriptPubKey.IsPayToAnchor());
    t.vout[0].nValue = 2;
    CheckIsStandard(t);
    t.vout[0].nValue = 1;
    CheckIsNotStandard(t, "dust");
}

BOOST_AUTO_TEST_CASE(max_standard_legacy_sigops)
{
    CCoinsView coins_dummy;
    CCoinsViewCache coins(&coins_dummy);
    CKey key;
    key.MakeNewKey(true);

    // Create a pathological P2SH script padded with as many sigops as is standard.
    CScript max_sigops_redeem_script{CScript() << std::vector<unsigned char>{} << key.GetPubKey()};
    for (unsigned i{0}; i < MAX_P2SH_SIGOPS - 1; ++i) max_sigops_redeem_script << OP_2DUP << OP_CHECKSIG << OP_DROP;
    max_sigops_redeem_script << OP_CHECKSIG << OP_NOT;
    const CScript max_sigops_p2sh{GetScriptForDestination(ScriptHash(max_sigops_redeem_script))};

    // Create a transaction fanning out as many such P2SH outputs as is standard to spend in a
    // single transaction, and a transaction spending them.
    CMutableTransaction tx_create, tx_max_sigops;
    const unsigned p2sh_inputs_count{MAX_TX_LEGACY_SIGOPS / MAX_P2SH_SIGOPS};
    tx_create.vout.reserve(p2sh_inputs_count);
    for (unsigned i{0}; i < p2sh_inputs_count; ++i) {
        tx_create.vout.emplace_back(424242 + i, max_sigops_p2sh);
    }
    auto prev_txid{tx_create.GetHash()};
    tx_max_sigops.vin.reserve(p2sh_inputs_count);
    for (unsigned i{0}; i < p2sh_inputs_count; ++i) {
        tx_max_sigops.vin.emplace_back(prev_txid, i, CScript() << ToByteVector(max_sigops_redeem_script));
    }

    // p2sh_inputs_count is truncated to 166 (from 166.6666..)
    BOOST_CHECK_LT(p2sh_inputs_count * MAX_P2SH_SIGOPS, MAX_TX_LEGACY_SIGOPS);
    AddCoins(coins, CTransaction(tx_create), 0, false);

    // 2490 sigops is below the limit.
    BOOST_CHECK_EQUAL(GetP2SHSigOpCount(CTransaction(tx_max_sigops), coins), 2490);
    BOOST_CHECK(::AreInputsStandard(CTransaction(tx_max_sigops), coins));

    // Adding one more input will bump this to 2505, hitting the limit.
    tx_create.vout.emplace_back(424242, max_sigops_p2sh);
    prev_txid = tx_create.GetHash();
    for (unsigned i{0}; i < p2sh_inputs_count; ++i) {
        tx_max_sigops.vin[i] = CTxIn(COutPoint(prev_txid, i), CScript() << ToByteVector(max_sigops_redeem_script));
    }
    tx_max_sigops.vin.emplace_back(prev_txid, p2sh_inputs_count, CScript() << ToByteVector(max_sigops_redeem_script));
    AddCoins(coins, CTransaction(tx_create), 0, false);
    BOOST_CHECK_GT((p2sh_inputs_count + 1) * MAX_P2SH_SIGOPS, MAX_TX_LEGACY_SIGOPS);
    BOOST_CHECK_EQUAL(GetP2SHSigOpCount(CTransaction(tx_max_sigops), coins), 2505);
    BOOST_CHECK(!::AreInputsStandard(CTransaction(tx_max_sigops), coins));

    // Now, check the limit can be reached with regular P2PK outputs too. Use a separate
    // preparation transaction, to demonstrate spending coins from a single tx is irrelevant.
    CMutableTransaction tx_create_p2pk;
    const auto p2pk_script{CScript() << key.GetPubKey() << OP_CHECKSIG};
    unsigned p2pk_inputs_count{10}; // From 2490 to 2500.
    for (unsigned i{0}; i < p2pk_inputs_count; ++i) {
        tx_create_p2pk.vout.emplace_back(212121 + i, p2pk_script);
    }
    prev_txid = tx_create_p2pk.GetHash();
    tx_max_sigops.vin.resize(p2sh_inputs_count); // Drop the extra input.
    for (unsigned i{0}; i < p2pk_inputs_count; ++i) {
        tx_max_sigops.vin.emplace_back(prev_txid, i);
    }
    AddCoins(coins, CTransaction(tx_create_p2pk), 0, false);

    // The transaction now contains exactly 2500 sigops, the check should pass.
    BOOST_CHECK_EQUAL(p2sh_inputs_count * MAX_P2SH_SIGOPS + p2pk_inputs_count * 1, MAX_TX_LEGACY_SIGOPS);
    BOOST_CHECK(::AreInputsStandard(CTransaction(tx_max_sigops), coins));

    // Now, add some Segwit inputs. We add one for each defined Segwit output type. The limit
    // is exclusively on non-witness sigops and therefore those should not be counted.
    CMutableTransaction tx_create_segwit;
    const auto witness_script{CScript() << key.GetPubKey() << OP_CHECKSIG};
    tx_create_segwit.vout.emplace_back(121212, GetScriptForDestination(WitnessV0KeyHash(key.GetPubKey())));
    tx_create_segwit.vout.emplace_back(131313, GetScriptForDestination(WitnessV0ScriptHash(witness_script)));
    tx_create_segwit.vout.emplace_back(141414, GetScriptForDestination(WitnessV1Taproot{XOnlyPubKey(key.GetPubKey())}));
    prev_txid = tx_create_segwit.GetHash();
    for (unsigned i{0}; i < tx_create_segwit.vout.size(); ++i) {
        tx_max_sigops.vin.emplace_back(prev_txid, i);
    }

    // The transaction now still contains exactly 2500 sigops, the check should pass.
    AddCoins(coins, CTransaction(tx_create_segwit), 0, false);
    BOOST_REQUIRE(::AreInputsStandard(CTransaction(tx_max_sigops), coins));

    // Add one more P2PK input. We'll reach the limit.
    tx_create_p2pk.vout.emplace_back(212121, p2pk_script);
    prev_txid = tx_create_p2pk.GetHash();
    tx_max_sigops.vin.resize(p2sh_inputs_count);
    ++p2pk_inputs_count;
    for (unsigned i{0}; i < p2pk_inputs_count; ++i) {
        tx_max_sigops.vin.emplace_back(prev_txid, i);
    }
    AddCoins(coins, CTransaction(tx_create_p2pk), 0, false);
    BOOST_CHECK_GT(p2sh_inputs_count * MAX_P2SH_SIGOPS + p2pk_inputs_count * 1, MAX_TX_LEGACY_SIGOPS);
    BOOST_CHECK(!::AreInputsStandard(CTransaction(tx_max_sigops), coins));
}

BOOST_AUTO_TEST_CASE(checktxinputs_invalid_transactions_test)
{
    auto check_invalid{[](CAmount input_value, CAmount output_value, bool coinbase, int spend_height, TxValidationResult expected_result, std::string_view expected_reason) {
        CCoinsView coins_dummy;
        CCoinsViewCache inputs(&coins_dummy);

        const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
        inputs.AddCoin(prevout, Coin{{input_value, CScript() << OP_TRUE}, /*nHeightIn=*/1, coinbase}, /*possible_overwrite=*/false);

        CMutableTransaction mtx;
        mtx.vin.emplace_back(prevout);
        mtx.vout.emplace_back(output_value, CScript() << OP_TRUE);

        TxValidationState state;
        CAmount txfee{0};
        BOOST_CHECK(!Consensus::CheckTxInputs(CTransaction{mtx}, state, inputs, spend_height, txfee));
        BOOST_CHECK(state.IsInvalid());
        BOOST_CHECK_EQUAL(state.GetResult(), expected_result);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), expected_reason);
    }};

    check_invalid(/*input_value=*/MAX_MONEY + 1,
                  /*output_value=*/0,
                  /*coinbase=*/false,
                  /*spend_height=*/2,
                  TxValidationResult::TX_CONSENSUS, /*expected_reason=*/"bad-txns-inputvalues-outofrange");

    check_invalid(/*input_value=*/1 * COIN,
                  /*output_value=*/2 * COIN,
                  /*coinbase=*/false,
                  /*spend_height=*/2,
                  TxValidationResult::TX_CONSENSUS, /*expected_reason=*/"bad-txns-in-belowout");

    check_invalid(/*input_value=*/1 * COIN,
                  /*output_value=*/0,
                  /*coinbase=*/true,
                  /*spend_height=*/COINBASE_MATURITY,
                  TxValidationResult::TX_PREMATURE_SPEND, /*expected_reason=*/"bad-txns-premature-spend-of-coinbase");
}

BOOST_AUTO_TEST_CASE(getvalueout_out_of_range_throws)
{
    CMutableTransaction mtx;
    mtx.vout.emplace_back(MAX_MONEY + 1, CScript() << OP_TRUE);

    const CTransaction tx{mtx};
    BOOST_CHECK_EXCEPTION(tx.GetValueOut(), std::runtime_error, HasReason("GetValueOut: value out of range"));
}

/** Sanity check the return value of SpendsNonAnchorWitnessProg for various output types. */
BOOST_AUTO_TEST_CASE(spends_witness_prog)
{
    CCoinsView coins_dummy;
    CCoinsViewCache coins(&coins_dummy);
    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey{key.GetPubKey()};
    CMutableTransaction tx_create{}, tx_spend{};
    tx_create.vout.emplace_back(0, CScript{});
    tx_spend.vin.emplace_back(Txid{}, 0);
    std::vector<std::vector<uint8_t>> sol_dummy;

    // CNoDestination, PubKeyDestination, PKHash, ScriptHash, WitnessV0ScriptHash, WitnessV0KeyHash,
    // WitnessV1Taproot, WitnessV2MercaturaPQ, PayToAnchor, WitnessUnknown.
    static_assert(std::variant_size_v<CTxDestination> == 10);

    // Go through all defined output types and sanity check SpendsNonAnchorWitnessProg.

    // P2PK
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(PubKeyDestination{pubkey});
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::PUBKEY);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(!::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // P2PKH
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(PKHash{pubkey});
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::PUBKEYHASH);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(!::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // P2SH
    auto redeem_script{CScript{} << OP_1 << OP_CHECKSIG};
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash{redeem_script});
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::SCRIPTHASH);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    tx_spend.vin[0].scriptSig = CScript{} << OP_0 << ToByteVector(redeem_script);
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(!::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));
    tx_spend.vin[0].scriptSig.clear();

    // native P2WSH
    const auto witness_script{CScript{} << OP_12 << OP_HASH160 << OP_DUP << OP_EQUAL};
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(WitnessV0ScriptHash{witness_script});
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::WITNESS_V0_SCRIPTHASH);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // P2SH-wrapped P2WSH
    redeem_script = tx_create.vout[0].scriptPubKey;
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash(redeem_script));
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::SCRIPTHASH);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    tx_spend.vin[0].scriptSig = CScript{} << ToByteVector(redeem_script);
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));
    tx_spend.vin[0].scriptSig.clear();
    BOOST_CHECK(!::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // native P2WPKH
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(WitnessV0KeyHash{pubkey});
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::WITNESS_V0_KEYHASH);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // P2SH-wrapped P2WPKH
    redeem_script = tx_create.vout[0].scriptPubKey;
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash(redeem_script));
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::SCRIPTHASH);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    tx_spend.vin[0].scriptSig = CScript{} << ToByteVector(redeem_script);
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));
    tx_spend.vin[0].scriptSig.clear();
    BOOST_CHECK(!::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // P2TR
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(WitnessV1Taproot{XOnlyPubKey{pubkey}});
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::WITNESS_V1_TAPROOT);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // P2SH-wrapped P2TR (undefined, non-standard)
    redeem_script = tx_create.vout[0].scriptPubKey;
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash(redeem_script));
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::SCRIPTHASH);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    tx_spend.vin[0].scriptSig = CScript{} << ToByteVector(redeem_script);
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));
    tx_spend.vin[0].scriptSig.clear();
    BOOST_CHECK(!::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // Mercatura PQ Authorization v1 (native witness v2)
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(WitnessV2MercaturaPQ{});
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::WITNESS_V2_MERCATURA_PQ);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // P2A
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(PayToAnchor{});
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::ANCHOR);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(!::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // P2SH-wrapped P2A (undefined, non-standard)
    redeem_script = tx_create.vout[0].scriptPubKey;
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash(redeem_script));
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::SCRIPTHASH);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    tx_spend.vin[0].scriptSig = CScript{} << ToByteVector(redeem_script);
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));
    tx_spend.vin[0].scriptSig.clear();

    // Undefined version 1 witness program
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(WitnessUnknown{1, {0x42, 0x42}});
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::WITNESS_UNKNOWN);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // P2SH-wrapped undefined version 1 witness program
    redeem_script = tx_create.vout[0].scriptPubKey;
    tx_create.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash(redeem_script));
    BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::SCRIPTHASH);
    tx_spend.vin[0].prevout.hash = tx_create.GetHash();
    tx_spend.vin[0].scriptSig = CScript{} << ToByteVector(redeem_script);
    AddCoins(coins, CTransaction{tx_create}, 0, false);
    BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));
    tx_spend.vin[0].scriptSig.clear();
    BOOST_CHECK(!::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

    // Various undefined witness versions 3-16 with 32-byte programs.
    // Witness version 2 is permanently assigned to Mercatura PQ Authorization v1.
    const auto program{ToByteVector(XOnlyPubKey{pubkey})};
    for (int i{3}; i <= 16; ++i) {
        tx_create.vout[0].scriptPubKey = GetScriptForDestination(WitnessUnknown{i, program});
        BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::WITNESS_UNKNOWN);
        tx_spend.vin[0].prevout.hash = tx_create.GetHash();
        AddCoins(coins, CTransaction{tx_create}, 0, false);
        BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));

        // It's also detected within P2SH.
        redeem_script = tx_create.vout[0].scriptPubKey;
        tx_create.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash(redeem_script));
        BOOST_CHECK_EQUAL(Solver(tx_create.vout[0].scriptPubKey, sol_dummy), TxoutType::SCRIPTHASH);
        tx_spend.vin[0].prevout.hash = tx_create.GetHash();
        tx_spend.vin[0].scriptSig = CScript{} << ToByteVector(redeem_script);
        AddCoins(coins, CTransaction{tx_create}, 0, false);
        BOOST_CHECK(::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));
        tx_spend.vin[0].scriptSig.clear();
        BOOST_CHECK(!::SpendsNonAnchorWitnessProg(CTransaction{tx_spend}, coins));
    }
}

BOOST_AUTO_TEST_SUITE_END()
