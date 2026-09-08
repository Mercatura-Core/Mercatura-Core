// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>
#include <policy/policy.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <test/util/transaction_utils.h>
#include <tinyformat.h>
#include <uint256.h>


#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(multisig_tests, BasicTestingSetup)

static CScript
sign_multisig(const CScript& scriptPubKey, const std::vector<CKey>& keys, const CTransaction& transaction, int whichIn)
{
    uint256 hash = SignatureHash(scriptPubKey, transaction, whichIn, SIGHASH_ALL, 0, SigVersion::BASE);

    CScript result;
    result << OP_0; // CHECKMULTISIG bug workaround
    for (const CKey &key : keys)
    {
        std::vector<unsigned char> vchSig;
        BOOST_CHECK(key.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        result << vchSig;
    }
    return result;
}

BOOST_AUTO_TEST_CASE(multisig_verify)
{
    // Mercatura disables inherited ECDSA/Schnorr ownership authorization.
    // CHECKMULTISIG scripts may still be parsed/classified, but execution
    // must fail closed rather than authorize ownership.

    const script_verify_flags flags{
        SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC
    };

    ScriptError err;
    CKey key[4];
    CAmount amount{0};

    for (auto& k : key) {
        k.MakeNewKey(true);
    }

    const CScript a_and_b{
        CScript()
            << OP_2
            << ToByteVector(key[0].GetPubKey())
            << ToByteVector(key[1].GetPubKey())
            << OP_2
            << OP_CHECKMULTISIG
    };

    const CScript a_or_b{
        CScript()
            << OP_1
            << ToByteVector(key[0].GetPubKey())
            << ToByteVector(key[1].GetPubKey())
            << OP_2
            << OP_CHECKMULTISIG
    };

    const CScript escrow{
        CScript()
            << OP_2
            << ToByteVector(key[0].GetPubKey())
            << ToByteVector(key[1].GetPubKey())
            << ToByteVector(key[2].GetPubKey())
            << OP_3
            << OP_CHECKMULTISIG
    };

    CMutableTransaction txFrom;
    txFrom.vout.resize(3);
    txFrom.vout[0].scriptPubKey = a_and_b;
    txFrom.vout[1].scriptPubKey = a_or_b;
    txFrom.vout[2].scriptPubKey = escrow;

    CMutableTransaction txTo[3];

    for (int i = 0; i < 3; ++i) {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
    }

    // Use historically sufficient Bitcoin signature sets. Mercatura must
    // reject them specifically because CHECKMULTISIG authorization is
    // disabled, not because signatures are absent.
    const std::vector<std::vector<CKey>> signing_keys{
        {key[0], key[1]},
        {key[0]},
        {key[0], key[1]},
    };

    const CScript scripts[]{
        a_and_b,
        a_or_b,
        escrow,
    };

    for (int i = 0; i < 3; ++i) {
        const CScript script_sig{
            sign_multisig(
                scripts[i],
                signing_keys[i],
                CTransaction(txTo[i]),
                0)
        };

        err = SCRIPT_ERR_OK;

        BOOST_CHECK(!VerifyScript(
            script_sig,
            scripts[i],
            nullptr,
            flags,
            MutableTransactionSignatureChecker(
                &txTo[i],
                0,
                amount,
                MissingDataBehavior::ASSERT_FAIL),
            &err));

        BOOST_CHECK_MESSAGE(
            err == SCRIPT_ERR_BAD_OPCODE,
            strprintf(
                "classical multisig form %d: expected BAD_OPCODE, got %s",
                i,
                ScriptErrorString(err)));
    }
}

BOOST_AUTO_TEST_CASE(multisig_IsStandard)
{
    CKey key[4];
    for (int i = 0; i < 4; i++)
        key[i].MakeNewKey(true);

    const auto is_standard{[](const CScript& spk) {
        TxoutType type;
        bool res{::IsStandard(spk, type)};
        if (res) {
            BOOST_CHECK_EQUAL(type, TxoutType::MULTISIG);
        }
        return res;
    }};

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    BOOST_CHECK(is_standard(a_and_b));

    CScript a_or_b;
    a_or_b  << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    BOOST_CHECK(is_standard(a_or_b));

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
    BOOST_CHECK(is_standard(escrow));

    CScript one_of_four;
    one_of_four << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << ToByteVector(key[3].GetPubKey()) << OP_4 << OP_CHECKMULTISIG;
    BOOST_CHECK(!is_standard(one_of_four));

    CScript malformed[6];
    malformed[0] << OP_3 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    malformed[1] << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
    malformed[2] << OP_0 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    malformed[3] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_0 << OP_CHECKMULTISIG;
    malformed[4] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_CHECKMULTISIG;
    malformed[5] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey());

    for (int i = 0; i < 6; i++) {
        BOOST_CHECK(!is_standard(malformed[i]));
    }
}

BOOST_AUTO_TEST_CASE(multisig_Sign)
{
    // The inherited transaction signer must not produce ownership
    // authorization for classical CHECKMULTISIG outputs in Mercatura.
    FillableSigningProvider keystore;
    CKey key[4];

    for (auto& k : key) {
        k.MakeNewKey(true);
        BOOST_CHECK(keystore.AddKey(k));
    }

    const CScript a_and_b{
        CScript()
            << OP_2
            << ToByteVector(key[0].GetPubKey())
            << ToByteVector(key[1].GetPubKey())
            << OP_2
            << OP_CHECKMULTISIG
    };

    const CScript a_or_b{
        CScript()
            << OP_1
            << ToByteVector(key[0].GetPubKey())
            << ToByteVector(key[1].GetPubKey())
            << OP_2
            << OP_CHECKMULTISIG
    };

    const CScript escrow{
        CScript()
            << OP_2
            << ToByteVector(key[0].GetPubKey())
            << ToByteVector(key[1].GetPubKey())
            << ToByteVector(key[2].GetPubKey())
            << OP_3
            << OP_CHECKMULTISIG
    };

    CMutableTransaction txFrom;
    txFrom.vout.resize(3);
    txFrom.vout[0].scriptPubKey = a_and_b;
    txFrom.vout[1].scriptPubKey = a_or_b;
    txFrom.vout[2].scriptPubKey = escrow;

    CMutableTransaction txTo[3];

    for (int i = 0; i < 3; ++i) {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;

        SignatureData empty;

        BOOST_CHECK_MESSAGE(
            !SignSignature(
                keystore,
                CTransaction(txFrom),
                txTo[i],
                0,
                SIGHASH_ALL,
                empty),
            strprintf(
                "classical CHECKMULTISIG signer unexpectedly succeeded for output %d",
                i));
    }
}


BOOST_AUTO_TEST_SUITE_END()
