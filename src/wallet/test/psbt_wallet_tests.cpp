// Copyright (c) 2017-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/mercatura_mldsa.h>
#include <crypto/mercatura_pqderive.h>
#include <key_io.h>
#include <node/types.h>
#include <psbt.h>
#include <util/bip32.h>
#include <util/strencodings.h>
#include <wallet/wallet.h>

#include <boost/test/unit_test.hpp>
#include <wallet/test/util.h>
#include <test/util/setup_common.h>
#include <wallet/test/wallet_test_fixture.h>

using namespace util::hex_literals;

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(psbt_wallet_tests, WalletTestingSetup)

static void import_descriptor(CWallet& wallet, const std::string& descriptor)
    EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    AssertLockHeld(wallet.cs_wallet);
    FlatSigningProvider provider;
    std::string error;
    auto descs = Parse(descriptor, provider, error, /* require_checksum=*/ false);
    assert(descs.size() == 1);
    auto& desc = descs.at(0);
    WalletDescriptor w_desc(std::move(desc), 0, 0, 10, 0);
    Assert(wallet.AddWalletDescriptor(w_desc, provider, "", false));
}

BOOST_AUTO_TEST_CASE(psbt_updater_test)
{
    LOCK(m_wallet.cs_wallet);
    m_wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);

    // Create prevtxs and add to wallet
    DataStream s_prev_tx1{
        "0200000000010158e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd7501000000171600145f275f436b09a8cc9a2eb2a2f528485c68a56323feffffff02d8231f1b0100000017a914aed962d6654f9a2b36608eb9d64d2b260db4f1118700c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e88702483045022100a22edcc6e5bc511af4cc4ae0de0fcd75c7e04d8c1c3a8aa9d820ed4b967384ec02200642963597b9b1bc22c75e9f3e117284a962188bf5e8a74c895089046a20ad770121035509a48eb623e10aace8bfd0212fdb8a8e5af3c94b0b133b95e114cab89e4f7965000000"_hex,
    };
    CTransactionRef prev_tx1;
    s_prev_tx1 >> TX_WITH_WITNESS(prev_tx1);
    m_wallet.mapWallet.emplace(std::piecewise_construct, std::forward_as_tuple(prev_tx1->GetHash()), std::forward_as_tuple(prev_tx1, TxStateInactive{}));

    DataStream s_prev_tx2{
        "0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f618765000000"_hex,
    };
    CTransactionRef prev_tx2;
    s_prev_tx2 >> TX_WITH_WITNESS(prev_tx2);
    m_wallet.mapWallet.emplace(std::piecewise_construct, std::forward_as_tuple(prev_tx2->GetHash()), std::forward_as_tuple(prev_tx2, TxStateInactive{}));

    // Import descriptors for keys and scripts
    import_descriptor(m_wallet, "sh(multi(2,xprv9s21ZrQH143K2LE7W4Xf3jATf9jECxSb7wj91ZnmY4qEJrS66Qru9RFqq8xbkgT32ya6HqYJweFdJUEDf5Q6JFV7jMiUws7kQfe6Tv4RbfN/0h/0h/0h,xprv9s21ZrQH143K2LE7W4Xf3jATf9jECxSb7wj91ZnmY4qEJrS66Qru9RFqq8xbkgT32ya6HqYJweFdJUEDf5Q6JFV7jMiUws7kQfe6Tv4RbfN/0h/0h/1h))");
    import_descriptor(m_wallet, "sh(wsh(multi(2,xprv9s21ZrQH143K2LE7W4Xf3jATf9jECxSb7wj91ZnmY4qEJrS66Qru9RFqq8xbkgT32ya6HqYJweFdJUEDf5Q6JFV7jMiUws7kQfe6Tv4RbfN/0h/0h/2h,xprv9s21ZrQH143K2LE7W4Xf3jATf9jECxSb7wj91ZnmY4qEJrS66Qru9RFqq8xbkgT32ya6HqYJweFdJUEDf5Q6JFV7jMiUws7kQfe6Tv4RbfN/0h/0h/3h)))");
    import_descriptor(m_wallet, "wpkh(xprv9s21ZrQH143K2LE7W4Xf3jATf9jECxSb7wj91ZnmY4qEJrS66Qru9RFqq8xbkgT32ya6HqYJweFdJUEDf5Q6JFV7jMiUws7kQfe6Tv4RbfN/0h/0h/*h)");

    // Call FillPSBT
    PartiallySignedTransaction psbtx;
    DataStream ssData{
        "70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f000000000000000000"_hex,
    };
    ssData >> psbtx;

    // Fill transaction with our data
    bool complete = true;
    BOOST_REQUIRE(!m_wallet.FillPSBT(psbtx, complete, std::nullopt, false, true));

    // Get the final tx
    DataStream ssTx{};
    ssTx << psbtx;
    std::string final_hex = HexStr(ssTx);
    BOOST_CHECK_EQUAL(final_hex, "70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f00000000000100bb0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f6187650000000104475221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae2206029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f10d90c6a4f000000800000008000000080220602dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d710d90c6a4f0000008000000080010000800001008a020000000158e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd7501000000171600145f275f436b09a8cc9a2eb2a2f528485c68a56323feffffff02d8231f1b0100000017a914aed962d6654f9a2b36608eb9d64d2b260db4f1118700c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e8876500000001012000c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e88701042200208c2353173743b595dfb4a07b72ba8e42e3797da74e87fe7d9d7497e3b2028903010547522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae2206023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7310d90c6a4f000000800000008003000080220603089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc10d90c6a4f00000080000000800200008000220203a9a4c37f5996d3aa25dbac6b570af0650394492942460b354753ed9eeca5877110d90c6a4f000000800000008004000080002202027f6399757d2eff55a136ad02c684b1838b6556e5f1b6b34282a94b6b5005109610d90c6a4f00000080000000800500008000");

    // Mutate the transaction so that one of the inputs is invalid
    psbtx.tx->vin[0].prevout.n = 2;

    // Try to sign the mutated input
    SignatureData sigdata;
    BOOST_CHECK(m_wallet.FillPSBT(psbtx, complete, std::nullopt, true, true));
}

BOOST_AUTO_TEST_CASE(parse_hd_keypath)
{
    std::vector<uint32_t> keypath;

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1", keypath));
    BOOST_CHECK(!ParseHDKeypath("///////////////////////////", keypath));

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1'/1", keypath));
    BOOST_CHECK(!ParseHDKeypath("//////////////////////////'/", keypath));

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/", keypath));
    BOOST_CHECK(!ParseHDKeypath("1///////////////////////////", keypath));

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1'/", keypath));
    BOOST_CHECK(!ParseHDKeypath("1/'//////////////////////////", keypath));

    BOOST_CHECK(ParseHDKeypath("", keypath));
    BOOST_CHECK(!ParseHDKeypath(" ", keypath));

    BOOST_CHECK(ParseHDKeypath("0", keypath));
    BOOST_CHECK(!ParseHDKeypath("O", keypath));

    BOOST_CHECK(ParseHDKeypath("0000'/0000'/0000'", keypath));
    BOOST_CHECK(!ParseHDKeypath("0000,/0000,/0000,", keypath));

    BOOST_CHECK(ParseHDKeypath("01234", keypath));
    BOOST_CHECK(!ParseHDKeypath("0x1234", keypath));

    BOOST_CHECK(ParseHDKeypath("1", keypath));
    BOOST_CHECK(!ParseHDKeypath(" 1", keypath));

    BOOST_CHECK(ParseHDKeypath("42", keypath));
    BOOST_CHECK(!ParseHDKeypath("m42", keypath));

    BOOST_CHECK(ParseHDKeypath("4294967295", keypath)); // 4294967295 == 0xFFFFFFFF (uint32_t max)
    BOOST_CHECK(!ParseHDKeypath("4294967296", keypath)); // 4294967296 == 0xFFFFFFFF (uint32_t max) + 1

    BOOST_CHECK(ParseHDKeypath("m", keypath));
    BOOST_CHECK(!ParseHDKeypath("n", keypath));

    BOOST_CHECK(ParseHDKeypath("m/", keypath));
    BOOST_CHECK(!ParseHDKeypath("n/", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0", keypath));
    BOOST_CHECK(!ParseHDKeypath("n/0", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0'", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/0''", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0'/0'", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/'0/0'", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/0", keypath));
    BOOST_CHECK(!ParseHDKeypath("n/0/0", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/0/00", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/0/0/f00", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/0/000000000000000000000000000000000000000000000000000000000000000000000000000000000000", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/1/1/111111111111111111111111111111111111111111111111111111111111111111111111111111111111", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/00/0", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/0'/00/'0", keypath));

    BOOST_CHECK(ParseHDKeypath("m/1/", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/1//", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/4294967295", keypath)); // 4294967295 == 0xFFFFFFFF (uint32_t max)
    BOOST_CHECK(!ParseHDKeypath("m/0/4294967296", keypath)); // 4294967296 == 0xFFFFFFFF (uint32_t max) + 1

    BOOST_CHECK(ParseHDKeypath("m/4294967295", keypath)); // 4294967295 == 0xFFFFFFFF (uint32_t max)
    BOOST_CHECK(!ParseHDKeypath("m/4294967296", keypath)); // 4294967296 == 0xFFFFFFFF (uint32_t max) + 1
}


BOOST_AUTO_TEST_CASE(mercatura_pq_psbt_proprietary_roundtrip)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.emplace_back(
        COutPoint{Txid{}, 0});
    tx.vout.emplace_back(
        1,
        CScript{});

    PartiallySignedTransaction psbt{
        tx
    };

    std::vector<unsigned char> public_key(
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE,
        0x42);

    std::vector<unsigned char> signature(
        MERCATURA_MLDSA65_SIGNATURE_SIZE,
        0x93);

    BOOST_REQUIRE(
        SetMercaturaPQPSBTPublicKey(
            psbt.inputs.at(0),
            public_key));

    BOOST_REQUIRE(
        SetMercaturaPQPSBTSignature(
            psbt.inputs.at(0),
            signature));

    const auto* stored_public_key{
        GetMercaturaPQPSBTPublicKey(
            psbt.inputs.at(0))
    };

    const auto* stored_signature{
        GetMercaturaPQPSBTSignature(
            psbt.inputs.at(0))
    };

    BOOST_REQUIRE(stored_public_key);
    BOOST_REQUIRE(stored_signature);

    BOOST_CHECK(
        *stored_public_key ==
        public_key);

    BOOST_CHECK(
        *stored_signature ==
        signature);

    DataStream encoded;
    encoded << psbt;

    PartiallySignedTransaction decoded;
    encoded >> decoded;

    BOOST_REQUIRE_EQUAL(
        decoded.inputs.size(),
        1U);

    const auto* decoded_public_key{
        GetMercaturaPQPSBTPublicKey(
            decoded.inputs.at(0))
    };

    const auto* decoded_signature{
        GetMercaturaPQPSBTSignature(
            decoded.inputs.at(0))
    };

    BOOST_REQUIRE(decoded_public_key);
    BOOST_REQUIRE(decoded_signature);

    BOOST_CHECK(
        *decoded_public_key ==
        public_key);

    BOOST_CHECK(
        *decoded_signature ==
        signature);
}

BOOST_AUTO_TEST_CASE(mercatura_pq_psbt_proprietary_reject_malformed)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.emplace_back(
        COutPoint{Txid{}, 0});
    tx.vout.emplace_back(
        1,
        CScript{});

    // The typed setters must reject incorrect sizes before any
    // proprietary field is added.
    {
        PartiallySignedTransaction psbt{
            tx
        };

        std::vector<unsigned char> short_public_key(
            MERCATURA_MLDSA65_PUBLIC_KEY_SIZE - 1,
            0x11);

        std::vector<unsigned char> short_signature(
            MERCATURA_MLDSA65_SIGNATURE_SIZE - 1,
            0x22);

        BOOST_CHECK(
            !SetMercaturaPQPSBTPublicKey(
                psbt.inputs.at(0),
                short_public_key));

        BOOST_CHECK(
            !SetMercaturaPQPSBTSignature(
                psbt.inputs.at(0),
                short_signature));

        BOOST_CHECK(
            psbt.inputs.at(0)
                .m_proprietary.empty());
    }

    // A malformed public-key value supplied directly through the generic
    // proprietary container must be rejected when the PSBT is decoded.
    {
        PartiallySignedTransaction malformed{
            tx
        };

        std::vector<unsigned char> short_public_key(
            MERCATURA_MLDSA65_PUBLIC_KEY_SIZE - 1,
            0x33);

        malformed.inputs.at(0)
            .m_proprietary.insert(
                MakeMercaturaPQPSBTProprietary(
                    PSBT_MERCATURA_PQ_PUBLIC_KEY,
                    short_public_key));

        DataStream encoded;
        encoded << malformed;

        PartiallySignedTransaction decoded;

        BOOST_CHECK_THROW(
            encoded >> decoded,
            std::ios_base::failure);
    }

    // The same exact-size rule applies to signatures.
    {
        PartiallySignedTransaction malformed{
            tx
        };

        std::vector<unsigned char> short_signature(
            MERCATURA_MLDSA65_SIGNATURE_SIZE - 1,
            0x44);

        malformed.inputs.at(0)
            .m_proprietary.insert(
                MakeMercaturaPQPSBTProprietary(
                    PSBT_MERCATURA_PQ_SIGNATURE,
                    short_signature));

        DataStream encoded;
        encoded << malformed;

        PartiallySignedTransaction decoded;

        BOOST_CHECK_THROW(
            encoded >> decoded,
            std::ios_base::failure);
    }

    // Mercatura PQ v1 fields use no proprietary key-data following
    // the subtype. Generic PSBT software may preserve other proprietary
    // records, but our recognized PQ fields must use the canonical key.
    {
        PartiallySignedTransaction malformed{
            tx
        };

        std::vector<unsigned char> public_key(
            MERCATURA_MLDSA65_PUBLIC_KEY_SIZE,
            0x55);

        PSBTProprietary entry{
            MakeMercaturaPQPSBTProprietary(
                PSBT_MERCATURA_PQ_PUBLIC_KEY,
                public_key)
        };

        entry.key.push_back(
            0x01);

        malformed.inputs.at(0)
            .m_proprietary.insert(
                std::move(entry));

        DataStream encoded;
        encoded << malformed;

        PartiallySignedTransaction decoded;

        BOOST_CHECK_THROW(
            encoded >> decoded,
            std::ios_base::failure);
    }
}

BOOST_AUTO_TEST_CASE(mercatura_pq_psbt_proprietary_combine)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.emplace_back(
        COutPoint{Txid{}, 0});
    tx.vout.emplace_back(
        1,
        CScript{});

    std::vector<unsigned char> public_key(
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE,
        0x66);

    std::vector<unsigned char> signature(
        MERCATURA_MLDSA65_SIGNATURE_SIZE,
        0x77);

    PartiallySignedTransaction with_public_key{
        tx
    };

    PartiallySignedTransaction with_signature{
        tx
    };

    BOOST_REQUIRE(
        SetMercaturaPQPSBTPublicKey(
            with_public_key.inputs.at(0),
            public_key));

    BOOST_REQUIRE(
        SetMercaturaPQPSBTSignature(
            with_signature.inputs.at(0),
            signature));

    PartiallySignedTransaction combined;

    BOOST_REQUIRE(
        CombinePSBTs(
            combined,
            {
                with_public_key,
                with_signature
            }));

    const auto* combined_public_key{
        GetMercaturaPQPSBTPublicKey(
            combined.inputs.at(0))
    };

    const auto* combined_signature{
        GetMercaturaPQPSBTSignature(
            combined.inputs.at(0))
    };

    BOOST_REQUIRE(combined_public_key);
    BOOST_REQUIRE(combined_signature);

    BOOST_CHECK(
        *combined_public_key ==
        public_key);

    BOOST_CHECK(
        *combined_signature ==
        signature);

    // Two PSBTs must not silently combine contradictory Mercatura PQ
    // values for the same proprietary subtype.
    PartiallySignedTransaction conflict_a{
        tx
    };

    PartiallySignedTransaction conflict_b{
        tx
    };

    std::vector<unsigned char> public_key_a(
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE,
        0x88);

    std::vector<unsigned char> public_key_b(
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE,
        0x99);

    BOOST_REQUIRE(
        SetMercaturaPQPSBTPublicKey(
            conflict_a.inputs.at(0),
            public_key_a));

    BOOST_REQUIRE(
        SetMercaturaPQPSBTPublicKey(
            conflict_b.inputs.at(0),
            public_key_b));

    PartiallySignedTransaction conflict_result;

    BOOST_CHECK(
        !CombinePSBTs(
            conflict_result,
            {
                conflict_a,
                conflict_b
            }));
}


BOOST_AUTO_TEST_CASE(mercatura_pq_psbt_finalize)
{
    std::array<
        unsigned char,
        MERCATURA_MLDSA65_SEED_SIZE>
        seed{};

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_RANDOM_SIZE>
        randomness{};

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE>
        public_key{};

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_SECRET_KEY_SIZE>
        secret_key{};

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_SIGNATURE_SIZE>
        signature{};

    for (size_t i = 0; i < seed.size(); ++i) {
        seed[i] =
            static_cast<unsigned char>(i);
    }

    BOOST_REQUIRE_EQUAL(
        mercatura_mldsa65_keypair_from_seed(
            public_key.data(),
            public_key.size(),
            secret_key.data(),
            secret_key.size(),
            seed.data(),
            seed.size()),
        1);

    MercaturaPQKeyCommitment commitment{};

    BOOST_REQUIRE(
        ComputeMercaturaPQKeyCommitmentV1(
            commitment,
            public_key));

    const std::vector<unsigned char> program{
        commitment.begin(),
        commitment.end()
    };

    CScript pq_script;
    pq_script << OP_2 << program;

    CMutableTransaction tx;
    tx.version = 2;

    tx.vin.emplace_back(
        COutPoint{Txid{}, 0});

    CScript recipient_script;
    recipient_script << OP_TRUE;

    tx.vout.emplace_back(
        10000,
        recipient_script);

    PartiallySignedTransaction base_psbt{
        tx
    };

    base_psbt.inputs.at(0).witness_utxo =
        CTxOut{
            20000,
            pq_script
        };

    const PrecomputedTransactionData txdata{
        PrecomputePSBTData(
            base_psbt)
    };

    BOOST_REQUIRE(
        txdata.m_pq_ready);

    MercaturaPQHash384 digest{};

    BOOST_REQUIRE(
        ComputeMercaturaPQAuthDigestV1(
            digest,
            *base_psbt.tx,
            0,
            Params().GetConsensus().hashGenesisBlock,
            txdata));

    static constexpr char PQ_CONTEXT[] =
        "Mercatura/PQAuth/v1";

    BOOST_REQUIRE_EQUAL(
        mercatura_mldsa65_sign(
            signature.data(),
            signature.size(),
            digest.data(),
            digest.size(),
            reinterpret_cast<const uint8_t*>(
                PQ_CONTEXT),
            sizeof(PQ_CONTEXT) - 1,
            randomness.data(),
            randomness.size(),
            secret_key.data(),
            secret_key.size()),
        1);

    BOOST_REQUIRE(
        SetMercaturaPQPSBTPublicKey(
            base_psbt.inputs.at(0),
            public_key));

    BOOST_REQUIRE(
        SetMercaturaPQPSBTSignature(
            base_psbt.inputs.at(0),
            signature));

    // --------------------------------------------------------
    // Valid PQ PSBT finalization.
    // --------------------------------------------------------

    PartiallySignedTransaction finalized{
        base_psbt
    };

    BOOST_REQUIRE(
        FinalizePSBT(
            finalized));

    BOOST_CHECK(
        finalized.inputs.at(0)
            .final_script_sig.empty());

    BOOST_REQUIRE_EQUAL(
        finalized.inputs.at(0)
            .final_script_witness.stack.size(),
        2U);

    BOOST_CHECK_EQUAL(
        finalized.inputs.at(0)
            .final_script_witness.stack.at(0).size(),
        MERCATURA_MLDSA65_SIGNATURE_SIZE);

    BOOST_CHECK_EQUAL(
        finalized.inputs.at(0)
            .final_script_witness.stack.at(1).size(),
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

    BOOST_CHECK(
        finalized.inputs.at(0)
            .final_script_witness.stack.at(0) ==
        std::vector<unsigned char>(
            signature.begin(),
            signature.end()));

    BOOST_CHECK(
        finalized.inputs.at(0)
            .final_script_witness.stack.at(1) ==
        std::vector<unsigned char>(
            public_key.begin(),
            public_key.end()));

    // Re-finalization must recognize the already-finalized witness
    // as valid under the active Mercatura genesis context.
    BOOST_CHECK(
        FinalizePSBT(
            finalized));

    // A valid finalized PQ witness must still reject contradictory
    // PSBT sighash metadata.
    {
        PartiallySignedTransaction bad_metadata{
            finalized
        };

        bad_metadata.inputs.at(0).sighash_type =
            SIGHASH_ALL;

        const PrecomputedTransactionData bad_txdata{
            PrecomputePSBTData(
                bad_metadata)
        };

        BOOST_CHECK(
            !PSBTInputSignedAndVerified(
                bad_metadata,
                0,
                &bad_txdata));

        BOOST_CHECK(
            !FinalizePSBT(
                bad_metadata));
    }

    // --------------------------------------------------------
    // Finalize-and-extract must put the same exact PQ witness
    // onto the extracted transaction input.
    // --------------------------------------------------------

    PartiallySignedTransaction extract_psbt{
        base_psbt
    };

    CMutableTransaction extracted;

    BOOST_REQUIRE(
        FinalizeAndExtractPSBT(
            extract_psbt,
            extracted));

    BOOST_REQUIRE_EQUAL(
        extracted.vin.at(0)
            .scriptWitness.stack.size(),
        2U);

    BOOST_CHECK(
        extracted.vin.at(0)
            .scriptSig.empty());

    BOOST_CHECK(
        extracted.vin.at(0)
            .scriptWitness.stack.at(0) ==
        std::vector<unsigned char>(
            signature.begin(),
            signature.end()));

    BOOST_CHECK(
        extracted.vin.at(0)
            .scriptWitness.stack.at(1) ==
        std::vector<unsigned char>(
            public_key.begin(),
            public_key.end()));

    // --------------------------------------------------------
    // Corrupted signature must not finalize.
    // --------------------------------------------------------

    {
        PartiallySignedTransaction bad{
            base_psbt
        };

        auto bad_signature{
            signature
        };

        bad_signature.at(0) ^= 0x01;

        BOOST_REQUIRE(
            SetMercaturaPQPSBTSignature(
                bad.inputs.at(0),
                bad_signature));

        BOOST_CHECK(
            !FinalizePSBT(
                bad));

        BOOST_CHECK(
            bad.inputs.at(0)
                .final_script_witness.IsNull());
    }

    // --------------------------------------------------------
    // Corrupted public key must fail the output commitment
    // and/or ML-DSA verification.
    // --------------------------------------------------------

    {
        PartiallySignedTransaction bad{
            base_psbt
        };

        auto bad_public_key{
            public_key
        };

        bad_public_key.at(0) ^= 0x01;

        BOOST_REQUIRE(
            SetMercaturaPQPSBTPublicKey(
                bad.inputs.at(0),
                bad_public_key));

        BOOST_CHECK(
            !FinalizePSBT(
                bad));

        BOOST_CHECK(
            bad.inputs.at(0)
                .final_script_witness.IsNull());
    }

    // --------------------------------------------------------
    // PQ Authorization v1 supports SIGHASH_DEFAULT only.
    // --------------------------------------------------------

    {
        PartiallySignedTransaction bad{
            base_psbt
        };

        bad.inputs.at(0).sighash_type =
            SIGHASH_ALL;

        BOOST_CHECK(
            !FinalizePSBT(
                bad));

        BOOST_CHECK(
            bad.inputs.at(0)
                .final_script_witness.IsNull());
    }

    // Explicit SIGHASH_DEFAULT is permitted.
    {
        PartiallySignedTransaction good{
            base_psbt
        };

        good.inputs.at(0).sighash_type =
            SIGHASH_DEFAULT;

        BOOST_CHECK(
            FinalizePSBT(
                good));
    }

    // --------------------------------------------------------
    // Missing spent-output context must fail closed.
    // --------------------------------------------------------

    {
        PartiallySignedTransaction missing{
            base_psbt
        };

        missing.inputs.at(0)
            .witness_utxo.SetNull();

        BOOST_CHECK(
            !FinalizePSBT(
                missing));

        BOOST_CHECK(
            missing.inputs.at(0)
                .final_script_witness.IsNull());
    }

    memory_cleanse(
        secret_key.data(),
        secret_key.size());
}


BOOST_AUTO_TEST_CASE(mercatura_pq_psbt_multi_input_binding)
{
    static constexpr char PQ_CONTEXT[] =
        "Mercatura/PQAuth/v1";

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_RANDOM_SIZE>
        randomness{};

    struct PQTestKey
    {
        std::array<
            unsigned char,
            MERCATURA_MLDSA65_PUBLIC_KEY_SIZE>
            public_key{};

        std::array<
            unsigned char,
            MERCATURA_MLDSA65_SECRET_KEY_SIZE>
            secret_key{};

        MercaturaPQKeyCommitment commitment{};
    };

    const auto make_key =
        [](unsigned char seed_offset) {
            std::array<
                unsigned char,
                MERCATURA_MLDSA65_SEED_SIZE>
                seed{};

            for (size_t i = 0;
                 i < seed.size();
                 ++i) {
                seed[i] =
                    static_cast<unsigned char>(
                        seed_offset + i);
            }

            PQTestKey key;

            BOOST_REQUIRE_EQUAL(
                mercatura_mldsa65_keypair_from_seed(
                    key.public_key.data(),
                    key.public_key.size(),
                    key.secret_key.data(),
                    key.secret_key.size(),
                    seed.data(),
                    seed.size()),
                1);

            BOOST_REQUIRE(
                ComputeMercaturaPQKeyCommitmentV1(
                    key.commitment,
                    key.public_key));

            return key;
        };

    PQTestKey key_a{
        make_key(0x10)
    };

    PQTestKey key_b{
        make_key(0x50)
    };

    const auto make_pq_script =
        [](const MercaturaPQKeyCommitment& commitment) {
            const std::vector<unsigned char> program{
                commitment.begin(),
                commitment.end()
            };

            CScript script;
            script << OP_2 << program;

            return script;
        };

    const CScript pq_script_a{
        make_pq_script(
            key_a.commitment)
    };

    const CScript pq_script_b{
        make_pq_script(
            key_b.commitment)
    };

    CMutableTransaction tx;
    tx.version = 2;

    tx.vin.emplace_back(
        COutPoint{
            Txid{},
            0});

    tx.vin.emplace_back(
        COutPoint{
            Txid{},
            1});

    CScript recipient_script;
    recipient_script << OP_TRUE;

    tx.vout.emplace_back(
        30000,
        recipient_script);

    tx.vout.emplace_back(
        15000,
        recipient_script);

    PartiallySignedTransaction base_psbt{
        tx
    };

    base_psbt.inputs.at(0).witness_utxo =
        CTxOut{
            25000,
            pq_script_a
        };

    base_psbt.inputs.at(1).witness_utxo =
        CTxOut{
            30000,
            pq_script_b
        };

    const PrecomputedTransactionData txdata{
        PrecomputePSBTData(
            base_psbt)
    };

    BOOST_REQUIRE(
        txdata.m_pq_ready);

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_SIGNATURE_SIZE>
        signature_a{};

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_SIGNATURE_SIZE>
        signature_b{};

    MercaturaPQHash384 digest_a{};
    MercaturaPQHash384 digest_b{};

    BOOST_REQUIRE(
        ComputeMercaturaPQAuthDigestV1(
            digest_a,
            *base_psbt.tx,
            0,
            Params().GetConsensus().hashGenesisBlock,
            txdata));

    BOOST_REQUIRE(
        ComputeMercaturaPQAuthDigestV1(
            digest_b,
            *base_psbt.tx,
            1,
            Params().GetConsensus().hashGenesisBlock,
            txdata));

    BOOST_REQUIRE(
        digest_a != digest_b);

    BOOST_REQUIRE_EQUAL(
        mercatura_mldsa65_sign(
            signature_a.data(),
            signature_a.size(),
            digest_a.data(),
            digest_a.size(),
            reinterpret_cast<const uint8_t*>(
                PQ_CONTEXT),
            sizeof(PQ_CONTEXT) - 1,
            randomness.data(),
            randomness.size(),
            key_a.secret_key.data(),
            key_a.secret_key.size()),
        1);

    BOOST_REQUIRE_EQUAL(
        mercatura_mldsa65_sign(
            signature_b.data(),
            signature_b.size(),
            digest_b.data(),
            digest_b.size(),
            reinterpret_cast<const uint8_t*>(
                PQ_CONTEXT),
            sizeof(PQ_CONTEXT) - 1,
            randomness.data(),
            randomness.size(),
            key_b.secret_key.data(),
            key_b.secret_key.size()),
        1);

    BOOST_REQUIRE(
        SetMercaturaPQPSBTPublicKey(
            base_psbt.inputs.at(0),
            key_a.public_key));

    BOOST_REQUIRE(
        SetMercaturaPQPSBTSignature(
            base_psbt.inputs.at(0),
            signature_a));

    BOOST_REQUIRE(
        SetMercaturaPQPSBTPublicKey(
            base_psbt.inputs.at(1),
            key_b.public_key));

    BOOST_REQUIRE(
        SetMercaturaPQPSBTSignature(
            base_psbt.inputs.at(1),
            signature_b));

    // --------------------------------------------------------
    // Both PQ inputs finalize against one shared transaction
    // authorization context.
    // --------------------------------------------------------

    {
        PartiallySignedTransaction good{
            base_psbt
        };

        BOOST_REQUIRE(
            FinalizePSBT(
                good));

        BOOST_REQUIRE_EQUAL(
            good.inputs.at(0)
                .final_script_witness.stack.size(),
            2U);

        BOOST_REQUIRE_EQUAL(
            good.inputs.at(1)
                .final_script_witness.stack.size(),
            2U);
    }

    // --------------------------------------------------------
    // Input 0's authorization commits to the amount/script
    // information for input 1 as well.
    // --------------------------------------------------------

    {
        PartiallySignedTransaction bad{
            base_psbt
        };

        bad.inputs.at(1)
            .witness_utxo.nValue += 1;

        BOOST_CHECK(
            !FinalizePSBT(
                bad));

        BOOST_CHECK(
            bad.inputs.at(0)
                .final_script_witness.IsNull());
    }

    // --------------------------------------------------------
    // PQ Authorization v1 also commits to all transaction
    // outputs. Changing an output after signing must fail.
    // --------------------------------------------------------

    {
        PartiallySignedTransaction bad{
            base_psbt
        };

        bad.tx->vout.at(0)
            .nValue += 1;

        BOOST_CHECK(
            !FinalizePSBT(
                bad));

        BOOST_CHECK(
            bad.inputs.at(0)
                .final_script_witness.IsNull());
    }

    // --------------------------------------------------------
    // Input ordering/prevout identity is committed as well.
    // --------------------------------------------------------

    {
        PartiallySignedTransaction bad{
            base_psbt
        };

        std::swap(
            bad.tx->vin.at(0),
            bad.tx->vin.at(1));

        BOOST_CHECK(
            !FinalizePSBT(
                bad));

        BOOST_CHECK(
            bad.inputs.at(0)
                .final_script_witness.IsNull());
    }

    memory_cleanse(
        key_a.secret_key.data(),
        key_a.secret_key.size());

    memory_cleanse(
        key_b.secret_key.data(),
        key_b.secret_key.size());
}


BOOST_AUTO_TEST_CASE(mercatura_pq_psbt_wallet_signing)
{
    CWallet wallet{
        m_node.chain.get(),
        "",
        CreateMockableWalletDatabase()
    };

    CKeyingMaterial master_seed(
        MERCATURA_PQ_WALLET_MASTER_SEED_SIZE);

    for (size_t i = 0;
         i < master_seed.size();
         ++i) {
        master_seed[i] =
            static_cast<unsigned char>(
                0x30 + i);
    }

    struct TestPQKey
    {
        std::array<
            uint8_t,
            MERCATURA_MLDSA65_PUBLIC_KEY_SIZE>
            public_key{};

        MercaturaPQKeyCommitment commitment{};

        MercaturaPQKeyLocator locator{};
    };

    const auto derive_key =
        [&](uint32_t index) {
            const std::span<
                const unsigned char,
                MERCATURA_PQ_WALLET_MASTER_SEED_SIZE>
                seed_span{
                    master_seed.data(),
                    MERCATURA_PQ_WALLET_MASTER_SEED_SIZE
                };

            auto child_seed{
                DeriveMercaturaPQChildSeed(
                    seed_span,
                    Params().GetConsensus().hashGenesisBlock,
                    /*account=*/0,
                    /*branch=*/0,
                    index)
            };

            BOOST_REQUIRE(
                child_seed.has_value());

            TestPQKey key;
            key.locator = {
                /*account=*/0,
                /*branch=*/0,
                index
            };

            std::array<
                uint8_t,
                MERCATURA_MLDSA65_SECRET_KEY_SIZE>
                secret_key{};

            BOOST_REQUIRE_EQUAL(
                mercatura_mldsa65_keypair_from_seed(
                    key.public_key.data(),
                    key.public_key.size(),
                    secret_key.data(),
                    secret_key.size(),
                    child_seed->data(),
                    child_seed->size()),
                1);

            BOOST_REQUIRE(
                ComputeMercaturaPQKeyCommitmentV1(
                    key.commitment,
                    key.public_key));

            memory_cleanse(
                child_seed->data(),
                child_seed->size());

            memory_cleanse(
                secret_key.data(),
                secret_key.size());

            return key;
        };

    const TestPQKey key_a{
        derive_key(0)
    };

    const TestPQKey key_b{
        derive_key(1)
    };

    // Deliberately not loaded into the wallet.
    const TestPQKey unowned_key{
        derive_key(2)
    };

    MercaturaPQWalletState state;
    state.account = 0;
    state.next_external_index = 3;
    state.next_internal_index = 0;

    {
        LOCK(wallet.cs_wallet);

        BOOST_REQUIRE(
            wallet.LoadMercaturaPQState(
                state));

        BOOST_REQUIRE(
            wallet.LoadMercaturaPQSeed(
                master_seed));

        BOOST_REQUIRE(
            wallet.LoadMercaturaPQKeyLocator(
                key_a.commitment,
                key_a.locator));

        BOOST_REQUIRE(
            wallet.LoadMercaturaPQKeyLocator(
                key_b.commitment,
                key_b.locator));

        BOOST_REQUIRE(
            wallet.ValidateMercaturaPQKeyLocators());
    }

    const auto pq_script =
        [](const MercaturaPQKeyCommitment& commitment) {
            const std::vector<unsigned char> program{
                commitment.begin(),
                commitment.end()
            };

            CScript script;
            script << OP_2 << program;
            return script;
        };

    const auto make_psbt =
        [&](const std::vector<
                std::pair<
                    CAmount,
                    MercaturaPQKeyCommitment>>& inputs,
            const std::vector<CAmount>& outputs) {
            CMutableTransaction tx;
            tx.version = 2;

            for (size_t i = 0;
                 i < inputs.size();
                 ++i) {
                tx.vin.emplace_back(
                    COutPoint{
                        Txid{},
                        static_cast<uint32_t>(i)
                    });
            }

            CScript recipient;
            recipient << OP_TRUE;

            for (const CAmount amount :
                 outputs) {
                tx.vout.emplace_back(
                    amount,
                    recipient);
            }

            PartiallySignedTransaction psbt{
                tx
            };

            for (size_t i = 0;
                 i < inputs.size();
                 ++i) {
                psbt.inputs.at(i)
                    .witness_utxo =
                    CTxOut{
                        inputs.at(i).first,
                        pq_script(
                            inputs.at(i).second)
                    };
            }

            return psbt;
        };

    // ========================================================
    // sign=false:
    // report that the wallet can sign, but do not create
    // authorization material.
    // ========================================================

    {
        auto psbt{
            make_psbt(
                {
                    {
                        20000,
                        key_a.commitment
                    }
                },
                {10000})
        };

        bool complete{true};
        size_t n_signed{0};

        const auto error{
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/false,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/false)
        };

        BOOST_CHECK(
            !error.has_value());

        BOOST_CHECK(
            !complete);

        BOOST_CHECK_EQUAL(
            n_signed,
            1U);

        BOOST_CHECK(
            GetMercaturaPQPSBTPublicKey(
                psbt.inputs.at(0)) ==
            nullptr);

        BOOST_CHECK(
            GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0)) ==
            nullptr);

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_witness.IsNull());
    }

    // ========================================================
    // sign=true, finalize=false:
    // add proprietary PQ authorization data but leave the
    // PSBT explicitly incomplete/unfinalized.
    // ========================================================

    {
        auto psbt{
            make_psbt(
                {
                    {
                        20000,
                        key_a.commitment
                    }
                },
                {10000})
        };

        bool complete{true};
        size_t n_signed{0};

        const auto error{
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/false)
        };

        BOOST_CHECK(
            !error.has_value());

        BOOST_CHECK(
            !complete);

        BOOST_CHECK_EQUAL(
            n_signed,
            1U);

        const auto* public_key{
            GetMercaturaPQPSBTPublicKey(
                psbt.inputs.at(0))
        };

        const auto* signature{
            GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0))
        };

        BOOST_REQUIRE(public_key);
        BOOST_REQUIRE(signature);

        BOOST_CHECK_EQUAL(
            public_key->size(),
            MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

        BOOST_CHECK_EQUAL(
            signature->size(),
            MERCATURA_MLDSA65_SIGNATURE_SIZE);

        BOOST_CHECK(
            *public_key ==
            std::vector<unsigned char>(
                key_a.public_key.begin(),
                key_a.public_key.end()));

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_witness.IsNull());
    }

    // ========================================================
    // sign=true, finalize=true:
    // create and finalize the exact native PQ witness.
    //
    // n_signed must remain exactly one; the descriptor SPKM
    // must not double-count this input.
    // ========================================================

    {
        auto psbt{
            make_psbt(
                {
                    {
                        20000,
                        key_a.commitment
                    }
                },
                {10000})
        };

        bool complete{false};
        size_t n_signed{0};

        const auto error{
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/true)
        };

        BOOST_CHECK(
            !error.has_value());

        BOOST_CHECK(
            complete);

        BOOST_CHECK_EQUAL(
            n_signed,
            1U);

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_sig.empty());

        BOOST_REQUIRE_EQUAL(
            psbt.inputs.at(0)
                .final_script_witness.stack.size(),
            2U);

        BOOST_CHECK_EQUAL(
            psbt.inputs.at(0)
                .final_script_witness.stack.at(0).size(),
            MERCATURA_MLDSA65_SIGNATURE_SIZE);

        BOOST_CHECK_EQUAL(
            psbt.inputs.at(0)
                .final_script_witness.stack.at(1).size(),
            MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

        const auto* public_key{
            GetMercaturaPQPSBTPublicKey(
                psbt.inputs.at(0))
        };

        const auto* signature{
            GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0))
        };

        BOOST_REQUIRE(public_key);
        BOOST_REQUIRE(signature);

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_witness.stack.at(0) ==
            *signature);

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_witness.stack.at(1) ==
            *public_key);
    }

    // ========================================================
    // Unowned native PQ input:
    // leave it incomplete rather than treating it as a wallet
    // error.
    // ========================================================

    {
        auto psbt{
            make_psbt(
                {
                    {
                        20000,
                        unowned_key.commitment
                    }
                },
                {10000})
        };

        bool complete{true};
        size_t n_signed{99};

        const auto error{
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/true)
        };

        BOOST_CHECK(
            !error.has_value());

        BOOST_CHECK(
            !complete);

        BOOST_CHECK_EQUAL(
            n_signed,
            0U);

        BOOST_CHECK(
            GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0)) ==
            nullptr);

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_witness.IsNull());
    }

    // ========================================================
    // Two wallet-owned PQ inputs:
    // both are signed against one transaction-wide PQAuth
    // context.
    // ========================================================

    {
        auto psbt{
            make_psbt(
                {
                    {
                        25000,
                        key_a.commitment
                    },
                    {
                        30000,
                        key_b.commitment
                    }
                },
                {
                    30000,
                    15000
                })
        };

        const CAmount output_0{
            psbt.tx->vout.at(0).nValue
        };

        const CAmount output_1{
            psbt.tx->vout.at(1).nValue
        };

        const CScript output_0_script{
            psbt.tx->vout.at(0).scriptPubKey
        };

        const CScript output_1_script{
            psbt.tx->vout.at(1).scriptPubKey
        };

        bool complete{false};
        size_t n_signed{0};

        const auto error{
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/true)
        };

        BOOST_CHECK(
            !error.has_value());

        BOOST_CHECK(
            complete);

        BOOST_CHECK_EQUAL(
            n_signed,
            2U);

        BOOST_REQUIRE_EQUAL(
            psbt.inputs.at(0)
                .final_script_witness.stack.size(),
            2U);

        BOOST_REQUIRE_EQUAL(
            psbt.inputs.at(1)
                .final_script_witness.stack.size(),
            2U);

        // Signing must not alter batching/multi-output semantics.
        BOOST_REQUIRE_EQUAL(
            psbt.tx->vout.size(),
            2U);

        BOOST_CHECK_EQUAL(
            psbt.tx->vout.at(0).nValue,
            output_0);

        BOOST_CHECK_EQUAL(
            psbt.tx->vout.at(1).nValue,
            output_1);

        BOOST_CHECK(
            psbt.tx->vout.at(0).scriptPubKey ==
            output_0_script);

        BOOST_CHECK(
            psbt.tx->vout.at(1).scriptPubKey ==
            output_1_script);
    }

    // ========================================================
    // Mixed owned + unowned PQ inputs:
    // sign only what this wallet controls.
    // ========================================================

    {
        auto psbt{
            make_psbt(
                {
                    {
                        25000,
                        key_a.commitment
                    },
                    {
                        30000,
                        unowned_key.commitment
                    }
                },
                {45000})
        };

        bool complete{true};
        size_t n_signed{0};

        const auto error{
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/true)
        };

        BOOST_CHECK(
            !error.has_value());

        BOOST_CHECK(
            !complete);

        BOOST_CHECK_EQUAL(
            n_signed,
            1U);

        BOOST_REQUIRE_EQUAL(
            psbt.inputs.at(0)
                .final_script_witness.stack.size(),
            2U);

        BOOST_CHECK(
            psbt.inputs.at(1)
                .final_script_witness.IsNull());

        BOOST_CHECK(
            GetMercaturaPQPSBTSignature(
                psbt.inputs.at(1)) ==
            nullptr);
    }

    // ========================================================
    // Wallet knows the locator but has no usable master seed:
    // signing must fail closed and must not partially modify
    // the caller's PSBT.
    // ========================================================

    {
        CWallet no_seed_wallet{
            m_node.chain.get(),
            "",
            CreateMockableWalletDatabase()
        };

        {
            LOCK(no_seed_wallet.cs_wallet);

            BOOST_REQUIRE(
                no_seed_wallet.LoadMercaturaPQState(
                    state));

            BOOST_REQUIRE(
                no_seed_wallet.LoadMercaturaPQKeyLocator(
                    key_a.commitment,
                    key_a.locator));
        }

        auto psbt{
            make_psbt(
                {
                    {
                        20000,
                        key_a.commitment
                    }
                },
                {10000})
        };

        bool complete{true};
        size_t n_signed{0};

        const auto error{
            no_seed_wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/true)
        };

        BOOST_REQUIRE(
            error.has_value());

        BOOST_CHECK(
            *error ==
            PSBTError::INCOMPLETE);

        BOOST_CHECK_EQUAL(
            n_signed,
            0U);

        BOOST_CHECK(
            GetMercaturaPQPSBTPublicKey(
                psbt.inputs.at(0)) ==
            nullptr);

        BOOST_CHECK(
            GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0)) ==
            nullptr);

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_witness.IsNull());
    }

    // ========================================================
    // Offline PSBT workflow:
    //
    // wallet signer -> proprietary PQ fields -> serialize ->
    // deserialize -> independent finalizer/extractor.
    // ========================================================

    {
        auto psbt{
            make_psbt(
                {
                    {
                        25000,
                        key_a.commitment
                    }
                },
                {
                    12000,
                    8000
                })
        };

        const auto original_outputs{
            psbt.tx->vout
        };

        bool complete{true};
        size_t n_signed{0};

        const auto error{
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/false)
        };

        BOOST_CHECK(
            !error.has_value());

        BOOST_CHECK(
            !complete);

        BOOST_CHECK_EQUAL(
            n_signed,
            1U);

        BOOST_REQUIRE(
            GetMercaturaPQPSBTPublicKey(
                psbt.inputs.at(0)));

        BOOST_REQUIRE(
            GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0)));

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_witness.IsNull());

        DataStream encoded;
        encoded << psbt;

        PartiallySignedTransaction decoded;
        encoded >> decoded;

        BOOST_REQUIRE(
            GetMercaturaPQPSBTPublicKey(
                decoded.inputs.at(0)));

        BOOST_REQUIRE(
            GetMercaturaPQPSBTSignature(
                decoded.inputs.at(0)));

        BOOST_CHECK(
            decoded.inputs.at(0)
                .final_script_witness.IsNull());

        CMutableTransaction extracted;

        BOOST_REQUIRE(
            FinalizeAndExtractPSBT(
                decoded,
                extracted));

        BOOST_REQUIRE_EQUAL(
            extracted.vin.at(0)
                .scriptWitness.stack.size(),
            2U);

        BOOST_CHECK(
            extracted.vin.at(0)
                .scriptSig.empty());

        BOOST_REQUIRE_EQUAL(
            extracted.vout.size(),
            original_outputs.size());

        for (size_t i = 0;
             i < original_outputs.size();
             ++i) {
            BOOST_CHECK(
                extracted.vout.at(i) ==
                original_outputs.at(i));
        }
    }

    // ========================================================
    // Existing valid PQ authorization:
    //
    // FillPSBT must validate/reuse it rather than generating a
    // second randomized ML-DSA signature or double-counting it.
    // ========================================================

    {
        auto psbt{
            make_psbt(
                {
                    {
                        20000,
                        key_a.commitment
                    }
                },
                {10000})
        };

        bool complete{true};
        size_t n_signed{0};

        auto error{
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/false)
        };

        BOOST_REQUIRE(
            !error.has_value());

        BOOST_REQUIRE_EQUAL(
            n_signed,
            1U);

        const auto* first_public_key{
            GetMercaturaPQPSBTPublicKey(
                psbt.inputs.at(0))
        };

        const auto* first_signature{
            GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0))
        };

        BOOST_REQUIRE(first_public_key);
        BOOST_REQUIRE(first_signature);

        const std::vector<unsigned char>
            saved_public_key{
                *first_public_key
            };

        const std::vector<unsigned char>
            saved_signature{
                *first_signature
            };

        complete = true;
        n_signed = 99;

        error =
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/false);

        BOOST_CHECK(
            !error.has_value());

        BOOST_CHECK(
            !complete);

        // Authorization already existed, so this invocation created
        // no new signature.
        BOOST_CHECK_EQUAL(
            n_signed,
            0U);

        BOOST_REQUIRE(
            GetMercaturaPQPSBTPublicKey(
                psbt.inputs.at(0)));

        BOOST_REQUIRE(
            GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0)));

        BOOST_CHECK(
            *GetMercaturaPQPSBTPublicKey(
                psbt.inputs.at(0)) ==
            saved_public_key);

        BOOST_CHECK(
            *GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0)) ==
            saved_signature);

        // The same existing authorization may later be finalized
        // without generating another signature.
        complete = false;
        n_signed = 99;

        error =
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/true);

        BOOST_CHECK(
            !error.has_value());

        BOOST_CHECK(
            complete);

        BOOST_CHECK_EQUAL(
            n_signed,
            0U);

        BOOST_CHECK(
            *GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0)) ==
            saved_signature);

        BOOST_REQUIRE_EQUAL(
            psbt.inputs.at(0)
                .final_script_witness.stack.size(),
            2U);

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_witness.stack.at(0) ==
            saved_signature);

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_witness.stack.at(1) ==
            saved_public_key);
    }

    // ========================================================
    // Transaction-wide missing UTXO context:
    //
    // Even when input 0 belongs to this wallet, PQAuth v1 must
    // not sign it if another spent output is unavailable because
    // the authorization digest commits to every spent output.
    //
    // Failure must be atomic: no proprietary authorization data
    // or final witness may leak into the caller's PSBT.
    // ========================================================

    {
        auto psbt{
            make_psbt(
                {
                    {
                        25000,
                        key_a.commitment
                    },
                    {
                        30000,
                        key_b.commitment
                    }
                },
                {45000})
        };

        const CTxOut original_input_0{
            psbt.inputs.at(0)
                .witness_utxo
        };

        // Remove the second input's spent-output information.
        psbt.inputs.at(1)
            .witness_utxo.SetNull();

        bool complete{true};
        size_t n_signed{0};

        const auto error{
            wallet.FillPSBT(
                psbt,
                complete,
                std::nullopt,
                /*sign=*/true,
                /*bip32derivs=*/false,
                &n_signed,
                /*finalize=*/true)
        };

        BOOST_REQUIRE(
            error.has_value());

        BOOST_CHECK(
            *error ==
            PSBTError::MISSING_INPUTS);

        BOOST_CHECK_EQUAL(
            n_signed,
            0U);

        // Input 0 was signable in isolation, but must remain wholly
        // untouched because transaction-wide context was incomplete.
        BOOST_CHECK(
            GetMercaturaPQPSBTPublicKey(
                psbt.inputs.at(0)) ==
            nullptr);

        BOOST_CHECK(
            GetMercaturaPQPSBTSignature(
                psbt.inputs.at(0)) ==
            nullptr);

        BOOST_CHECK(
            psbt.inputs.at(0)
                .final_script_witness.IsNull());

        BOOST_CHECK(
            psbt.inputs.at(0)
                .witness_utxo ==
            original_input_0);

        BOOST_CHECK(
            psbt.inputs.at(1)
                .final_script_witness.IsNull());
    }

    memory_cleanse(
        master_seed.data(),
        master_seed.size());
}

BOOST_AUTO_TEST_CASE(mercatura_pq_psbt_witness_utxo_pruning)
{
    std::array<
        unsigned char,
        MERCATURA_MLDSA65_SEED_SIZE>
        seed{};

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_RANDOM_SIZE>
        randomness{};

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE>
        public_key{};

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_SECRET_KEY_SIZE>
        secret_key{};

    std::array<
        unsigned char,
        MERCATURA_MLDSA65_SIGNATURE_SIZE>
        signature{};

    for (size_t i = 0; i < seed.size(); ++i) {
        seed[i] =
            static_cast<unsigned char>(
                0x70 + i);
    }

    BOOST_REQUIRE_EQUAL(
        mercatura_mldsa65_keypair_from_seed(
            public_key.data(),
            public_key.size(),
            secret_key.data(),
            secret_key.size(),
            seed.data(),
            seed.size()),
        1);

    MercaturaPQKeyCommitment commitment{};

    BOOST_REQUIRE(
        ComputeMercaturaPQKeyCommitmentV1(
            commitment,
            public_key));

    const std::vector<unsigned char> program{
        commitment.begin(),
        commitment.end()
    };

    CScript pq_script;
    pq_script << OP_2 << program;

    // Create a real previous transaction so non_witness_utxo is
    // internally consistent with the spending transaction's prevout.
    CMutableTransaction funding_mut;
    funding_mut.version = 2;
    funding_mut.vin.emplace_back(
        COutPoint{Txid{}, 0});

    funding_mut.vout.emplace_back(
        25000,
        pq_script);

    const CTransactionRef funding{
        MakeTransactionRef(
            funding_mut)
    };

    CMutableTransaction spending;
    spending.version = 2;
    spending.vin.emplace_back(
        COutPoint{
            funding->GetHash(),
            0});

    CScript recipient;
    recipient << OP_TRUE;

    spending.vout.emplace_back(
        15000,
        recipient);

    PartiallySignedTransaction psbt{
        spending
    };

    // Supply both forms initially. For native PQ witness-v2,
    // witness_utxo must be sufficient after the redundant full
    // previous transaction is pruned.
    psbt.inputs.at(0).non_witness_utxo =
        funding;

    psbt.inputs.at(0).witness_utxo =
        funding->vout.at(0);

    const PrecomputedTransactionData txdata{
        PrecomputePSBTData(
            psbt)
    };

    BOOST_REQUIRE(
        txdata.m_pq_ready);

    MercaturaPQHash384 digest{};

    BOOST_REQUIRE(
        ComputeMercaturaPQAuthDigestV1(
            digest,
            *psbt.tx,
            0,
            Params().GetConsensus().hashGenesisBlock,
            txdata));

    static constexpr char PQ_CONTEXT[] =
        "Mercatura/PQAuth/v1";

    BOOST_REQUIRE_EQUAL(
        mercatura_mldsa65_sign(
            signature.data(),
            signature.size(),
            digest.data(),
            digest.size(),
            reinterpret_cast<const uint8_t*>(
                PQ_CONTEXT),
            sizeof(PQ_CONTEXT) - 1,
            randomness.data(),
            randomness.size(),
            secret_key.data(),
            secret_key.size()),
        1);

    BOOST_REQUIRE(
        SetMercaturaPQPSBTPublicKey(
            psbt.inputs.at(0),
            public_key));

    BOOST_REQUIRE(
        SetMercaturaPQPSBTSignature(
            psbt.inputs.at(0),
            signature));

    BOOST_REQUIRE(
        FinalizePSBT(
            psbt));

    BOOST_REQUIRE(
        psbt.inputs.at(0)
            .non_witness_utxo);

    BOOST_REQUIRE(
        !psbt.inputs.at(0)
            .witness_utxo.IsNull());

    // PQ is native witness-v2, so the full previous transaction is
    // redundant once the exact spent output is present.
    RemoveUnnecessaryTransactions(
        psbt);

    BOOST_CHECK(
        !psbt.inputs.at(0)
            .non_witness_utxo);

    BOOST_REQUIRE(
        !psbt.inputs.at(0)
            .witness_utxo.IsNull());

    BOOST_CHECK_EQUAL(
        psbt.inputs.at(0)
            .witness_utxo.nValue,
        funding->vout.at(0).nValue);

    BOOST_CHECK(
        psbt.inputs.at(0)
            .witness_utxo.scriptPubKey ==
        funding->vout.at(0).scriptPubKey);

    // Recompute everything using witness_utxo only.
    const PrecomputedTransactionData pruned_txdata{
        PrecomputePSBTData(
            psbt)
    };

    BOOST_REQUIRE(
        pruned_txdata.m_pq_ready);

    BOOST_CHECK(
        PSBTInputSignedAndVerified(
            psbt,
            0,
            &pruned_txdata));

    // Serialize and reload the pruned PSBT. This proves that no
    // hidden dependency on non_witness_utxo survives in memory.
    DataStream encoded;
    encoded << psbt;

    PartiallySignedTransaction decoded;
    encoded >> decoded;

    BOOST_CHECK(
        !decoded.inputs.at(0)
            .non_witness_utxo);

    BOOST_REQUIRE(
        !decoded.inputs.at(0)
            .witness_utxo.IsNull());

    const PrecomputedTransactionData decoded_txdata{
        PrecomputePSBTData(
            decoded)
    };

    BOOST_REQUIRE(
        decoded_txdata.m_pq_ready);

    BOOST_CHECK(
        PSBTInputSignedAndVerified(
            decoded,
            0,
            &decoded_txdata));

    // Finalize-and-extract must remain valid after pruning and
    // serialization round trip.
    CMutableTransaction extracted;

    BOOST_REQUIRE(
        FinalizeAndExtractPSBT(
            decoded,
            extracted));

    BOOST_REQUIRE_EQUAL(
        extracted.vin.at(0)
            .scriptWitness.stack.size(),
        2U);

    BOOST_CHECK_EQUAL(
        extracted.vin.at(0)
            .scriptWitness.stack.at(0).size(),
        MERCATURA_MLDSA65_SIGNATURE_SIZE);

    BOOST_CHECK_EQUAL(
        extracted.vin.at(0)
            .scriptWitness.stack.at(1).size(),
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

    memory_cleanse(
        secret_key.data(),
        secret_key.size());
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
