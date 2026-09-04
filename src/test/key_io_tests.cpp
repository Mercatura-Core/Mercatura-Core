// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/data/key_io_invalid.json.h>
#include <test/data/key_io_valid.json.h>

#include <crypto/mercatura_mldsa.h>
#include <crypto/mercatura_pqderive.h>
#include <crypto/mercatura_pqkey.h>
#include <crypto/sha256.h>
#include <key.h>
#include <key_io.h>
#include <script/script.h>
#include <test/util/json.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>

BOOST_FIXTURE_TEST_SUITE(key_io_tests, BasicTestingSetup)

// Goal: check that parsed keys match test payload
BOOST_AUTO_TEST_CASE(key_io_valid_parse)
{
    UniValue tests = read_json(json_tests::key_io_valid);
    CKey privkey;
    CTxDestination destination;
    SelectParams(ChainType::MAIN);

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 3) { // Allow for extra stuff (useful for comments)
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get_str();
        const std::vector<std::byte> exp_payload{ParseHex<std::byte>(test[1].get_str())};
        const UniValue &metadata = test[2].get_obj();
        bool isPrivkey = metadata.find_value("isPrivkey").get_bool();
        SelectParams(ChainTypeFromString(metadata.find_value("chain").get_str()).value());
        bool try_case_flip = metadata.find_value("tryCaseFlip").isNull() ? false : metadata.find_value("tryCaseFlip").get_bool();
        if (isPrivkey) {
            bool isCompressed = metadata.find_value("isCompressed").get_bool();
            // Must be valid private key
            privkey = DecodeSecret(exp_base58string);
            BOOST_CHECK_MESSAGE(privkey.IsValid(), "!IsValid:" + strTest);
            BOOST_CHECK_MESSAGE(privkey.IsCompressed() == isCompressed, "compressed mismatch:" + strTest);
            BOOST_CHECK_MESSAGE(std::ranges::equal(privkey, exp_payload), "key mismatch:" + strTest);

            // Private key must be invalid public key
            destination = DecodeDestination(exp_base58string);
            BOOST_CHECK_MESSAGE(!IsValidDestination(destination), "IsValid privkey as pubkey:" + strTest);
        } else {
            // Must be valid public key
            destination = DecodeDestination(exp_base58string);
            CScript script = GetScriptForDestination(destination);
            BOOST_CHECK_MESSAGE(IsValidDestination(destination), "!IsValid:" + strTest);
            BOOST_CHECK_EQUAL(HexStr(script), HexStr(exp_payload));

            // Try flipped case version
            for (char& c : exp_base58string) {
                if (c >= 'a' && c <= 'z') {
                    c = (c - 'a') + 'A';
                } else if (c >= 'A' && c <= 'Z') {
                    c = (c - 'A') + 'a';
                }
            }
            destination = DecodeDestination(exp_base58string);
            BOOST_CHECK_MESSAGE(IsValidDestination(destination) == try_case_flip, "!IsValid case flipped:" + strTest);
            if (IsValidDestination(destination)) {
                script = GetScriptForDestination(destination);
                BOOST_CHECK_EQUAL(HexStr(script), HexStr(exp_payload));
            }

            // Public key must be invalid private key
            privkey = DecodeSecret(exp_base58string);
            BOOST_CHECK_MESSAGE(!privkey.IsValid(), "IsValid pubkey as privkey:" + strTest);
        }
    }
}

// Goal: check that generated keys match test vectors
BOOST_AUTO_TEST_CASE(key_io_valid_gen)
{
    UniValue tests = read_json(json_tests::key_io_valid);

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 3) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get_str();
        std::vector<unsigned char> exp_payload = ParseHex(test[1].get_str());
        const UniValue &metadata = test[2].get_obj();
        bool isPrivkey = metadata.find_value("isPrivkey").get_bool();
        SelectParams(ChainTypeFromString(metadata.find_value("chain").get_str()).value());
        if (isPrivkey) {
            bool isCompressed = metadata.find_value("isCompressed").get_bool();
            CKey key;
            key.Set(exp_payload.begin(), exp_payload.end(), isCompressed);
            assert(key.IsValid());
            BOOST_CHECK_MESSAGE(EncodeSecret(key) == exp_base58string, "result mismatch: " + strTest);
        } else {
            CTxDestination dest;
            CScript exp_script(exp_payload.begin(), exp_payload.end());
            BOOST_CHECK(ExtractDestination(exp_script, dest));
            std::string address = EncodeDestination(dest);

            BOOST_CHECK_EQUAL(address, exp_base58string);
        }
    }

    SelectParams(ChainType::MAIN);
}


// Goal: check that base58 parsing code is robust against a variety of corrupted data
BOOST_AUTO_TEST_CASE(key_io_invalid)
{
    UniValue tests = read_json(json_tests::key_io_invalid); // Negative testcases
    CKey privkey;
    CTxDestination destination;

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 1) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get_str();

        // must be invalid as public and as private key
        for (const auto& chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::SIGNET, ChainType::REGTEST}) {
            SelectParams(chain);
            destination = DecodeDestination(exp_base58string);
            BOOST_CHECK_MESSAGE(!IsValidDestination(destination), "IsValid pubkey in mainnet:" + strTest);
            privkey = DecodeSecret(exp_base58string);
            BOOST_CHECK_MESSAGE(!privkey.IsValid(), "IsValid privkey in mainnet:" + strTest);
        }
    }
}


BOOST_AUTO_TEST_CASE(mercatura_pq_v2_address_roundtrip)
{
    // Deterministic 32-byte PQ commitment: 00 01 02 ... 1f.
    WitnessV2MercaturaPQ pq;
    for (size_t i = 0; i < pq.size(); ++i) {
        pq.begin()[i] = static_cast<unsigned char>(i);
    }

    const CTxDestination pq_dest{pq};
    const std::vector<unsigned char> program{pq.begin(), pq.end()};
    const CScript expected_script{CScript() << OP_2 << program};

    const auto check_chain = [&](ChainType chain, const std::string& expected_address) {
        SelectParams(chain);

        BOOST_CHECK(IsValidDestination(pq_dest));

        const std::string address{EncodeDestination(pq_dest)};
        BOOST_CHECK_EQUAL(address, expected_address);

        const CTxDestination decoded{DecodeDestination(address)};
        BOOST_CHECK(decoded == pq_dest);
        BOOST_CHECK(IsValidDestination(decoded));

        const CScript script{GetScriptForDestination(decoded)};
        BOOST_CHECK_EQUAL(HexStr(script), HexStr(expected_script));

        CTxDestination extracted;
        BOOST_CHECK(ExtractDestination(script, extracted));
        BOOST_CHECK(extracted == pq_dest);

        BOOST_CHECK_EQUAL(EncodeDestination(extracted), expected_address);
    };

    check_chain(
        ChainType::MAIN,
        "mca1zqqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0srmet37");

    check_chain(
        ChainType::TESTNET,
        "tmca1zqqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0sg5a4wt");

    check_chain(
        ChainType::REGTEST,
        "mcrt1zqqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0s2m3gwg");

    SelectParams(ChainType::MAIN);

    // Witness version 2 is permanently assigned to Mercatura PQ v1.
    // Generic WitnessUnknown must never represent or encode it.
    const CTxDestination unknown_v2{
        WitnessUnknown{2, program}
    };
    BOOST_CHECK(!IsValidDestination(unknown_v2));
    BOOST_CHECK(EncodeDestination(unknown_v2).empty());
    BOOST_CHECK(GetScriptForDestination(unknown_v2).empty());

    // Valid Bech32m addresses carrying v2 programs of the wrong size
    // must not decode as destinations.
    const std::string v2_31_bytes{
        "mca1zqqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc5xrcp2"
    };
    const std::string v2_33_bytes{
        "mca1zqqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jq9gzc8t"
    };

    BOOST_CHECK(!IsValidDestination(DecodeDestination(v2_31_bytes)));
    BOOST_CHECK(!IsValidDestination(DecodeDestination(v2_33_bytes)));

    // Witness v2 must use Bech32m, never the original Bech32 checksum.
    const std::string v2_wrong_bech32_checksum{
        "mca1zqqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0sk8f85u"
    };
    BOOST_CHECK(!IsValidDestination(
        DecodeDestination(v2_wrong_bech32_checksum)));

    // Network HRPs must remain isolated.
    const std::string main_address{
        "mca1zqqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0srmet37"
    };

    SelectParams(ChainType::TESTNET);
    BOOST_CHECK(!IsValidDestination(DecodeDestination(main_address)));

    SelectParams(ChainType::MAIN);
}


BOOST_AUTO_TEST_CASE(mercatura_pq_deterministic_key_address_pipeline)
{
    std::array<unsigned char, MERCATURA_PQ_MASTER_SEED_SIZE> master_seed{};
    for (size_t i = 0; i < master_seed.size(); ++i) {
        master_seed[i] = static_cast<unsigned char>(i);
    }

    std::array<unsigned char, 32> genesis_bytes{};
    for (size_t i = 0; i < genesis_bytes.size(); ++i) {
        genesis_bytes[i] = static_cast<unsigned char>(0x20 + i);
    }

    const uint256 genesis_hash{
        std::span<const unsigned char>{genesis_bytes}
    };

    const auto child_seed = DeriveMercaturaPQChildSeed(
        master_seed,
        genesis_hash,
        /*account=*/0,
        /*branch=*/0,
        /*index=*/0);

    BOOST_REQUIRE(child_seed.has_value());

    BOOST_CHECK_EQUAL(
        HexStr(*child_seed),
        "0be624be1e03a6fd1e078d25352115a74aca5696e332ab7e23e5dd019b784484");

    std::array<unsigned char, MERCATURA_MLDSA65_PUBLIC_KEY_SIZE> public_key{};
    std::array<unsigned char, MERCATURA_MLDSA65_SECRET_KEY_SIZE> secret_key{};

    BOOST_REQUIRE_EQUAL(
        mercatura_mldsa65_keypair_from_seed(
            public_key.data(),
            public_key.size(),
            secret_key.data(),
            secret_key.size(),
            child_seed->data(),
            child_seed->size()),
        1);

    std::array<unsigned char, CSHA256::OUTPUT_SIZE> public_key_hash{};
    CSHA256()
        .Write(public_key.data(), public_key.size())
        .Finalize(public_key_hash.data());

    MercaturaPQKeyCommitment commitment{};
    BOOST_REQUIRE(
        ComputeMercaturaPQKeyCommitmentV1(
            commitment,
            public_key));

    WitnessV2MercaturaPQ pq_destination;
    std::copy(
        commitment.begin(),
        commitment.end(),
        pq_destination.begin());

    const CTxDestination destination{pq_destination};

    BOOST_CHECK(IsValidDestination(destination));

    const CScript expected_script{
        CScript()
            << OP_2
            << std::vector<unsigned char>(
                   commitment.begin(),
                   commitment.end())
    };

    BOOST_CHECK_EQUAL(
        HexStr(GetScriptForDestination(destination)),
        HexStr(expected_script));

    CTxDestination extracted;
    BOOST_REQUIRE(
        ExtractDestination(
            expected_script,
            extracted));

    BOOST_CHECK(extracted == destination);

    BOOST_CHECK_EQUAL(
        HexStr(public_key_hash),
        "cc1b39a4364121e44178dfaa0d23d4c873a4781d4d115690290726e7f06d8210");

    BOOST_CHECK_EQUAL(
        HexStr(commitment),
        "3a3f04c02668cf6bb22235e44d9ae81d6ffebbc53a313e4e94c248666abb89c0");

    SelectParams(ChainType::MAIN);
    const std::string main_address{
        EncodeDestination(destination)
    };

    BOOST_CHECK_EQUAL(
        main_address,
        "mca1z8glsfspxdr8khv3zxhjymxhgr4hlaw798gcnun55cfyxv64m38qqvr8rrn");

    BOOST_CHECK(
        DecodeDestination(main_address) == destination);

    SelectParams(ChainType::TESTNET);
    const std::string testnet_address{
        EncodeDestination(destination)
    };

    BOOST_CHECK_EQUAL(
        testnet_address,
        "tmca1z8glsfspxdr8khv3zxhjymxhgr4hlaw798gcnun55cfyxv64m38qq8vraux");

    BOOST_CHECK(
        DecodeDestination(testnet_address) == destination);

    SelectParams(ChainType::REGTEST);
    const std::string regtest_address{
        EncodeDestination(destination)
    };

    BOOST_CHECK_EQUAL(
        regtest_address,
        "mcrt1z8glsfspxdr8khv3zxhjymxhgr4hlaw798gcnun55cfyxv64m38qq9r0qu9");

    BOOST_CHECK(
        DecodeDestination(regtest_address) == destination);

    // All three networks must produce distinct address strings.
    BOOST_CHECK(main_address != testnet_address);
    BOOST_CHECK(main_address != regtest_address);
    BOOST_CHECK(testnet_address != regtest_address);

    SelectParams(ChainType::MAIN);
}

BOOST_AUTO_TEST_SUITE_END()
