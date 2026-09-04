// Copyright (c) 2022-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <wallet/test/util.h>
#include <wallet/walletdb.h>
#include <wallet/wallet.h>
#include <test/util/common.h>
#include <test/util/logging.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

namespace wallet {

BOOST_AUTO_TEST_SUITE(walletload_tests)

class DummyDescriptor final : public Descriptor {
private:
    std::string desc;
public:
    explicit DummyDescriptor(const std::string& descriptor) : desc(descriptor) {};
    ~DummyDescriptor() = default;

    std::string ToString(bool compat_format) const override { return desc; }
    std::optional<OutputType> GetOutputType() const override { return OutputType::UNKNOWN; }

    bool IsRange() const override { return false; }
    bool IsSolvable() const override { return false; }
    bool IsSingleType() const override { return true; }
    bool HavePrivateKeys(const SigningProvider&) const override { return false; }
    bool ToPrivateString(const SigningProvider& provider, std::string& out) const override { return false; }
    bool ToNormalizedString(const SigningProvider& provider, std::string& out, const DescriptorCache* cache = nullptr) const override { return false; }
    bool Expand(int pos, const SigningProvider& provider, std::vector<CScript>& output_scripts, FlatSigningProvider& out, DescriptorCache* write_cache = nullptr) const override { return false; };
    bool ExpandFromCache(int pos, const DescriptorCache& read_cache, std::vector<CScript>& output_scripts, FlatSigningProvider& out) const override { return false; }
    void ExpandPrivate(int pos, const SigningProvider& provider, FlatSigningProvider& out) const override {}
    std::optional<int64_t> ScriptSize() const override { return {}; }
    std::optional<int64_t> MaxSatisfactionWeight(bool) const override { return {}; }
    std::optional<int64_t> MaxSatisfactionElems() const override { return {}; }
    void GetPubKeys(std::set<CPubKey>& pubkeys, std::set<CExtPubKey>& ext_pubs) const override {}
    std::vector<std::string> Warnings() const override { return {}; }
    uint32_t GetMaxKeyExpr() const override { return 0; }
    size_t GetKeyCount() const override { return 0; }
};

BOOST_FIXTURE_TEST_CASE(wallet_load_descriptors, TestingSetup)
{
    bilingual_str _error;
    std::vector<bilingual_str> _warnings;
    std::unique_ptr<WalletDatabase> database = CreateMockableWalletDatabase();
    {
        // Write unknown active descriptor
        WalletBatch batch(*database);
        std::string unknown_desc = "trx(tpubD6NzVbkrYhZ4Y4S7m6Y5s9GD8FqEMBy56AGphZXuagajudVZEnYyBahZMgHNCTJc2at82YX6s8JiL1Lohu5A3v1Ur76qguNH4QVQ7qYrBQx/86'/1'/0'/0/*)#8pn8tzdt";
        WalletDescriptor wallet_descriptor(std::make_shared<DummyDescriptor>(unknown_desc), 0, 0, 0, 0);
        BOOST_CHECK(batch.WriteDescriptor(uint256(), wallet_descriptor));
        BOOST_CHECK(batch.WriteActiveScriptPubKeyMan(static_cast<uint8_t>(OutputType::UNKNOWN), uint256(), false));
    }

    {
        // Now try to load the wallet and verify the error.
        const std::shared_ptr<CWallet> wallet(new CWallet(m_node.chain.get(), "", std::move(database)));
        BOOST_CHECK_EQUAL(wallet->PopulateWalletFromDB(_error, _warnings), DBErrors::UNKNOWN_DESCRIPTOR);
    }

    // Test 2
    // Now write a valid descriptor with an invalid ID.
    // As the software produces another ID for the descriptor, the loading process must be aborted.
    database = CreateMockableWalletDatabase();

    // Verify the error
    bool found = false;
    DebugLogHelper logHelper("The descriptor ID calculated by the wallet differs from the one in DB", [&](const std::string* s) {
        found = true;
        return false;
    });

    {
        // Write valid descriptor with invalid ID
        WalletBatch batch(*database);
        std::string desc = "wpkh([d34db33f/84h/0h/0h]xpub6DJ2dNUysrn5Vt36jH2KLBT2i1auw1tTSSomg8PhqNiUtx8QX2SvC9nrHu81fT41fvDUnhMjEzQgXnQjKEu3oaqMSzhSrHMxyyoEAmUHQbY/0/*)#cjjspncu";
        WalletDescriptor wallet_descriptor(std::make_shared<DummyDescriptor>(desc), 0, 0, 0, 0);
        BOOST_CHECK(batch.WriteDescriptor(uint256::ONE, wallet_descriptor));
    }

    {
        // Now try to load the wallet and verify the error.
        const std::shared_ptr<CWallet> wallet(new CWallet(m_node.chain.get(), "", std::move(database)));
        BOOST_CHECK_EQUAL(wallet->PopulateWalletFromDB(_error, _warnings), DBErrors::CORRUPT);
        BOOST_CHECK(found); // The error must be logged
    }
}


BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_load_failure_matrix, TestingSetup)
{
    const auto load_database =
        [&](std::unique_ptr<WalletDatabase> database) {
            bilingual_str error;
            std::vector<bilingual_str> warnings;

            const std::shared_ptr<CWallet> wallet{
                new CWallet(
                    m_node.chain.get(),
                    "",
                    std::move(database))
            };

            return wallet->PopulateWalletFromDB(
                error,
                warnings);
        };

    const auto valid_state = [] {
        MercaturaPQWalletState state;
        state.account = 7;
        state.next_external_index = 11;
        state.next_internal_index = 13;
        return state;
    };

    const auto valid_seed = [] {
        CKeyingMaterial seed(32);
        for (size_t i = 0; i < seed.size(); ++i) {
            seed[i] = static_cast<unsigned char>(i);
        }
        return seed;
    };

    // A wallet with no PQ records remains valid.
    {
        auto database = CreateMockableWalletDatabase();

        BOOST_CHECK_EQUAL(
            load_database(std::move(database)),
            DBErrors::LOAD_OK);
    }

    // Unsupported PQ scheme version.
    {
        auto database = CreateMockableWalletDatabase();

        MercaturaPQWalletState state = valid_state();
        state.scheme_version =
            MercaturaPQWalletState::SCHEME_VERSION + 1;

        {
            auto batch = database->MakeBatch();

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_STATE,
                    state));

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_SEED,
                    valid_seed()));
        }

        BOOST_CHECK_EQUAL(
            load_database(std::move(database)),
            DBErrors::TOO_NEW);
    }

    // Unsupported PQ derivation version.
    {
        auto database = CreateMockableWalletDatabase();

        MercaturaPQWalletState state = valid_state();
        state.derivation_version =
            MercaturaPQWalletState::DERIVATION_VERSION + 1;

        {
            auto batch = database->MakeBatch();

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_STATE,
                    state));

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_SEED,
                    valid_seed()));
        }

        BOOST_CHECK_EQUAL(
            load_database(std::move(database)),
            DBErrors::TOO_NEW);
    }

    // Plaintext master seed must be exactly 32 bytes.
    for (const size_t bad_size : {size_t{31}, size_t{33}}) {
        auto database = CreateMockableWalletDatabase();

        CKeyingMaterial bad_seed(bad_size);

        {
            auto batch = database->MakeBatch();

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_STATE,
                    valid_state()));

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_SEED,
                    bad_seed));
        }

        BOOST_CHECK_EQUAL(
            load_database(std::move(database)),
            DBErrors::CORRUPT);
    }

    // Seed without PQ wallet state.
    {
        auto database = CreateMockableWalletDatabase();

        {
            auto batch = database->MakeBatch();

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_SEED,
                    valid_seed()));
        }

        BOOST_CHECK_EQUAL(
            load_database(std::move(database)),
            DBErrors::CORRUPT);
    }

    // PQ wallet state without any master seed.
    {
        auto database = CreateMockableWalletDatabase();

        {
            auto batch = database->MakeBatch();

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_STATE,
                    valid_state()));
        }

        BOOST_CHECK_EQUAL(
            load_database(std::move(database)),
            DBErrors::CORRUPT);
    }

    // Plaintext and encrypted PQ seeds may never coexist.
    {
        auto database = CreateMockableWalletDatabase();

        MercaturaPQCryptedSeed crypted_seed;
        crypted_seed.ciphertext = {
            0x01, 0x02, 0x03, 0x04
        };
        crypted_seed.seed_check.fill(0x11);

        {
            auto batch = database->MakeBatch();

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_STATE,
                    valid_state()));

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_SEED,
                    valid_seed()));

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_CRYPTED_SEED,
                    crypted_seed));
        }

        BOOST_CHECK_EQUAL(
            load_database(std::move(database)),
            DBErrors::CORRUPT);
    }

    // Empty encrypted seed is invalid.
    {
        auto database = CreateMockableWalletDatabase();

        const MercaturaPQCryptedSeed empty_crypted_seed{};

        {
            auto batch = database->MakeBatch();

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_STATE,
                    valid_state()));

            BOOST_REQUIRE(
                batch->Write(
                    DBKeys::MERCATURA_PQ_CRYPTED_SEED,
                    empty_crypted_seed));
        }

        BOOST_CHECK_EQUAL(
            load_database(std::move(database)),
            DBErrors::CORRUPT);
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
