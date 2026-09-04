// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <bench/bench.h>
#include <crypto/mercatura_mldsa.h>
#include <crypto/mercatura_pqkey.h>
#include <key.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <streams.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

static constexpr char PQ_CONTEXT[] = "Mercatura/PQAuth/v1";

struct RawMLDSAFixture
{
    std::array<unsigned char, MERCATURA_MLDSA65_SEED_SIZE> seed{};
    std::array<unsigned char, MERCATURA_MLDSA65_RANDOM_SIZE> randomness{};
    std::array<unsigned char, MERCATURA_MLDSA65_PUBLIC_KEY_SIZE> public_key{};
    std::array<unsigned char, MERCATURA_MLDSA65_SECRET_KEY_SIZE> secret_key{};
    std::array<unsigned char, MERCATURA_MLDSA65_SIGNATURE_SIZE> signature{};
    MercaturaPQHash384 message{};

    RawMLDSAFixture()
    {
        for (size_t i = 0; i < seed.size(); ++i) {
            seed[i] = static_cast<unsigned char>(i);
        }

        for (size_t i = 0; i < message.size(); ++i) {
            message[i] = static_cast<unsigned char>(0x80U + i);
        }

        assert(
            mercatura_mldsa65_keypair_from_seed(
                public_key.data(),
                public_key.size(),
                secret_key.data(),
                secret_key.size(),
                seed.data(),
                seed.size()));

        assert(
            mercatura_mldsa65_sign(
                signature.data(),
                signature.size(),
                message.data(),
                message.size(),
                reinterpret_cast<const uint8_t*>(PQ_CONTEXT),
                sizeof(PQ_CONTEXT) - 1,
                randomness.data(),
                randomness.size(),
                secret_key.data(),
                secret_key.size()));
    }
};

struct FullAuthorizationFixture
{
    CMutableTransaction tx;
    PrecomputedTransactionData txdata;
    uint256 genesis_hash{};

    std::array<unsigned char, MERCATURA_MLDSA65_SEED_SIZE> seed{};
    std::array<unsigned char, MERCATURA_MLDSA65_RANDOM_SIZE> randomness{};
    std::array<unsigned char, MERCATURA_MLDSA65_PUBLIC_KEY_SIZE> public_key{};
    std::array<unsigned char, MERCATURA_MLDSA65_SECRET_KEY_SIZE> secret_key{};
    std::array<unsigned char, MERCATURA_MLDSA65_SIGNATURE_SIZE> signature{};

    FullAuthorizationFixture()
    {
        const auto txhex = ParseHex(
            "02000000"
            "02"
            "000102030405060708090a0b0c0d0e0f"
            "101112131415161718191a1b1c1d1e1f"
            "01000000"
            "00"
            "feffffff"
            "202122232425262728292a2b2c2d2e2f"
            "303132333435363738393a3b3c3d3e3f"
            "02000000"
            "00"
            "78563412"
            "02"
            "3930000000000000"
            "01"
            "51"
            "3209010000000000"
            "03"
            "6a01ff"
            "00000000");

        SpanReader{txhex} >> TX_WITH_WITNESS(tx);

        for (size_t i = 0; i < seed.size(); ++i) {
            seed[i] = static_cast<unsigned char>(i);
        }

        assert(
            mercatura_mldsa65_keypair_from_seed(
                public_key.data(),
                public_key.size(),
                secret_key.data(),
                secret_key.size(),
                seed.data(),
                seed.size()));

        MercaturaPQKeyCommitment commitment{};

        assert(
            ComputeMercaturaPQKeyCommitmentV1(
                commitment,
                public_key));

        const std::vector<unsigned char> program{
            commitment.begin(),
            commitment.end()
        };

        CScript pq_script;
        pq_script << OP_2 << program;

        CScript other_spent_script;
        other_spent_script << OP_TRUE;

        // Ensure PrecomputedTransactionData recognizes this input as
        // a Mercatura PQ witness spend before the real witness exists.
        tx.vin[1].scriptWitness.stack = {
            std::vector<unsigned char>{0x00}
        };

        std::vector<CTxOut> spent_outputs;
        spent_outputs.emplace_back(
            11111,
            other_spent_script);
        spent_outputs.emplace_back(
            22222,
            pq_script);

        txdata.Init(
            tx,
            std::move(spent_outputs));

        assert(txdata.m_pq_ready);

        const auto genesis_bytes = ParseHex(
            "404142434445464748494a4b4c4d4e4f"
            "505152535455565758595a5b5c5d5e5f");

        genesis_hash = uint256{
            std::span<const unsigned char>{
                genesis_bytes
            }
        };

        MercaturaPQHash384 digest{};

        assert(
            ComputeMercaturaPQAuthDigestV1(
                digest,
                tx,
                1,
                genesis_hash,
                txdata));

        assert(
            mercatura_mldsa65_sign(
                signature.data(),
                signature.size(),
                digest.data(),
                digest.size(),
                reinterpret_cast<const uint8_t*>(PQ_CONTEXT),
                sizeof(PQ_CONTEXT) - 1,
                randomness.data(),
                randomness.size(),
                secret_key.data(),
                secret_key.size()));
    }
};

static void MercaturaPQ_KeyCommitment(
    benchmark::Bench& bench)
{
    RawMLDSAFixture fixture;
    MercaturaPQKeyCommitment commitment{};

    bench.run([&] {
        const bool success{
            ComputeMercaturaPQKeyCommitmentV1(
                commitment,
                fixture.public_key)
        };

        assert(success);
    });
}

static void MercaturaPQ_MLDSA65_VerifyValid(
    benchmark::Bench& bench)
{
    RawMLDSAFixture fixture;

    bench.run([&] {
        const int success{
            mercatura_mldsa65_verify(
                fixture.signature.data(),
                fixture.signature.size(),
                fixture.message.data(),
                fixture.message.size(),
                reinterpret_cast<const uint8_t*>(PQ_CONTEXT),
                sizeof(PQ_CONTEXT) - 1,
                fixture.public_key.data(),
                fixture.public_key.size())
        };

        assert(success);
    });
}

static void MercaturaPQ_MLDSA65_VerifyInvalid(
    benchmark::Bench& bench)
{
    RawMLDSAFixture fixture;

    // Keep the signature structurally valid and change the signed
    // message instead. This avoids benchmarking only a cheap malformed
    // signature parser rejection.
    auto wrong_message = fixture.message;
    wrong_message[0] ^= 0x01;

    bench.run([&] {
        const int success{
            mercatura_mldsa65_verify(
                fixture.signature.data(),
                fixture.signature.size(),
                wrong_message.data(),
                wrong_message.size(),
                reinterpret_cast<const uint8_t*>(PQ_CONTEXT),
                sizeof(PQ_CONTEXT) - 1,
                fixture.public_key.data(),
                fixture.public_key.size())
        };

        assert(!success);
    });
}

static void Reference_ECDSA_Verify(
    benchmark::Bench& bench)
{
    ECC_Context ecc_context{};

    const auto secret = ParseHex(
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f");

    const uint256 message{
        ParseHex(
            "202122232425262728292a2b2c2d2e2f"
            "303132333435363738393a3b3c3d3e3f")
    };

    CKey key;
    key.Set(
        secret.begin(),
        secret.end(),
        true);

    assert(key.IsValid());

    const CPubKey public_key{
        key.GetPubKey()
    };

    std::vector<unsigned char> signature;

    assert(
        key.Sign(
            message,
            signature));

    assert(
        public_key.Verify(
            message,
            signature));

    bench.run([&] {
        const bool success{
            public_key.Verify(
                message,
                signature)
        };

        assert(success);
    });
}

static void Reference_Schnorr_Verify(
    benchmark::Bench& bench)
{
    ECC_Context ecc_context{};

    const auto secret = ParseHex(
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f");

    const uint256 message{
        ParseHex(
            "202122232425262728292a2b2c2d2e2f"
            "303132333435363738393a3b3c3d3e3f")
    };

    const uint256 aux{
        ParseHex(
            "404142434445464748494a4b4c4d4e4f"
            "505152535455565758595a5b5c5d5e5f")
    };

    CKey key;
    key.Set(
        secret.begin(),
        secret.end(),
        true);

    assert(key.IsValid());

    const XOnlyPubKey public_key{
        key.GetPubKey()
    };

    unsigned char signature[64];

    assert(
        key.SignSchnorr(
            message,
            signature,
            nullptr,
            aux));

    assert(
        public_key.VerifySchnorr(
            message,
            signature));

    bench.run([&] {
        const bool success{
            public_key.VerifySchnorr(
                message,
                signature)
        };

        assert(success);
    });
}

static void MercaturaPQ_FullAuthorization(
    benchmark::Bench& bench)
{
    FullAuthorizationFixture fixture;

    MutableTransactionSignatureChecker checker{
        &fixture.tx,
        1,
        22222,
        fixture.txdata,
        MissingDataBehavior::FAIL,
        std::optional<uint256>{
            fixture.genesis_hash
        }
    };

    bench.run([&] {
        ScriptError error{
            SCRIPT_ERR_UNKNOWN_ERROR
        };

        const bool success{
            checker.CheckMercaturaPQSignature(
                fixture.signature,
                fixture.public_key,
                &error)
        };

        assert(success);
        assert(error == SCRIPT_ERR_OK);
    });
}

} // namespace

BENCHMARK(MercaturaPQ_KeyCommitment);
BENCHMARK(MercaturaPQ_MLDSA65_VerifyValid);
BENCHMARK(MercaturaPQ_MLDSA65_VerifyInvalid);
BENCHMARK(MercaturaPQ_FullAuthorization);
BENCHMARK(Reference_ECDSA_Verify);
BENCHMARK(Reference_Schnorr_Verify);
