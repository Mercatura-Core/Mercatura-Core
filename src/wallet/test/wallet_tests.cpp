// Copyright (c) 2012-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>
#include <algorithm>
#include <chainparams.h>
#include <crypto/mercatura_mldsa.h>
#include <crypto/mercatura_pqderive.h>
#include <crypto/mercatura_pqkey.h>
#include <support/cleanse.h>

#include <cstdint>
#include <future>
#include <memory>
#include <vector>

#include <addresstype.h>
#include <interfaces/chain.h>
#include <key_io.h>
#include <node/blockstorage.h>
#include <node/types.h>
#include <policy/policy.h>
#include <rpc/server.h>
#include <script/solver.h>
#include <test/util/common.h>
#include <test/util/logging.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <util/translation.h>
#include <validation.h>
#include <validationinterface.h>
#include <wallet/coincontrol.h>
#include <wallet/context.h>
#include <wallet/receive.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/test/wallet_test_fixture.h>

#include <boost/test/unit_test.hpp>
#include <univalue.h>

using node::MAX_BLOCKFILE_SIZE;

namespace wallet {

// Ensure that fee levels defined in the wallet are at least as high
// as the default levels for node policy.
static_assert(DEFAULT_TRANSACTION_MINFEE >= DEFAULT_MIN_RELAY_TX_FEE, "wallet minimum fee is smaller than default relay fee");
static_assert(WALLET_INCREMENTAL_RELAY_FEE >= DEFAULT_INCREMENTAL_RELAY_FEE, "wallet incremental fee is smaller than default incremental relay fee");

BOOST_FIXTURE_TEST_SUITE(wallet_tests, WalletTestingSetup)

static constexpr CAmount TEST_SIMPLE_SPEND_FEE{1}; // 0.01 MCA.


static CMutableTransaction TestSimpleSpend(const CTransaction& from, uint32_t index, const CKey& key, const CScript& pubkey)
{
    CMutableTransaction mtx;
    mtx.vout.emplace_back(from.vout[index].nValue - TEST_SIMPLE_SPEND_FEE, pubkey);
    mtx.vin.push_back({CTxIn{from.GetHash(), index}});
    FillableSigningProvider keystore;
    keystore.AddKey(key);
    std::map<COutPoint, Coin> coins;
    coins[mtx.vin[0].prevout].out = from.vout[index];
    std::map<int, bilingual_str> input_errors;
    BOOST_CHECK(SignTransaction(mtx, &keystore, coins, SIGHASH_ALL, input_errors));
    return mtx;
}

static void AddKey(CWallet& wallet, const CKey& key)
{
    LOCK(wallet.cs_wallet);
    FlatSigningProvider provider;
    std::string error;
    auto descs = Parse("combo(" + EncodeSecret(key) + ")", provider, error, /* require_checksum=*/ false);
    assert(descs.size() == 1);
    auto& desc = descs.at(0);
    WalletDescriptor w_desc(std::move(desc), 0, 0, 1, 1);
    Assert(wallet.AddWalletDescriptor(w_desc, provider, "", false));
}

BOOST_FIXTURE_TEST_CASE(update_non_range_descriptor, TestingSetup)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    {
        LOCK(wallet.cs_wallet);
        wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
        auto key{GenerateRandomKey()};
        auto desc_str{"combo(" + EncodeSecret(key) + ")"};
        FlatSigningProvider provider;
        std::string error;
        auto descs{Parse(desc_str, provider, error, /* require_checksum=*/ false)};
        auto& desc{descs.at(0)};
        WalletDescriptor w_desc{std::move(desc), 0, 0, 0, 0};
        BOOST_CHECK(wallet.AddWalletDescriptor(w_desc, provider, "", false));
        // Wallet should update the non-range descriptor successfully
        BOOST_CHECK(wallet.AddWalletDescriptor(w_desc, provider, "", false));
    }
}

BOOST_FIXTURE_TEST_CASE(scan_for_wallet_transactions, TestChain100Setup)
{
    // Cap last block file size, and mine new block in a new block file.
    CBlockIndex* oldTip = WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain().Tip());
    const CAmount old_tip_coinbase_value{m_coinbase_txns.back()->vout.at(0).nValue};

    WITH_LOCK(::cs_main, m_node.chainman->m_blockman.GetBlockFileInfo(oldTip->GetBlockPos().nFile)->nSize = MAX_BLOCKFILE_SIZE);
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

    CBlockIndex* newTip = WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain().Tip());
    const CAmount new_tip_coinbase_value{m_coinbase_txns.back()->vout.at(0).nValue};

    // Verify ScanForWalletTransactions fails to read an unknown start block.
    {
        CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
        {
            LOCK(wallet.cs_wallet);
            LOCK(Assert(m_node.chainman)->GetMutex());
            wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
            wallet.SetLastBlockProcessed(m_node.chainman->ActiveChain().Height(), m_node.chainman->ActiveChain().Tip()->GetBlockHash());
        }
        AddKey(wallet, coinbaseKey);
        WalletRescanReserver reserver(wallet);
        reserver.reserve();
        CWallet::ScanResult result = wallet.ScanForWalletTransactions(/*start_block=*/{}, /*start_height=*/0, /*max_height=*/{}, reserver, /*fUpdate=*/false, /*save_progress=*/false);
        BOOST_CHECK_EQUAL(result.status, CWallet::ScanResult::FAILURE);
        BOOST_CHECK(result.last_failed_block.IsNull());
        BOOST_CHECK(result.last_scanned_block.IsNull());
        BOOST_CHECK(!result.last_scanned_height);
        BOOST_CHECK_EQUAL(GetBalance(wallet).m_mine_immature, 0);
    }

    // Verify ScanForWalletTransactions picks up transactions in both the old
    // and new block files.
    {
        CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
        {
            LOCK(wallet.cs_wallet);
            LOCK(Assert(m_node.chainman)->GetMutex());
            wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
            wallet.SetLastBlockProcessed(newTip->nHeight, newTip->GetBlockHash());
        }
        AddKey(wallet, coinbaseKey);
        WalletRescanReserver reserver(wallet);
        std::chrono::steady_clock::time_point fake_time;
        reserver.setNow([&] { fake_time += 60s; return fake_time; });
        reserver.reserve();

        {
            CBlockLocator locator;
            BOOST_CHECK(WalletBatch{wallet.GetDatabase()}.ReadBestBlock(locator));
            BOOST_CHECK(!locator.IsNull() && locator.vHave.front() == newTip->GetBlockHash());
        }

        CWallet::ScanResult result = wallet.ScanForWalletTransactions(/*start_block=*/oldTip->GetBlockHash(), /*start_height=*/oldTip->nHeight, /*max_height=*/{}, reserver, /*fUpdate=*/false, /*save_progress=*/true);
        BOOST_CHECK_EQUAL(result.status, CWallet::ScanResult::SUCCESS);
        BOOST_CHECK(result.last_failed_block.IsNull());
        BOOST_CHECK_EQUAL(result.last_scanned_block, newTip->GetBlockHash());
        BOOST_CHECK_EQUAL(*result.last_scanned_height, newTip->nHeight);
        BOOST_CHECK_EQUAL(GetBalance(wallet).m_mine_immature, old_tip_coinbase_value + new_tip_coinbase_value);

        {
            CBlockLocator locator;
            BOOST_CHECK(WalletBatch{wallet.GetDatabase()}.ReadBestBlock(locator));
            BOOST_CHECK(!locator.IsNull() && locator.vHave.front() == newTip->GetBlockHash());
        }
    }

    // Prune the older block file.
    int file_number;
    {
        LOCK(cs_main);
        file_number = oldTip->GetBlockPos().nFile;
        Assert(m_node.chainman)->m_blockman.PruneOneBlockFile(file_number);
    }
    m_node.chainman->m_blockman.UnlinkPrunedFiles({file_number});

    // Verify ScanForWalletTransactions only picks transactions in the new block
    // file.
    {
        CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
        {
            LOCK(wallet.cs_wallet);
            LOCK(Assert(m_node.chainman)->GetMutex());
            wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
            wallet.SetLastBlockProcessed(m_node.chainman->ActiveChain().Height(), m_node.chainman->ActiveChain().Tip()->GetBlockHash());
        }
        AddKey(wallet, coinbaseKey);
        WalletRescanReserver reserver(wallet);
        reserver.reserve();
        CWallet::ScanResult result = wallet.ScanForWalletTransactions(/*start_block=*/oldTip->GetBlockHash(), /*start_height=*/oldTip->nHeight, /*max_height=*/{}, reserver, /*fUpdate=*/false, /*save_progress=*/false);
        BOOST_CHECK_EQUAL(result.status, CWallet::ScanResult::FAILURE);
        BOOST_CHECK_EQUAL(result.last_failed_block, oldTip->GetBlockHash());
        BOOST_CHECK_EQUAL(result.last_scanned_block, newTip->GetBlockHash());
        BOOST_CHECK_EQUAL(*result.last_scanned_height, newTip->nHeight);
        BOOST_CHECK_EQUAL(GetBalance(wallet).m_mine_immature, new_tip_coinbase_value);
    }

    // Prune the remaining block file.
    {
        LOCK(cs_main);
        file_number = newTip->GetBlockPos().nFile;
        Assert(m_node.chainman)->m_blockman.PruneOneBlockFile(file_number);
    }
    m_node.chainman->m_blockman.UnlinkPrunedFiles({file_number});

    // Verify ScanForWalletTransactions scans no blocks.
    {
        CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
        {
            LOCK(wallet.cs_wallet);
            LOCK(Assert(m_node.chainman)->GetMutex());
            wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
            wallet.SetLastBlockProcessed(m_node.chainman->ActiveChain().Height(), m_node.chainman->ActiveChain().Tip()->GetBlockHash());
        }
        AddKey(wallet, coinbaseKey);
        WalletRescanReserver reserver(wallet);
        reserver.reserve();
        CWallet::ScanResult result = wallet.ScanForWalletTransactions(/*start_block=*/oldTip->GetBlockHash(), /*start_height=*/oldTip->nHeight, /*max_height=*/{}, reserver, /*fUpdate=*/false, /*save_progress=*/false);
        BOOST_CHECK_EQUAL(result.status, CWallet::ScanResult::FAILURE);
        BOOST_CHECK_EQUAL(result.last_failed_block, newTip->GetBlockHash());
        BOOST_CHECK(result.last_scanned_block.IsNull());
        BOOST_CHECK(!result.last_scanned_height);
        BOOST_CHECK_EQUAL(GetBalance(wallet).m_mine_immature, 0);
    }
}

// This test verifies that wallet settings can be added and removed
// concurrently, ensuring no race conditions occur during either process.
BOOST_FIXTURE_TEST_CASE(write_wallet_settings_concurrently, TestingSetup)
{
    auto chain = m_node.chain.get();
    const auto NUM_WALLETS{5};

    // Since we're counting the number of wallets, ensure we start without any.
    BOOST_REQUIRE(chain->getRwSetting("wallet").isNull());

    const auto& check_concurrent_wallet = [&](const auto& settings_function, int num_expected_wallets) {
        std::vector<std::thread> threads;
        threads.reserve(NUM_WALLETS);
        for (auto i{0}; i < NUM_WALLETS; ++i) threads.emplace_back(settings_function, i);
        for (auto& t : threads) t.join();

        auto wallets = chain->getRwSetting("wallet");
        BOOST_CHECK_EQUAL(wallets.getValues().size(), num_expected_wallets);
    };

    // Add NUM_WALLETS wallets concurrently, ensure we end up with NUM_WALLETS stored.
    check_concurrent_wallet([&chain](int i) {
        Assert(AddWalletSetting(*chain, strprintf("wallet_%d", i)));
    },
                            /*num_expected_wallets=*/NUM_WALLETS);

    // Remove NUM_WALLETS wallets concurrently, ensure we end up with 0 wallets.
    check_concurrent_wallet([&chain](int i) {
        Assert(RemoveWalletSetting(*chain, strprintf("wallet_%d", i)));
    },
                            /*num_expected_wallets=*/0);
}

static int64_t AddTx(ChainstateManager& chainman, CWallet& wallet, uint32_t lockTime, int64_t mockTime, int64_t blockTime)
{
    CMutableTransaction tx;
    TxState state = TxStateInactive{};
    tx.nLockTime = lockTime;
    SetMockTime(mockTime);
    CBlockIndex* block = nullptr;
    if (blockTime > 0) {
        LOCK(cs_main);
        auto inserted = chainman.BlockIndex().emplace(std::piecewise_construct, std::make_tuple(GetRandHash()), std::make_tuple());
        assert(inserted.second);
        const uint256& hash = inserted.first->first;
        block = &inserted.first->second;
        block->nTime = blockTime;
        block->phashBlock = &hash;
        state = TxStateConfirmed{hash, block->nHeight, /*index=*/0};
    }
    return wallet.AddToWallet(MakeTransactionRef(tx), state, [&](CWalletTx& wtx, bool /* new_tx */) {
        // Assign wtx.m_state to simplify test and avoid the need to simulate
        // reorg events. Without this, AddToWallet asserts false when the same
        // transaction is confirmed in different blocks.
        wtx.m_state = state;
        return true;
    })->nTimeSmart;
}

// Simple test to verify assignment of CWalletTx::nSmartTime value. Could be
// expanded to cover more corner cases of smart time logic.
BOOST_AUTO_TEST_CASE(ComputeTimeSmart)
{
    // New transaction should use clock time if lower than block time.
    BOOST_CHECK_EQUAL(AddTx(*m_node.chainman, m_wallet, 1, 100, 120), 100);

    // Test that updating existing transaction does not change smart time.
    BOOST_CHECK_EQUAL(AddTx(*m_node.chainman, m_wallet, 1, 200, 220), 100);

    // New transaction should use clock time if there's no block time.
    BOOST_CHECK_EQUAL(AddTx(*m_node.chainman, m_wallet, 2, 300, 0), 300);

    // New transaction should use block time if lower than clock time.
    BOOST_CHECK_EQUAL(AddTx(*m_node.chainman, m_wallet, 3, 420, 400), 400);

    // New transaction should use latest entry time if higher than
    // min(block time, clock time).
    BOOST_CHECK_EQUAL(AddTx(*m_node.chainman, m_wallet, 4, 500, 390), 400);

    // If there are future entries, new transaction should use time of the
    // newest entry that is no more than 300 seconds ahead of the clock time.
    BOOST_CHECK_EQUAL(AddTx(*m_node.chainman, m_wallet, 5, 50, 600), 300);
}

void TestLoadWallet(const std::string& name, DatabaseFormat format, std::function<void(std::shared_ptr<CWallet>)> f)
{
    node::NodeContext node;
    auto chain{interfaces::MakeChain(node)};
    DatabaseOptions options;
    options.require_format = format;
    DatabaseStatus status;
    bilingual_str error;
    std::vector<bilingual_str> warnings;
    auto database{MakeWalletDatabase(name, options, status, error)};
    auto wallet{std::make_shared<CWallet>(chain.get(), "", std::move(database))};
    BOOST_CHECK_EQUAL(wallet->PopulateWalletFromDB(error, warnings), DBErrors::LOAD_OK);
    WITH_LOCK(wallet->cs_wallet, f(wallet));
}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_plaintext_wallet_roundtrip, TestingSetup)
{
    MercaturaPQWalletState expected_state;
    expected_state.account = 7;
    expected_state.next_external_index = 11;
    expected_state.next_internal_index = 13;

    CKeyingMaterial expected_seed(32);
    for (size_t i = 0; i < expected_seed.size(); ++i) {
        expected_seed[i] = static_cast<unsigned char>(i);
    }

    for (DatabaseFormat format : DATABASE_FORMATS) {
        const std::string name{
            strprintf("mercatura-pq-plaintext-%i", format)
        };

        // First wallet instance: persist the PQ records.
        TestLoadWallet(
            name,
            format,
            [&](std::shared_ptr<CWallet> wallet)
                EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                WalletBatch batch{wallet->GetDatabase()};

                BOOST_REQUIRE(
                    batch.WriteMercaturaPQState(expected_state));

                BOOST_REQUIRE(
                    batch.WriteMercaturaPQSeed(expected_seed));
            });

        // Second wallet instance: reopen and verify exact recovery.
        TestLoadWallet(
            name,
            format,
            [&](std::shared_ptr<CWallet> wallet)
                EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                BOOST_REQUIRE(wallet->HasMercaturaPQState());

                BOOST_CHECK(
                    wallet->GetMercaturaPQState() ==
                    expected_state);

                bool inspected_seed{false};

                BOOST_REQUIRE(
                    wallet->WithMercaturaPQMasterSeed(
                        [&](const CKeyingMaterial& loaded_seed) {
                            inspected_seed = true;

                            BOOST_CHECK_EQUAL_COLLECTIONS(
                                loaded_seed.begin(),
                                loaded_seed.end(),
                                expected_seed.begin(),
                                expected_seed.end());

                            return true;
                        }));

                BOOST_CHECK(inspected_seed);
            });
    }
}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_encryption_lifecycle, TestingSetup)
{
    MercaturaPQWalletState expected_state;
    expected_state.account = 7;
    expected_state.next_external_index = 11;
    expected_state.next_internal_index = 13;

    CKeyingMaterial expected_seed(32);
    for (size_t i = 0; i < expected_seed.size(); ++i) {
        expected_seed[i] = static_cast<unsigned char>(i);
    }

    const SecureString correct_passphrase{
        "mercatura-pq-test-passphrase"
    };

    const SecureString wrong_passphrase{
        "wrong-mercatura-passphrase"
    };

    for (DatabaseFormat format : DATABASE_FORMATS) {
        const std::string name{
            strprintf("mercatura-pq-encryption-%i", format)
        };

        // --------------------------------------------------------
        // 1. Create the plaintext PQ wallet records.
        // --------------------------------------------------------
        TestLoadWallet(
            name,
            format,
            [&](std::shared_ptr<CWallet> wallet)
                EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                WalletBatch batch{wallet->GetDatabase()};

                BOOST_REQUIRE(
                    batch.WriteMercaturaPQState(
                        expected_state));

                BOOST_REQUIRE(
                    batch.WriteMercaturaPQSeed(
                        expected_seed));
            });

        // --------------------------------------------------------
        // 2. Reload plaintext state and encrypt the wallet.
        // --------------------------------------------------------
        TestLoadWallet(
            name,
            format,
            [&](std::shared_ptr<CWallet> wallet)
                EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                BOOST_REQUIRE(
                    wallet->HasMercaturaPQState());

                BOOST_REQUIRE(
                    wallet->HasMercaturaPQPlaintextSeed());

                // EncryptWallet() requires a descriptor wallet.
                // Keep this test blank so no unrelated descriptor
                // generation occurs during this PQ storage test.
                wallet->SetWalletFlag(
                    WALLET_FLAG_DESCRIPTORS);

                wallet->SetWalletFlag(
                    WALLET_FLAG_BLANK_WALLET);

                BOOST_REQUIRE(
                    wallet->EncryptWallet(
                        correct_passphrase));

                // EncryptWallet() must leave the wallet locked.
                BOOST_CHECK(wallet->IsLocked());

                // The decrypted PQ seed must not remain available.
                BOOST_CHECK(
                    !wallet->HasMercaturaPQPlaintextSeed());

                BOOST_CHECK(
                    !wallet->WithMercaturaPQMasterSeed(
                        [](const CKeyingMaterial&) {
                            return true;
                        }));

                // PQ encrypted material must count as encrypted
                // private-key material.
                BOOST_CHECK(
                    wallet->HaveCryptedKeys());

                // Verify the database transformation directly.
                {
                    auto raw_batch{
                        wallet->GetDatabase().MakeBatch()
                    };

                    BOOST_CHECK(
                        !raw_batch->Exists(
                            DBKeys::MERCATURA_PQ_SEED));

                    BOOST_CHECK(
                        raw_batch->Exists(
                            DBKeys::MERCATURA_PQ_CRYPTED_SEED));

                    CMasterKey stored_master_key;

                    BOOST_CHECK(
                        raw_batch->Read(
                            std::make_pair(
                                DBKeys::MASTER_KEY,
                                wallet->nMasterKeyMaxID),
                            stored_master_key));
                }

                // Wrong passphrase must not unlock anything.
                BOOST_CHECK(
                    !wallet->Unlock(
                        wrong_passphrase));

                BOOST_CHECK(wallet->IsLocked());

                BOOST_CHECK(
                    !wallet->HasMercaturaPQPlaintextSeed());

                // Correct passphrase must recover the exact seed.
                BOOST_REQUIRE(
                    wallet->Unlock(
                        correct_passphrase));

                BOOST_CHECK(!wallet->IsLocked());

                bool inspected_seed{false};

                BOOST_REQUIRE(
                    wallet->WithMercaturaPQMasterSeed(
                        [&](const CKeyingMaterial& loaded_seed) {
                            inspected_seed = true;

                            BOOST_CHECK_EQUAL_COLLECTIONS(
                                loaded_seed.begin(),
                                loaded_seed.end(),
                                expected_seed.begin(),
                                expected_seed.end());

                            return true;
                        }));

                BOOST_CHECK(inspected_seed);

                // Locking must make the decrypted seed
                // unavailable again.
                BOOST_REQUIRE(wallet->Lock());

                BOOST_CHECK(wallet->IsLocked());

                BOOST_CHECK(
                    !wallet->HasMercaturaPQPlaintextSeed());

                BOOST_CHECK(
                    !wallet->WithMercaturaPQMasterSeed(
                        [](const CKeyingMaterial&) {
                            return true;
                        }));
            });

        // --------------------------------------------------------
        // 3. Destroy/reopen the encrypted wallet.
        // --------------------------------------------------------
        TestLoadWallet(
            name,
            format,
            [&](std::shared_ptr<CWallet> wallet)
                EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                BOOST_REQUIRE(
                    wallet->HasMercaturaPQState());

                BOOST_CHECK(
                    wallet->GetMercaturaPQState() ==
                    expected_state);

                BOOST_CHECK(
                    wallet->HasEncryptionKeys());

                BOOST_CHECK(
                    wallet->HaveCryptedKeys());

                // Reopened encrypted wallet starts locked.
                BOOST_CHECK(wallet->IsLocked());

                BOOST_CHECK(
                    !wallet->HasMercaturaPQPlaintextSeed());

                // Wrong passphrase still fails after reload.
                BOOST_CHECK(
                    !wallet->Unlock(
                        wrong_passphrase));

                BOOST_CHECK(wallet->IsLocked());

                // Correct passphrase restores exactly the
                // original authoritative 32-byte seed.
                BOOST_REQUIRE(
                    wallet->Unlock(
                        correct_passphrase));

                bool inspected_seed{false};

                BOOST_REQUIRE(
                    wallet->WithMercaturaPQMasterSeed(
                        [&](const CKeyingMaterial& loaded_seed) {
                            inspected_seed = true;

                            BOOST_CHECK_EQUAL_COLLECTIONS(
                                loaded_seed.begin(),
                                loaded_seed.end(),
                                expected_seed.begin(),
                                expected_seed.end());

                            return true;
                        }));

                BOOST_CHECK(inspected_seed);

                BOOST_REQUIRE(wallet->Lock());

                BOOST_CHECK(
                    !wallet->HasMercaturaPQPlaintextSeed());
            });
    }
}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_encryption_tamper_rejection, TestingSetup)
{
    MercaturaPQWalletState state;
    state.account = 3;
    state.next_external_index = 5;
    state.next_internal_index = 8;

    CKeyingMaterial seed(32);
    for (size_t i = 0; i < seed.size(); ++i) {
        seed[i] = static_cast<unsigned char>(0x80 + i);
    }

    const SecureString passphrase{
        "mercatura-pq-tamper-test"
    };

    for (DatabaseFormat format : DATABASE_FORMATS) {
        for (const bool corrupt_check : {false, true}) {
            const std::string name{
                strprintf(
                    "mercatura-pq-tamper-%i-%i",
                    format,
                    corrupt_check)
            };

            // Create plaintext PQ wallet state.
            TestLoadWallet(
                name,
                format,
                [&](std::shared_ptr<CWallet> wallet)
                    EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                    WalletBatch batch{wallet->GetDatabase()};

                    BOOST_REQUIRE(
                        batch.WriteMercaturaPQState(state));

                    BOOST_REQUIRE(
                        batch.WriteMercaturaPQSeed(seed));
                });

            // Encrypt normally.
            TestLoadWallet(
                name,
                format,
                [&](std::shared_ptr<CWallet> wallet)
                    EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                    wallet->SetWalletFlag(
                        WALLET_FLAG_DESCRIPTORS);

                    wallet->SetWalletFlag(
                        WALLET_FLAG_BLANK_WALLET);

                    BOOST_REQUIRE(
                        wallet->EncryptWallet(passphrase));

                    BOOST_CHECK(wallet->IsLocked());
                });

            // Tamper with the encrypted PQ record directly.
            TestLoadWallet(
                name,
                format,
                [&](std::shared_ptr<CWallet> wallet)
                    EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                    auto raw_batch{
                        wallet->GetDatabase().MakeBatch()
                    };

                    MercaturaPQCryptedSeed crypted_seed;

                    BOOST_REQUIRE(
                        raw_batch->Read(
                            DBKeys::MERCATURA_PQ_CRYPTED_SEED,
                            crypted_seed));

                    BOOST_REQUIRE(
                        crypted_seed.IsStructurallyValid());

                    if (corrupt_check) {
                        crypted_seed.seed_check[0] ^= 0x01;
                    } else {
                        BOOST_REQUIRE(
                            !crypted_seed.ciphertext.empty());

                        crypted_seed.ciphertext[0] ^= 0x01;
                    }

                    BOOST_REQUIRE(
                        raw_batch->Write(
                            DBKeys::MERCATURA_PQ_CRYPTED_SEED,
                            crypted_seed,
                            true));
                });

            // Reloading remains structurally valid, but unlocking
            // with the correct passphrase must fail closed.
            TestLoadWallet(
                name,
                format,
                [&](std::shared_ptr<CWallet> wallet)
                    EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                    BOOST_CHECK(
                        wallet->HasEncryptionKeys());

                    BOOST_CHECK(
                        wallet->HaveCryptedKeys());

                    BOOST_CHECK(wallet->IsLocked());

                    BOOST_CHECK(
                        !wallet->Unlock(passphrase));

                    BOOST_CHECK(wallet->IsLocked());

                    BOOST_CHECK(
                        !wallet->HasMercaturaPQPlaintextSeed());

                    BOOST_CHECK(
                        !wallet->WithMercaturaPQMasterSeed(
                            [](const CKeyingMaterial&) {
                                return true;
                            }));
                });
        }
    }
}

BOOST_FIXTURE_TEST_CASE(LoadReceiveRequests, TestingSetup)
{
    for (DatabaseFormat format : DATABASE_FORMATS) {
        const std::string name{strprintf("receive-requests-%i", format)};
        TestLoadWallet(name, format, [](std::shared_ptr<CWallet> wallet) EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
            BOOST_CHECK(!wallet->IsAddressPreviouslySpent(PKHash()));
            WalletBatch batch{wallet->GetDatabase()};
            BOOST_CHECK(batch.WriteAddressPreviouslySpent(PKHash(), true));
            BOOST_CHECK(batch.WriteAddressPreviouslySpent(ScriptHash(), true));
            BOOST_CHECK(wallet->SetAddressReceiveRequest(batch, PKHash(), "0", "val_rr00"));
            BOOST_CHECK(wallet->EraseAddressReceiveRequest(batch, PKHash(), "0"));
            BOOST_CHECK(wallet->SetAddressReceiveRequest(batch, PKHash(), "1", "val_rr10"));
            BOOST_CHECK(wallet->SetAddressReceiveRequest(batch, PKHash(), "1", "val_rr11"));
            BOOST_CHECK(wallet->SetAddressReceiveRequest(batch, ScriptHash(), "2", "val_rr20"));
        });
        TestLoadWallet(name, format, [](std::shared_ptr<CWallet> wallet) EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
            BOOST_CHECK(wallet->IsAddressPreviouslySpent(PKHash()));
            BOOST_CHECK(wallet->IsAddressPreviouslySpent(ScriptHash()));
            auto requests = wallet->GetAddressReceiveRequests();
            auto erequests = {"val_rr11", "val_rr20"};
            BOOST_CHECK_EQUAL_COLLECTIONS(requests.begin(), requests.end(), std::begin(erequests), std::end(erequests));
            RunWithinTxn(wallet->GetDatabase(), /*process_desc=*/"test", [](WalletBatch& batch){
                BOOST_CHECK(batch.WriteAddressPreviouslySpent(PKHash(), false));
                BOOST_CHECK(batch.EraseAddressData(ScriptHash()));
                return true;
            });
        });
        TestLoadWallet(name, format, [](std::shared_ptr<CWallet> wallet) EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
            BOOST_CHECK(!wallet->IsAddressPreviouslySpent(PKHash()));
            BOOST_CHECK(!wallet->IsAddressPreviouslySpent(ScriptHash()));
            auto requests = wallet->GetAddressReceiveRequests();
            auto erequests = {"val_rr11"};
            BOOST_CHECK_EQUAL_COLLECTIONS(requests.begin(), requests.end(), std::begin(erequests), std::end(erequests));
        });
    }
}

class ListCoinsTestingSetup : public TestChain100Setup
{
public:
    ListCoinsTestingSetup()
    {
        CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
        wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);
    }

    ~ListCoinsTestingSetup()
    {
        wallet.reset();
    }

    CWalletTx& AddTx(CRecipient recipient)
    {
        CTransactionRef tx;
        CCoinControl dummy;
        {
            auto res = CreateTransaction(*wallet, {recipient}, /*change_pos=*/std::nullopt, dummy);
            BOOST_CHECK(res);
            tx = res->tx;
        }
        wallet->CommitTransaction(tx, {}, {});
        CMutableTransaction blocktx;
        {
            LOCK(wallet->cs_wallet);
            blocktx = CMutableTransaction(*wallet->mapWallet.at(tx->GetHash()).tx);
        }
        CreateAndProcessBlock({CMutableTransaction(blocktx)}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

        LOCK(wallet->cs_wallet);
        LOCK(Assert(m_node.chainman)->GetMutex());
        wallet->SetLastBlockProcessed(wallet->GetLastBlockHeight() + 1, m_node.chainman->ActiveChain().Tip()->GetBlockHash());
        auto it = wallet->mapWallet.find(tx->GetHash());
        BOOST_CHECK(it != wallet->mapWallet.end());
        it->second.m_state = TxStateConfirmed{m_node.chainman->ActiveChain().Tip()->GetBlockHash(), m_node.chainman->ActiveChain().Height(), /*index=*/1};
        return it->second;
    }

    std::unique_ptr<CWallet> wallet;
};

BOOST_FIXTURE_TEST_CASE(mercatura_pq_transaction_receive_ownership, ListCoinsTestingSetup)
{
    // Older inherited wallet test fixtures may not have Mercatura PQ
    // state yet. Initialize it explicitly for this PQ integration test.
    {
        LOCK(wallet->cs_wallet);

        if (!wallet->HasMercaturaPQState()) {
            BOOST_REQUIRE(
                wallet->InitializeMercaturaPQWallet());
        }
    }

    auto receive_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-transaction-receive")
    };

    BOOST_REQUIRE(receive_result);

    const CTxDestination receive_destination{
        *receive_result
    };

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &receive_destination) != nullptr);

    const CScript receive_script{
        GetScriptForDestination(
            receive_destination)
    };

    CMutableTransaction incoming;
    incoming.version = 2;

    incoming.vout.emplace_back(
        10 * COIN,
        receive_script);

    // Add a second unrelated output to ensure transaction-level
    // ownership is being detected because of the PQ output.
    incoming.vout.emplace_back(
        0,
        CScript{} << OP_RETURN <<
            std::vector<unsigned char>{0x50, 0x51});

    const CTransactionRef tx{
        MakeTransactionRef(
            std::move(incoming))
    };

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK(
            wallet->IsMine(
                tx->vout.at(0)));

        BOOST_CHECK(
            !wallet->IsMine(
                tx->vout.at(1)));

        BOOST_CHECK(
            wallet->IsMine(
                *tx));
    }

    CWalletTx* added{
        wallet->AddToWallet(
            tx,
            TxStateInactive{},
            [](CWalletTx&,
               bool /*new_tx*/) {
                return true;
            })
    };

    BOOST_REQUIRE(
        added != nullptr);

    {
        LOCK(wallet->cs_wallet);

        const auto it{
            wallet->mapWallet.find(
                tx->GetHash())
        };

        BOOST_REQUIRE(
            it != wallet->mapWallet.end());

        BOOST_CHECK(
            wallet->IsMine(
                *it->second.tx));

        BOOST_CHECK(
            wallet->IsMine(
                it->second.tx->vout.at(0)));

        BOOST_CHECK(
            !wallet->IsMine(
                it->second.tx->vout.at(1)));

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQState()
                .next_external_index,
            1U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            1U);
    }
}


BOOST_FIXTURE_TEST_CASE(mercatura_pq_transaction_change_generation, ListCoinsTestingSetup)
{
    {
        LOCK(wallet->cs_wallet);

        if (!wallet->HasMercaturaPQState()) {
            BOOST_REQUIRE(
                wallet->InitializeMercaturaPQWallet());
        }
    }

    MercaturaPQWalletState state_before;
    size_t locator_count_before{0};

    {
        LOCK(wallet->cs_wallet);

        state_before =
            wallet->GetMercaturaPQState();

        locator_count_before =
            wallet->GetMercaturaPQKeyLocatorCount();
    }

    // Use an unrelated valid PQ destination as the recipient.
    // It deliberately does not belong to this wallet.
    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0x7a);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    const CTxDestination recipient_destination{
        WitnessV2MercaturaPQ{
            recipient_hash}
    };

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK(
            !wallet->IsMine(
                recipient_destination));
    }

    const CRecipient recipient{
        recipient_destination,
        1 * COIN,
        /*subtract_fee=*/false
    };

    CCoinControl coin_control;

    // W5 is testing destination/ownership integration only.
    // Actual ML-DSA input signing is implemented in W6.
    auto result{
        CreateTransaction(
            *wallet,
            {recipient},
            /*change_pos=*/std::nullopt,
            coin_control,
            /*sign=*/false)
    };

    BOOST_REQUIRE(result);

    BOOST_REQUIRE(
        result->change_pos.has_value());

    const CTxOut& change_output{
        result->tx->vout.at(
            *result->change_pos)
    };

    CTxDestination change_destination;

    BOOST_REQUIRE(
        ExtractDestination(
            change_output.scriptPubKey,
            change_destination));

    const auto* pq_change{
        std::get_if<WitnessV2MercaturaPQ>(
            &change_destination)
    };

    BOOST_REQUIRE(
        pq_change != nullptr);

    MercaturaPQKeyCommitment change_commitment{};

    std::copy(
        pq_change->begin(),
        pq_change->end(),
        change_commitment.begin());

    {
        LOCK(wallet->cs_wallet);

        // The actual transaction output generated by
        // CreateTransaction must be recognized as ours.
        BOOST_CHECK(
            wallet->IsMine(
                change_output));

        BOOST_CHECK(
            wallet->IsMine(
                change_destination));

        BOOST_CHECK(
            !wallet->IsMine(
                recipient_destination));

        const auto state_after{
            wallet->GetMercaturaPQState()
        };

        // Creating transaction change consumes exactly one
        // branch-1 derivation index.
        BOOST_CHECK_EQUAL(
            state_after.next_external_index,
            state_before.next_external_index);

        BOOST_CHECK_EQUAL(
            state_after.next_internal_index,
            state_before.next_internal_index + 1);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            locator_count_before + 1);

        const auto locator{
            wallet->GetMercaturaPQKeyLocator(
                change_commitment)
        };

        BOOST_REQUIRE(
            locator.has_value());

        BOOST_CHECK_EQUAL(
            locator->account,
            state_before.account);

        BOOST_CHECK_EQUAL(
            locator->branch,
            1U);

        BOOST_CHECK_EQUAL(
            locator->index,
            state_before.next_internal_index);

        BOOST_CHECK(
            wallet->ValidateMercaturaPQKeyLocators());
    }
}


BOOST_FIXTURE_TEST_CASE(mercatura_pq_failed_change_derivation_monotonic, ListCoinsTestingSetup)
{
    {
        LOCK(wallet->cs_wallet);

        if (!wallet->HasMercaturaPQState()) {
            BOOST_REQUIRE(
                wallet->InitializeMercaturaPQWallet());
        }
    }

    MercaturaPQWalletState state_before;
    size_t locator_count_before{0};
    CAmount available_balance{0};

    {
        LOCK(wallet->cs_wallet);

        state_before =
            wallet->GetMercaturaPQState();

        locator_count_before =
            wallet->GetMercaturaPQKeyLocatorCount();

        available_balance =
            AvailableCoins(*wallet)
                .GetTotalAmount();
    }

    BOOST_REQUIRE(
        available_balance > 0);

    // Use a valid but unrelated PQ recipient.
    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0x6b);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    const CTxDestination recipient_destination{
        WitnessV2MercaturaPQ{
            recipient_hash}
    };

    CCoinControl coin_control;

    // ------------------------------------------------------------
    // First attempt:
    //
    // CreateTransaction derives a fresh PQ change destination before
    // coin selection. Ask for more than the wallet owns so transaction
    // construction subsequently fails.
    // ------------------------------------------------------------

    const CRecipient impossible_recipient{
        recipient_destination,
        available_balance + 1,
        /*subtract_fee=*/false
    };

    auto failed_result{
        CreateTransaction(
            *wallet,
            {impossible_recipient},
            /*change_pos=*/std::nullopt,
            coin_control,
            /*sign=*/false)
    };

    BOOST_CHECK(
        !failed_result);

    {
        LOCK(wallet->cs_wallet);

        const auto state_after_failure{
            wallet->GetMercaturaPQState()
        };

        // The failed transaction must still consume exactly one
        // internal/change derivation index.
        BOOST_CHECK_EQUAL(
            state_after_failure.next_external_index,
            state_before.next_external_index);

        BOOST_CHECK_EQUAL(
            state_after_failure.next_internal_index,
            state_before.next_internal_index + 1);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            locator_count_before + 1);

        BOOST_CHECK(
            wallet->ValidateMercaturaPQKeyLocators());
    }

    // ------------------------------------------------------------
    // Second attempt:
    //
    // Construct a valid unsigned transaction. Its change address must
    // use the next index, never the index consumed by the failed try.
    // ------------------------------------------------------------

    const CRecipient valid_recipient{
        recipient_destination,
        1 * COIN,
        /*subtract_fee=*/false
    };

    auto success_result{
        CreateTransaction(
            *wallet,
            {valid_recipient},
            /*change_pos=*/std::nullopt,
            coin_control,
            /*sign=*/false)
    };

    BOOST_REQUIRE(
        success_result);

    BOOST_REQUIRE(
        success_result->change_pos.has_value());

    const CTxOut& change_output{
        success_result->tx->vout.at(
            *success_result->change_pos)
    };

    CTxDestination change_destination;

    BOOST_REQUIRE(
        ExtractDestination(
            change_output.scriptPubKey,
            change_destination));

    const auto* pq_change{
        std::get_if<WitnessV2MercaturaPQ>(
            &change_destination)
    };

    BOOST_REQUIRE(
        pq_change != nullptr);

    MercaturaPQKeyCommitment change_commitment{};

    std::copy(
        pq_change->begin(),
        pq_change->end(),
        change_commitment.begin());

    {
        LOCK(wallet->cs_wallet);

        const auto state_after_success{
            wallet->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state_after_success.next_external_index,
            state_before.next_external_index);

        BOOST_CHECK_EQUAL(
            state_after_success.next_internal_index,
            state_before.next_internal_index + 2);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            locator_count_before + 2);

        const auto locator{
            wallet->GetMercaturaPQKeyLocator(
                change_commitment)
        };

        BOOST_REQUIRE(
            locator.has_value());

        BOOST_CHECK_EQUAL(
            locator->account,
            state_before.account);

        BOOST_CHECK_EQUAL(
            locator->branch,
            1U);

        // This proves the successful transaction did not reuse the
        // branch-1 index consumed by the failed transaction.
        BOOST_CHECK_EQUAL(
            locator->index,
            state_before.next_internal_index + 1);

        BOOST_CHECK(
            wallet->IsMine(
                change_output));

        BOOST_CHECK(
            wallet->ValidateMercaturaPQKeyLocators());
    }
}


BOOST_FIXTURE_TEST_CASE(ListCoinsTest, ListCoinsTestingSetup)
{
    const CAmount coinbase_value{m_coinbase_txns.back()->vout.at(0).nValue};
    std::string coinbaseAddress = coinbaseKey.GetPubKey().GetID().ToString();

    // Confirm ListCoins initially returns 1 coin grouped under coinbaseKey
    // address.
    std::map<CTxDestination, std::vector<COutput>> list;
    {
        LOCK(wallet->cs_wallet);
        list = ListCoins(*wallet);
    }
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(std::get<PKHash>(list.begin()->first).ToString(), coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 1U);

    // Check initial balance from one mature coinbase transaction.
    BOOST_CHECK_EQUAL(coinbase_value, WITH_LOCK(wallet->cs_wallet, return AvailableCoins(*wallet).GetTotalAmount()));

    // Add a transaction creating a change address, and confirm ListCoins still
    // returns the coin associated with the change address underneath the
    // coinbaseKey pubkey, even though the change address has a different
    // pubkey.
    AddTx(CRecipient{PubKeyDestination{{}}, 1 * COIN, /*subtract_fee=*/false});
    {
        LOCK(wallet->cs_wallet);
        list = ListCoins(*wallet);
    }
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(std::get<PKHash>(list.begin()->first).ToString(), coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 2U);

    // Lock both coins. Confirm number of available coins drops to 0.
    {
        LOCK(wallet->cs_wallet);
        BOOST_CHECK_EQUAL(AvailableCoins(*wallet).Size(), 2U);
    }
    for (const auto& group : list) {
        for (const auto& coin : group.second) {
            LOCK(wallet->cs_wallet);
            wallet->LockCoin(coin.outpoint, /*persist=*/false);
        }
    }
    {
        LOCK(wallet->cs_wallet);
        BOOST_CHECK_EQUAL(AvailableCoins(*wallet).Size(), 0U);
    }
    // Confirm ListCoins still returns same result as before, despite coins
    // being locked.
    {
        LOCK(wallet->cs_wallet);
        list = ListCoins(*wallet);
    }
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(std::get<PKHash>(list.begin()->first).ToString(), coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 2U);
}

void TestCoinsResult(ListCoinsTest& context, OutputType out_type, CAmount amount,
                     std::map<OutputType, size_t>& expected_coins_sizes)
{
    LOCK(context.wallet->cs_wallet);
    util::Result<CTxDestination> dest = Assert(context.wallet->GetNewDestination(out_type, ""));
    CWalletTx& wtx = context.AddTx(CRecipient{*dest, amount, /*fSubtractFeeFromAmount=*/true});
    CoinFilterParams filter;
    filter.skip_locked = false;
    CoinsResult available_coins = AvailableCoins(*context.wallet, nullptr, std::nullopt, filter);
    // Lock outputs so they are not spent in follow-up transactions
    for (uint32_t i = 0; i < wtx.tx->vout.size(); i++) context.wallet->LockCoin({wtx.GetHash(), i}, /*persist=*/false);
    for (const auto& [type, size] : expected_coins_sizes) BOOST_CHECK_EQUAL(size, available_coins.coins[type].size());
}

BOOST_FIXTURE_TEST_CASE(BasicOutputTypesTest, ListCoinsTest)
{
    std::map<OutputType, size_t> expected_coins_sizes;
    for (const auto& out_type : OUTPUT_TYPES) { expected_coins_sizes[out_type] = 0U; }

    // Verify our wallet has one usable coinbase UTXO before starting
    // This UTXO is a P2PK, so it should show up in the Other bucket
    expected_coins_sizes[OutputType::UNKNOWN] = 1U;
    CoinsResult available_coins = WITH_LOCK(wallet->cs_wallet, return AvailableCoins(*wallet));
    BOOST_CHECK_EQUAL(available_coins.Size(), expected_coins_sizes[OutputType::UNKNOWN]);
    BOOST_CHECK_EQUAL(available_coins.coins[OutputType::UNKNOWN].size(), expected_coins_sizes[OutputType::UNKNOWN]);

    // We will create a self transfer for each of the OutputTypes and
    // verify it is put in the correct bucket after running GetAvailablecoins
    //
    // For each OutputType, We expect 2 UTXOs in our wallet following the self transfer:
    //   1. One UTXO as the recipient
    //   2. One UTXO from the change, due to payment address matching logic

    for (const auto& out_type : OUTPUT_TYPES) {
        if (out_type == OutputType::UNKNOWN) continue;
        expected_coins_sizes[out_type] = 2U;
        TestCoinsResult(*this, out_type, 1 * COIN, expected_coins_sizes);
    }
}

BOOST_FIXTURE_TEST_CASE(wallet_disableprivkeys, TestChain100Setup)
{
    const std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LOCK(wallet->cs_wallet);
    wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
    wallet->SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    BOOST_CHECK(!wallet->GetNewDestination(OutputType::BECH32, ""));
}

// Explicit calculation which is used to test the wallet constant
// We get the same virtual size due to rounding(weight/4) for both use_max_sig values
static size_t CalculateNestedKeyhashInputSize(bool use_max_sig)
{
    // Generate ephemeral valid pubkey
    CKey key = GenerateRandomKey();
    CPubKey pubkey = key.GetPubKey();

    // Generate pubkey hash
    uint160 key_hash(Hash160(pubkey));

    // Create inner-script to enter into keystore. Key hash can't be 0...
    CScript inner_script = CScript() << OP_0 << std::vector<unsigned char>(key_hash.begin(), key_hash.end());

    // Create outer P2SH script for the output
    uint160 script_id(Hash160(inner_script));
    CScript script_pubkey = CScript() << OP_HASH160 << std::vector<unsigned char>(script_id.begin(), script_id.end()) << OP_EQUAL;

    // Add inner-script to key store and key to watchonly
    FillableSigningProvider keystore;
    keystore.AddCScript(inner_script);
    keystore.AddKeyPubKey(key, pubkey);

    // Fill in dummy signatures for fee calculation.
    SignatureData sig_data;

    if (!ProduceSignature(keystore, use_max_sig ? DUMMY_MAXIMUM_SIGNATURE_CREATOR : DUMMY_SIGNATURE_CREATOR, script_pubkey, sig_data)) {
        // We're hand-feeding it correct arguments; shouldn't happen
        assert(false);
    }

    CTxIn tx_in;
    UpdateInput(tx_in, sig_data);
    return (size_t)GetVirtualTransactionInputSize(tx_in);
}

BOOST_FIXTURE_TEST_CASE(dummy_input_size_test, TestChain100Setup)
{
    BOOST_CHECK_EQUAL(CalculateNestedKeyhashInputSize(false), DUMMY_NESTED_P2WPKH_INPUT_SIZE);
    BOOST_CHECK_EQUAL(CalculateNestedKeyhashInputSize(true), DUMMY_NESTED_P2WPKH_INPUT_SIZE);
}

bool malformed_descriptor(std::ios_base::failure e)
{
    std::string s(e.what());
    return s.find("Missing checksum") != std::string::npos;
}

BOOST_FIXTURE_TEST_CASE(wallet_descriptor_test, BasicTestingSetup)
{
    std::vector<unsigned char> malformed_record;
    VectorWriter vw{malformed_record, 0};
    vw << std::string("notadescriptor");
    vw << uint64_t{0};
    vw << int32_t{0};
    vw << int32_t{0};
    vw << int32_t{1};

    SpanReader vr{malformed_record};
    WalletDescriptor w_desc;
    BOOST_CHECK_EXCEPTION(vr >> w_desc, std::ios_base::failure, malformed_descriptor);
}

//! Test CWallet::CreateNew() and its behavior handling potential race
//! conditions if it's called the same time an incoming transaction shows up in
//! the mempool or a new block.
//!
//! It isn't possible to verify there aren't race condition in every case, so
//! this test just checks two specific cases and ensures that timing of
//! notifications in these cases doesn't prevent the wallet from detecting
//! transactions.
//!
//! In the first case, block and mempool transactions are created before the
//! wallet is loaded, but notifications about these transactions are delayed
//! until after it is loaded. The notifications are superfluous in this case, so
//! the test verifies the transactions are detected before they arrive.
//!
//! In the second case, block and mempool transactions are created after the
//! wallet rescan and notifications are immediately synced, to verify the wallet
//! must already have a handler in place for them, and there's no gap after
//! rescanning where new transactions in new blocks could be lost.
BOOST_FIXTURE_TEST_CASE(CreateWallet, TestChain100Setup)
{
    m_args.ForceSetArg("-unsafesqlitesync", "1");
    // Create new wallet with known key and unload it.
    WalletContext context;
    context.args = &m_args;
    context.chain = m_node.chain.get();
    auto wallet = TestCreateWallet(context);
    CKey key = GenerateRandomKey();
    AddKey(*wallet, key);
    TestUnloadWallet(std::move(wallet));


    // Add log hook to detect AddToWallet events from rescans, blockConnected,
    // and transactionAddedToMempool notifications
    int addtx_count = 0;
    DebugLogHelper addtx_counter("[default wallet] AddToWallet", [&](const std::string* s) {
        if (s) ++addtx_count;
        return false;
    });


    bool rescan_completed = false;
    DebugLogHelper rescan_check("[default wallet] Rescan completed", [&](const std::string* s) {
        if (s) rescan_completed = true;
        return false;
    });


    // Block the queue to prevent the wallet receiving blockConnected and
    // transactionAddedToMempool notifications, and create block and mempool
    // transactions paying to the wallet
    std::promise<void> promise;
    m_node.validation_signals->CallFunctionInValidationInterfaceQueue([&promise] {
        promise.get_future().wait();
    });
    std::string error;
    m_coinbase_txns.push_back(CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);
    auto block_tx = TestSimpleSpend(*m_coinbase_txns[0], 0, coinbaseKey, GetScriptForRawPubKey(key.GetPubKey()));
    m_coinbase_txns.push_back(CreateAndProcessBlock({block_tx}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);
    auto mempool_tx = TestSimpleSpend(*m_coinbase_txns[1], 0, coinbaseKey, GetScriptForRawPubKey(key.GetPubKey()));
    BOOST_CHECK(m_node.chain->broadcastTransaction(MakeTransactionRef(mempool_tx), DEFAULT_TRANSACTION_MAXFEE, node::TxBroadcast::MEMPOOL_NO_BROADCAST, error));


    // Reload wallet and make sure new transactions are detected despite events
    // being blocked
    // Loading will also ask for current mempool transactions
    wallet = TestLoadWallet(context);
    BOOST_CHECK(rescan_completed);
    // AddToWallet events for block_tx and mempool_tx (x2)
    BOOST_CHECK_EQUAL(addtx_count, 3);
    {
        LOCK(wallet->cs_wallet);
        BOOST_CHECK(wallet->mapWallet.contains(block_tx.GetHash()));
        BOOST_CHECK(wallet->mapWallet.contains(mempool_tx.GetHash()));
    }


    // Unblock notification queue and make sure stale blockConnected and
    // transactionAddedToMempool events are processed
    promise.set_value();
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    // AddToWallet events for block_tx and mempool_tx events are counted a
    // second time as the notification queue is processed
    BOOST_CHECK_EQUAL(addtx_count, 5);


    TestUnloadWallet(std::move(wallet));


    // Load wallet again, this time creating new block and mempool transactions
    // paying to the wallet as the wallet finishes loading and syncing the
    // queue so the events have to be handled immediately. Releasing the wallet
    // lock during the sync is a little artificial but is needed to avoid a
    // deadlock during the sync and simulates a new block notification happening
    // as soon as possible.
    addtx_count = 0;
    auto handler = HandleLoadWallet(context, [&](std::unique_ptr<interfaces::Wallet> wallet) {
            BOOST_CHECK(rescan_completed);
            m_coinbase_txns.push_back(CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);
            block_tx = TestSimpleSpend(*m_coinbase_txns[2], 0, coinbaseKey, GetScriptForRawPubKey(key.GetPubKey()));
            m_coinbase_txns.push_back(CreateAndProcessBlock({block_tx}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);
            mempool_tx = TestSimpleSpend(*m_coinbase_txns[3], 0, coinbaseKey, GetScriptForRawPubKey(key.GetPubKey()));
            BOOST_CHECK(m_node.chain->broadcastTransaction(MakeTransactionRef(mempool_tx), DEFAULT_TRANSACTION_MAXFEE, node::TxBroadcast::MEMPOOL_NO_BROADCAST, error));
            m_node.validation_signals->SyncWithValidationInterfaceQueue();
        });
    wallet = TestLoadWallet(context);
    // Since mempool transactions are requested at the end of loading, there will
    // be 2 additional AddToWallet calls, one from the previous test, and a duplicate for mempool_tx
    BOOST_CHECK_EQUAL(addtx_count, 2 + 2);
    {
        LOCK(wallet->cs_wallet);
        BOOST_CHECK(wallet->mapWallet.contains(block_tx.GetHash()));
        BOOST_CHECK(wallet->mapWallet.contains(mempool_tx.GetHash()));
    }


    TestUnloadWallet(std::move(wallet));
}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_creation_matrix, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    const uint64_t normal_flags{
        WALLET_FLAG_DESCRIPTORS
    };

    const uint64_t blank_flags{
        WALLET_FLAG_DESCRIPTORS |
        WALLET_FLAG_BLANK_WALLET
    };

    const uint64_t disabled_flags{
        WALLET_FLAG_DESCRIPTORS |
        WALLET_FLAG_DISABLE_PRIVATE_KEYS
    };

    const auto check_no_pq_private_state =
        [](const std::shared_ptr<CWallet>& wallet) {
            LOCK(wallet->cs_wallet);

            BOOST_CHECK(
                !wallet->HasMercaturaPQState());

            BOOST_CHECK(
                !wallet->HasMercaturaPQPlaintextSeed());

            BOOST_CHECK(
                !wallet->WithMercaturaPQMasterSeed(
                    [](const CKeyingMaterial&) {
                        return true;
                    }));
        };

    // ------------------------------------------------------------
    // Normal wallet:
    // automatically receives one authoritative PQ master seed.
    // ------------------------------------------------------------
    {
        auto wallet{
            TestCreateWallet(
                CreateMockableWalletDatabase(),
                context,
                normal_flags)
        };

        BOOST_REQUIRE(wallet);

        {
            LOCK(wallet->cs_wallet);

            BOOST_REQUIRE(
                wallet->HasMercaturaPQState());

            const auto state{
                wallet->GetMercaturaPQState()
            };

            BOOST_CHECK_EQUAL(
                state.scheme_version,
                1U);

            BOOST_CHECK_EQUAL(
                state.derivation_version,
                1U);

            BOOST_CHECK_EQUAL(state.account, 0U);
            BOOST_CHECK_EQUAL(
                state.next_external_index,
                0U);
            BOOST_CHECK_EQUAL(
                state.next_internal_index,
                0U);

            BOOST_REQUIRE(
                wallet->HasMercaturaPQPlaintextSeed());

            bool saw_seed{false};

            BOOST_REQUIRE(
                wallet->WithMercaturaPQMasterSeed(
                    [&](const CKeyingMaterial& seed) {
                        saw_seed = true;

                        BOOST_CHECK_EQUAL(
                            seed.size(),
                            MERCATURA_PQ_WALLET_MASTER_SEED_SIZE);

                        return true;
                    }));

            BOOST_CHECK(saw_seed);

            auto batch{
                wallet->GetDatabase().MakeBatch()
            };

            BOOST_CHECK(
                batch->Exists(
                    DBKeys::MERCATURA_PQ_STATE));

            BOOST_CHECK(
                batch->Exists(
                    DBKeys::MERCATURA_PQ_SEED));

            BOOST_CHECK(
                !batch->Exists(
                    DBKeys::MERCATURA_PQ_CRYPTED_SEED));
        }

        WaitForDeleteWallet(std::move(wallet));
    }

    // ------------------------------------------------------------
    // Genuine blank wallet:
    // must not silently receive PQ private material.
    // ------------------------------------------------------------
    {
        auto wallet{
            TestCreateWallet(
                CreateMockableWalletDatabase(),
                context,
                blank_flags)
        };

        BOOST_REQUIRE(wallet);
        check_no_pq_private_state(wallet);

        {
            auto batch{
                wallet->GetDatabase().MakeBatch()
            };

            BOOST_CHECK(
                !batch->Exists(
                    DBKeys::MERCATURA_PQ_STATE));

            BOOST_CHECK(
                !batch->Exists(
                    DBKeys::MERCATURA_PQ_SEED));

            BOOST_CHECK(
                !batch->Exists(
                    DBKeys::MERCATURA_PQ_CRYPTED_SEED));
        }

        WaitForDeleteWallet(std::move(wallet));
    }

    // ------------------------------------------------------------
    // Private-key-disabled wallet:
    // no PQ private material.
    // ------------------------------------------------------------
    {
        auto wallet{
            TestCreateWallet(
                CreateMockableWalletDatabase(),
                context,
                disabled_flags)
        };

        BOOST_REQUIRE(wallet);
        check_no_pq_private_state(wallet);

        WaitForDeleteWallet(std::move(wallet));
    }

}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_born_encrypted, TestChain100Setup)
{
    m_args.ForceSetArg("-unsafesqlitesync", "1");

    WalletContext context;
    context.args = &m_args;
    context.chain = m_node.chain.get();

    DatabaseOptions options;
    options.require_create = true;
    options.create_flags = WALLET_FLAG_DESCRIPTORS;
    options.create_passphrase.assign(
        "mercatura-pq-born-encrypted-test");

    DatabaseStatus status;
    bilingual_str error;
    std::vector<bilingual_str> warnings;

    const std::string wallet_name{
        "mercatura-pq-born-encrypted"
    };

    auto wallet{
        ::wallet::CreateWallet(
            context,
            wallet_name,
            /*load_on_start=*/false,
            options,
            status,
            error,
            warnings)
    };

    BOOST_REQUIRE_MESSAGE(
        wallet,
        error.original);

    BOOST_CHECK(
        status == DatabaseStatus::SUCCESS);

    {
        LOCK(wallet->cs_wallet);

        // The wallet must have been initialized with PQ state even
        // though Bitcoin Core temporarily created it as blank so it
        // could be encrypted before descriptor setup.
        BOOST_REQUIRE(
            wallet->HasMercaturaPQState());

        const auto state{
            wallet->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state.scheme_version,
            1U);

        BOOST_CHECK_EQUAL(
            state.derivation_version,
            1U);

        BOOST_CHECK_EQUAL(
            state.account,
            0U);

        BOOST_CHECK_EQUAL(
            state.next_external_index,
            0U);

        BOOST_CHECK_EQUAL(
            state.next_internal_index,
            0U);

        // A wallet born encrypted must not retain the authoritative
        // PQ seed in plaintext while locked.
        BOOST_CHECK(
            wallet->HasEncryptionKeys());

        BOOST_CHECK(
            wallet->HaveCryptedKeys());

        BOOST_CHECK(
            wallet->IsLocked());

        BOOST_CHECK(
            !wallet->HasMercaturaPQPlaintextSeed());

        BOOST_CHECK(
            !wallet->WithMercaturaPQMasterSeed(
                [](const CKeyingMaterial&) {
                    return true;
                }));

        // Verify the persistent representation directly.
        auto batch{
            wallet->GetDatabase().MakeBatch()
        };

        BOOST_CHECK(
            batch->Exists(
                DBKeys::MERCATURA_PQ_STATE));

        BOOST_CHECK(
            !batch->Exists(
                DBKeys::MERCATURA_PQ_SEED));

        BOOST_CHECK(
            batch->Exists(
                DBKeys::MERCATURA_PQ_CRYPTED_SEED));
    }

    // Wrong password must fail closed.
    BOOST_CHECK(
        !wallet->Unlock(
            SecureString{
                "wrong-born-encrypted-passphrase"
            }));

    BOOST_CHECK(wallet->IsLocked());

    // Correct password recovers the authoritative seed.
    BOOST_REQUIRE(
        wallet->Unlock(
            SecureString{
                "mercatura-pq-born-encrypted-test"
            }));

    {
        LOCK(wallet->cs_wallet);

        bool saw_seed{false};

        BOOST_REQUIRE(
            wallet->WithMercaturaPQMasterSeed(
                [&](const CKeyingMaterial& seed) {
                    saw_seed = true;

                    BOOST_CHECK_EQUAL(
                        seed.size(),
                        MERCATURA_PQ_WALLET_MASTER_SEED_SIZE);

                    return true;
                }));

        BOOST_CHECK(saw_seed);
    }

    BOOST_REQUIRE(wallet->Lock());

    BOOST_CHECK(wallet->IsLocked());

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK(
            !wallet->HasMercaturaPQPlaintextSeed());

        BOOST_CHECK(
            !wallet->WithMercaturaPQMasterSeed(
                [](const CKeyingMaterial&) {
                    return true;
                }));
    }

    // CreateWallet() registered this wallet in WalletContext.
    // Remove it through the production unload path before deletion.
    BOOST_REQUIRE(
        RemoveWallet(
            context,
            wallet,
            /*load_on_start=*/false,
            warnings));

    WaitForDeleteWallet(std::move(wallet));
}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_key_locator_persistence, TestingSetup)
{
    MercaturaPQWalletState expected_state;
    expected_state.account = 0;
    expected_state.next_external_index = 2;
    expected_state.next_internal_index = 1;

    CKeyingMaterial expected_seed(
        MERCATURA_PQ_WALLET_MASTER_SEED_SIZE);

    for (size_t i = 0;
         i < expected_seed.size();
         ++i) {
        expected_seed[i] =
            static_cast<unsigned char>(0x40 + i);
    }

    MercaturaPQKeyCommitment external_0{};
    MercaturaPQKeyCommitment external_1{};
    MercaturaPQKeyCommitment internal_0{};

    external_0.fill(0x11);
    external_1.fill(0x22);
    internal_0.fill(0x33);

    const MercaturaPQKeyLocator locator_external_0{
        /*account=*/0,
        /*branch=*/0,
        /*index=*/0,
    };

    const MercaturaPQKeyLocator locator_external_1{
        /*account=*/0,
        /*branch=*/0,
        /*index=*/1,
    };

    const MercaturaPQKeyLocator locator_internal_0{
        /*account=*/0,
        /*branch=*/1,
        /*index=*/0,
    };

    for (DatabaseFormat format : DATABASE_FORMATS) {
        const std::string name{
            strprintf(
                "mercatura-pq-locators-%i",
                format)
        };

        // --------------------------------------------------------
        // Write PQ state, seed, and public ownership metadata.
        // --------------------------------------------------------
        TestLoadWallet(
            name,
            format,
            [&](std::shared_ptr<CWallet> wallet)
                EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                WalletBatch batch{
                    wallet->GetDatabase()
                };

                BOOST_REQUIRE(
                    batch.WriteMercaturaPQState(
                        expected_state));

                BOOST_REQUIRE(
                    batch.WriteMercaturaPQSeed(
                        expected_seed));

                BOOST_REQUIRE(
                    batch.WriteMercaturaPQKeyLocator(
                        external_0,
                        locator_external_0));

                BOOST_REQUIRE(
                    batch.WriteMercaturaPQKeyLocator(
                        external_1,
                        locator_external_1));

                BOOST_REQUIRE(
                    batch.WriteMercaturaPQKeyLocator(
                        internal_0,
                        locator_internal_0));

                // Invalid branch values must never be accepted
                // by the normal writer.
                MercaturaPQKeyLocator invalid{
                    /*account=*/0,
                    /*branch=*/2,
                    /*index=*/0,
                };

                MercaturaPQKeyCommitment invalid_commitment{};
                invalid_commitment.fill(0x44);

                BOOST_CHECK(
                    !batch.WriteMercaturaPQKeyLocator(
                        invalid_commitment,
                        invalid));
            });

        // --------------------------------------------------------
        // Destroy/reopen and verify ownership metadata survives
        // without storing any expanded ML-DSA secret key.
        // --------------------------------------------------------
        TestLoadWallet(
            name,
            format,
            [&](std::shared_ptr<CWallet> wallet)
                EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet) {
                BOOST_REQUIRE(
                    wallet->HasMercaturaPQState());

                BOOST_CHECK_EQUAL(
                    wallet->GetMercaturaPQKeyLocatorCount(),
                    3U);

                BOOST_CHECK(
                    wallet->HasMercaturaPQKeyCommitment(
                        external_0));

                BOOST_CHECK(
                    wallet->HasMercaturaPQKeyCommitment(
                        external_1));

                BOOST_CHECK(
                    wallet->HasMercaturaPQKeyCommitment(
                        internal_0));

                const auto loaded_external_0{
                    wallet->GetMercaturaPQKeyLocator(
                        external_0)
                };

                const auto loaded_external_1{
                    wallet->GetMercaturaPQKeyLocator(
                        external_1)
                };

                const auto loaded_internal_0{
                    wallet->GetMercaturaPQKeyLocator(
                        internal_0)
                };

                BOOST_REQUIRE(
                    loaded_external_0.has_value());

                BOOST_REQUIRE(
                    loaded_external_1.has_value());

                BOOST_REQUIRE(
                    loaded_internal_0.has_value());

                BOOST_CHECK(
                    *loaded_external_0 ==
                    locator_external_0);

                BOOST_CHECK(
                    *loaded_external_1 ==
                    locator_external_1);

                BOOST_CHECK(
                    *loaded_internal_0 ==
                    locator_internal_0);

                BOOST_CHECK(
                    wallet->ValidateMercaturaPQKeyLocators());

                const auto state{
                    wallet->GetMercaturaPQState()
                };

                BOOST_CHECK_EQUAL(
                    state.next_external_index,
                    2U);

                BOOST_CHECK_EQUAL(
                    state.next_internal_index,
                    1U);
            });
    }
}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_live_destination_derivation, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    auto wallet{
        TestCreateWallet(
            CreateMockableWalletDatabase(),
            context,
            WALLET_FLAG_DESCRIPTORS)
    };

    BOOST_REQUIRE(wallet);

    CKeyingMaterial master_seed;

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->HasMercaturaPQState());

        BOOST_REQUIRE(
            wallet->WithMercaturaPQMasterSeed(
                [&](const CKeyingMaterial& seed) {
                    master_seed = seed;
                    return true;
                }));

        BOOST_REQUIRE_EQUAL(
            master_seed.size(),
            MERCATURA_PQ_MASTER_SEED_SIZE);
    }

    // Independently reproduce the destination generated by the
    // production wallet helper from the frozen derivation rules.
    const auto reproduce_destination =
        [&](uint8_t branch, uint32_t index) {
            const std::span<
                const unsigned char,
                MERCATURA_PQ_MASTER_SEED_SIZE>
                seed_span{
                    master_seed.data(),
                    MERCATURA_PQ_MASTER_SEED_SIZE
                };

            auto child_seed{
                DeriveMercaturaPQChildSeed(
                    seed_span,
                    Params().GetConsensus().hashGenesisBlock,
                    /*account=*/0,
                    branch,
                    index)
            };

            BOOST_REQUIRE(
                child_seed.has_value());

            std::array<
                uint8_t,
                MERCATURA_MLDSA65_PUBLIC_KEY_SIZE>
                public_key{};

            std::array<
                uint8_t,
                MERCATURA_MLDSA65_SECRET_KEY_SIZE>
                secret_key{};

            BOOST_REQUIRE_EQUAL(
                mercatura_mldsa65_keypair_from_seed(
                    public_key.data(),
                    public_key.size(),
                    secret_key.data(),
                    secret_key.size(),
                    child_seed->data(),
                    child_seed->size()),
                1);

            MercaturaPQKeyCommitment commitment{};

            BOOST_REQUIRE(
                ComputeMercaturaPQKeyCommitmentV1(
                    commitment,
                    public_key));

            memory_cleanse(
                child_seed->data(),
                child_seed->size());

            memory_cleanse(
                secret_key.data(),
                secret_key.size());

            const uint256 commitment_hash{
                std::span<const unsigned char>{
                    commitment}
            };

            return std::make_pair(
                CTxDestination{
                    WitnessV2MercaturaPQ{
                        commitment_hash}},
                commitment);
        };

    // ============================================================
    // External / receive branch 0, index 0
    // ============================================================

    const auto expected_receive_0{
        reproduce_destination(
            /*branch=*/0,
            /*index=*/0)
    };

    auto receive_0_result{
        wallet->GetNewMercaturaPQDestination(
            /*internal=*/false)
    };

    BOOST_REQUIRE(receive_0_result);

    const CTxDestination receive_0{
        *receive_0_result
    };

    BOOST_CHECK(
        receive_0 ==
        expected_receive_0.first);

    {
        LOCK(wallet->cs_wallet);

        const auto state{
            wallet->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state.next_external_index,
            1U);

        BOOST_CHECK_EQUAL(
            state.next_internal_index,
            0U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            1U);

        const auto locator{
            wallet->GetMercaturaPQKeyLocator(
                expected_receive_0.second)
        };

        BOOST_REQUIRE(locator.has_value());

        BOOST_CHECK_EQUAL(
            locator->account,
            0U);

        BOOST_CHECK_EQUAL(
            locator->branch,
            0U);

        BOOST_CHECK_EQUAL(
            locator->index,
            0U);
    }

    // ============================================================
    // External / receive branch 0, index 1
    // ============================================================

    const auto expected_receive_1{
        reproduce_destination(
            /*branch=*/0,
            /*index=*/1)
    };

    auto receive_1_result{
        wallet->GetNewMercaturaPQDestination(
            /*internal=*/false)
    };

    BOOST_REQUIRE(receive_1_result);

    const CTxDestination receive_1{
        *receive_1_result
    };

    BOOST_CHECK(
        receive_1 ==
        expected_receive_1.first);

    BOOST_CHECK(
        receive_1 != receive_0);

    {
        LOCK(wallet->cs_wallet);

        const auto state{
            wallet->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state.next_external_index,
            2U);

        BOOST_CHECK_EQUAL(
            state.next_internal_index,
            0U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            2U);

        const auto locator{
            wallet->GetMercaturaPQKeyLocator(
                expected_receive_1.second)
        };

        BOOST_REQUIRE(locator.has_value());

        BOOST_CHECK_EQUAL(
            locator->branch,
            0U);

        BOOST_CHECK_EQUAL(
            locator->index,
            1U);
    }

    // ============================================================
    // Internal / change branch 1, index 0
    // ============================================================

    const auto expected_change_0{
        reproduce_destination(
            /*branch=*/1,
            /*index=*/0)
    };

    auto change_0_result{
        wallet->GetNewMercaturaPQDestination(
            /*internal=*/true)
    };

    BOOST_REQUIRE(change_0_result);

    const CTxDestination change_0{
        *change_0_result
    };

    BOOST_CHECK(
        change_0 ==
        expected_change_0.first);

    BOOST_CHECK(
        change_0 != receive_0);

    BOOST_CHECK(
        change_0 != receive_1);

    {
        LOCK(wallet->cs_wallet);

        const auto state{
            wallet->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state.next_external_index,
            2U);

        BOOST_CHECK_EQUAL(
            state.next_internal_index,
            1U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            3U);

        const auto locator{
            wallet->GetMercaturaPQKeyLocator(
                expected_change_0.second)
        };

        BOOST_REQUIRE(locator.has_value());

        BOOST_CHECK_EQUAL(
            locator->branch,
            1U);

        BOOST_CHECK_EQUAL(
            locator->index,
            0U);

        BOOST_CHECK(
            wallet->ValidateMercaturaPQKeyLocators());
    }

    // ============================================================
    // Destination <-> script round trips.
    // ============================================================

    for (const CTxDestination& destination :
         {receive_0, receive_1, change_0}) {
        const CScript script{
            GetScriptForDestination(
                destination)
        };

        CTxDestination extracted;

        BOOST_REQUIRE(
            ExtractDestination(
                script,
                extracted));

        BOOST_CHECK(
            extracted ==
            destination);

        BOOST_REQUIRE(
            std::get_if<
                WitnessV2MercaturaPQ>(
                    &extracted) != nullptr);
    }

    // ============================================================
    // Reload persistence.
    //
    // The wallet must retain public ownership metadata without
    // needing to derive the secret keys again.
    // ============================================================

    auto duplicate_db{
        DuplicateMockDatabase(
            wallet->GetDatabase())
    };

    WaitForDeleteWallet(
        std::move(wallet));

    auto reloaded{
        TestLoadWallet(
            std::move(duplicate_db),
            context)
    };

    BOOST_REQUIRE(reloaded);

    {
        LOCK(reloaded->cs_wallet);

        const auto state{
            reloaded->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state.next_external_index,
            2U);

        BOOST_CHECK_EQUAL(
            state.next_internal_index,
            1U);

        BOOST_CHECK_EQUAL(
            reloaded->GetMercaturaPQKeyLocatorCount(),
            3U);

        BOOST_CHECK(
            reloaded->HasMercaturaPQKeyCommitment(
                expected_receive_0.second));

        BOOST_CHECK(
            reloaded->HasMercaturaPQKeyCommitment(
                expected_receive_1.second));

        BOOST_CHECK(
            reloaded->HasMercaturaPQKeyCommitment(
                expected_change_0.second));

        BOOST_CHECK(
            reloaded->ValidateMercaturaPQKeyLocators());
    }

    WaitForDeleteWallet(
        std::move(reloaded));

    if (!master_seed.empty()) {
        memory_cleanse(
            master_seed.data(),
            master_seed.size());

        master_seed.clear();
    }
}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_locked_derivation_behavior, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    auto wallet{
        TestCreateWallet(
            CreateMockableWalletDatabase(),
            context,
            WALLET_FLAG_DESCRIPTORS)
    };

    BOOST_REQUIRE(wallet);

    SecureString passphrase;
    passphrase.assign(
        "mercatura-pq-locked-derivation-test");

    BOOST_REQUIRE(
        wallet->EncryptWallet(
            passphrase));

    BOOST_CHECK(
        wallet->IsLocked());

    {
        LOCK(wallet->cs_wallet);

        const auto state{
            wallet->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state.next_external_index,
            0U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            0U);
    }

    // Mercatura PQ v1 deliberately has no xpub/public-child
    // derivation. A locked encrypted wallet therefore cannot
    // create a fresh address.
    auto locked_result{
        wallet->GetNewMercaturaPQDestination(
            /*internal=*/false)
    };

    BOOST_CHECK(
        !locked_result);

    // Failure must not consume an address index.
    {
        LOCK(wallet->cs_wallet);

        const auto state{
            wallet->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state.next_external_index,
            0U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            0U);
    }

    BOOST_REQUIRE(
        wallet->Unlock(
            passphrase));

    BOOST_CHECK(
        !wallet->IsLocked());

    auto unlocked_result{
        wallet->GetNewMercaturaPQDestination(
            /*internal=*/false)
    };

    BOOST_REQUIRE(
        unlocked_result);

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &*unlocked_result) != nullptr);

    {
        LOCK(wallet->cs_wallet);

        const auto state{
            wallet->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state.next_external_index,
            1U);

        BOOST_CHECK_EQUAL(
            state.next_internal_index,
            0U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            1U);

        BOOST_CHECK(
            wallet->ValidateMercaturaPQKeyLocators());
    }

    BOOST_REQUIRE(
        wallet->Lock());

    BOOST_CHECK(
        wallet->IsLocked());

    // A second locked derivation must again fail without advancing.
    auto second_locked_result{
        wallet->GetNewMercaturaPQDestination(
            /*internal=*/false)
    };

    BOOST_CHECK(
        !second_locked_result);

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQState()
                .next_external_index,
            1U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            1U);
    }

    WaitForDeleteWallet(
        std::move(wallet));
}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_normal_wallet_flows, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    auto wallet{
        TestCreateWallet(
            CreateMockableWalletDatabase(),
            context,
            WALLET_FLAG_DESCRIPTORS)
    };

    BOOST_REQUIRE(wallet);

    // ============================================================
    // Normal receive-address API must now produce Mercatura PQ.
    // ============================================================

    auto receive_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-receive")
    };

    BOOST_REQUIRE(receive_result);

    const CTxDestination receive{
        *receive_result
    };

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &receive) != nullptr);

    const CScript receive_script{
        GetScriptForDestination(
            receive)
    };

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK(
            wallet->IsMine(receive));

        BOOST_CHECK(
            wallet->IsMine(receive_script));

        const auto state{
            wallet->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state.next_external_index,
            1U);

        BOOST_CHECK_EQUAL(
            state.next_internal_index,
            0U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            1U);

        const auto* book_entry{
            wallet->FindAddressBookEntry(
                receive)
        };

        BOOST_REQUIRE(
            book_entry != nullptr);

        BOOST_REQUIRE(
            book_entry->label.has_value());

        BOOST_CHECK_EQUAL(
            *book_entry->label,
            "pq-receive");
    }

    // ============================================================
    // Normal change API must use branch 1.
    // ============================================================

    auto change_result{
        wallet->GetNewChangeDestination(
            OutputType::BECH32)
    };

    BOOST_REQUIRE(change_result);

    const CTxDestination change{
        *change_result
    };

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &change) != nullptr);

    BOOST_CHECK(
        change != receive);

    const CScript change_script{
        GetScriptForDestination(
            change)
    };

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK(
            wallet->IsMine(change));

        BOOST_CHECK(
            wallet->IsMine(change_script));

        const auto state{
            wallet->GetMercaturaPQState()
        };

        BOOST_CHECK_EQUAL(
            state.next_external_index,
            1U);

        BOOST_CHECK_EQUAL(
            state.next_internal_index,
            1U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            2U);
    }

    // ============================================================
    // An unrelated PQ commitment must not be ours.
    // ============================================================

    MercaturaPQKeyCommitment unknown_commitment{};
    unknown_commitment.fill(0xa5);

    const uint256 unknown_hash{
        std::span<const unsigned char>{
            unknown_commitment}
    };

    const CTxDestination unknown_destination{
        WitnessV2MercaturaPQ{
            unknown_hash}
    };

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK(
            !wallet->IsMine(
                unknown_destination));

        BOOST_CHECK(
            !wallet->IsMine(
                GetScriptForDestination(
                    unknown_destination)));
    }

    // ============================================================
    // Encrypt and lock.
    //
    // Ownership must continue working without the master seed.
    // ============================================================

    SecureString passphrase;
    passphrase.assign(
        "mercatura-pq-wallet-flow-test");

    BOOST_REQUIRE(
        wallet->EncryptWallet(
            passphrase));

    BOOST_CHECK(
        wallet->IsLocked());

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK(
            !wallet->HasMercaturaPQPlaintextSeed());

        BOOST_CHECK(
            wallet->IsMine(receive));

        BOOST_CHECK(
            wallet->IsMine(change));

        BOOST_CHECK(
            !wallet->IsMine(
                unknown_destination));
    }

    // A locked wallet can recognize existing addresses but cannot
    // derive another one because PQ v1 deliberately has no xpub.
    auto locked_receive{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "must-fail-while-locked")
    };

    BOOST_CHECK(
        !locked_receive);

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQState()
                .next_external_index,
            1U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            2U);
    }

    // ============================================================
    // Reload while encrypted and locked.
    // ============================================================

    auto duplicate_db{
        DuplicateMockDatabase(
            wallet->GetDatabase())
    };

    WaitForDeleteWallet(
        std::move(wallet));

    auto reloaded{
        TestLoadWallet(
            std::move(duplicate_db),
            context)
    };

    BOOST_REQUIRE(reloaded);

    BOOST_CHECK(
        reloaded->IsLocked());

    {
        LOCK(reloaded->cs_wallet);

        BOOST_CHECK(
            reloaded->IsMine(
                receive));

        BOOST_CHECK(
            reloaded->IsMine(
                change));

        BOOST_CHECK(
            !reloaded->IsMine(
                unknown_destination));

        BOOST_CHECK_EQUAL(
            reloaded->GetMercaturaPQKeyLocatorCount(),
            2U);

        BOOST_CHECK_EQUAL(
            reloaded->GetMercaturaPQState()
                .next_external_index,
            1U);

        BOOST_CHECK_EQUAL(
            reloaded->GetMercaturaPQState()
                .next_internal_index,
            1U);
    }

    BOOST_REQUIRE(
        reloaded->Unlock(
            passphrase));

    auto receive_after_unlock{
        reloaded->GetNewDestination(
            OutputType::BECH32,
            "after-unlock")
    };

    BOOST_REQUIRE(
        receive_after_unlock);

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &*receive_after_unlock) != nullptr);

    {
        LOCK(reloaded->cs_wallet);

        BOOST_CHECK(
            reloaded->IsMine(
                *receive_after_unlock));

        BOOST_CHECK_EQUAL(
            reloaded->GetMercaturaPQState()
                .next_external_index,
            2U);

        BOOST_CHECK_EQUAL(
            reloaded->GetMercaturaPQKeyLocatorCount(),
            3U);
    }

    WaitForDeleteWallet(
        std::move(reloaded));
}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_classical_address_generation_shutdown, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    auto wallet{
        TestCreateWallet(
            CreateMockableWalletDatabase(),
            context,
            WALLET_FLAG_DESCRIPTORS)
    };

    BOOST_REQUIRE(wallet);

    // Every inherited Bitcoin OutputType request must still produce
    // Mercatura native witness-v2 PQ ownership.
    const std::array<OutputType, 4> requested_types{
        OutputType::LEGACY,
        OutputType::P2SH_SEGWIT,
        OutputType::BECH32,
        OutputType::BECH32M,
    };

    uint32_t expected_external_index{0};

    for (const OutputType type : requested_types) {
        auto result{
            wallet->GetNewDestination(
                type,
                "pq-only")
        };

        BOOST_REQUIRE(result);

        BOOST_REQUIRE(
            std::get_if<WitnessV2MercaturaPQ>(
                &*result) != nullptr);

        {
            LOCK(wallet->cs_wallet);

            BOOST_CHECK(
                wallet->IsMine(*result));

            ++expected_external_index;

            BOOST_CHECK_EQUAL(
                wallet->GetMercaturaPQState()
                    .next_external_index,
                expected_external_index);
        }
    }

    // Change behaves the same way: the inherited requested type
    // cannot select a classical ownership destination.
    for (const OutputType type : requested_types) {
        auto result{
            wallet->GetNewChangeDestination(
                type)
        };

        BOOST_REQUIRE(result);

        BOOST_REQUIRE(
            std::get_if<WitnessV2MercaturaPQ>(
                &*result) != nullptr);

        {
            LOCK(wallet->cs_wallet);

            BOOST_CHECK(
                wallet->IsMine(*result));
        }
    }

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQState()
                .next_external_index,
            4U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQState()
                .next_internal_index,
            4U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            8U);

        BOOST_CHECK(
            wallet->ValidateMercaturaPQKeyLocators());
    }

    // Once encrypted and locked, address generation must fail.
    // It must never fall back to an inherited classical keypool.
    SecureString passphrase;
    passphrase.assign(
        "mercatura-pq-classical-shutdown-test");

    BOOST_REQUIRE(
        wallet->EncryptWallet(
            passphrase));

    BOOST_CHECK(
        wallet->IsLocked());

    for (const OutputType type : requested_types) {
        BOOST_CHECK(
            !wallet->GetNewDestination(
                type,
                "locked"));

        BOOST_CHECK(
            !wallet->GetNewChangeDestination(
                type));
    }

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQState()
                .next_external_index,
            4U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQState()
                .next_internal_index,
            4U);

        BOOST_CHECK_EQUAL(
            wallet->GetMercaturaPQKeyLocatorCount(),
            8U);

        // Existing destinations remain recognizable while locked.
        BOOST_CHECK(
            wallet->ValidateMercaturaPQKeyLocators());
    }

    WaitForDeleteWallet(
        std::move(wallet));
}

BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_single_input_signing, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    auto wallet{
        TestCreateWallet(
            CreateMockableWalletDatabase(),
            context,
            WALLET_FLAG_DESCRIPTORS)
    };

    BOOST_REQUIRE(wallet);

    auto receive_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-signing-source")
    };

    BOOST_REQUIRE(receive_result);

    const CTxDestination owned_destination{
        *receive_result
    };

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &owned_destination) != nullptr);

    // Create an incoming transaction containing one owned PQ output.
    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        10 * COIN,
        GetScriptForDestination(
            owned_destination));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->AddToWallet(
                funding_tx,
                TxStateInactive{},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);
    }

    // Spend the owned PQ output to an unrelated valid PQ destination.
    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0xa7);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    const CTxDestination recipient{
        WitnessV2MercaturaPQ{
            recipient_hash}
    };

    CMutableTransaction spend;
    spend.version = 2;

    spend.vin.emplace_back(
        COutPoint{
            funding_tx->GetHash(),
            0});

    spend.vout.emplace_back(
        9 * COIN,
        GetScriptForDestination(
            recipient));

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->SignTransaction(
                spend));
    }

    BOOST_CHECK(
        spend.vin.at(0)
            .scriptSig.empty());

    BOOST_REQUIRE_EQUAL(
        spend.vin.at(0)
            .scriptWitness.stack.size(),
        2U);

    BOOST_CHECK_EQUAL(
        spend.vin.at(0)
            .scriptWitness.stack.at(0).size(),
        MERCATURA_MLDSA65_SIGNATURE_SIZE);

    BOOST_CHECK_EQUAL(
        spend.vin.at(0)
            .scriptWitness.stack.at(1).size(),
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

    // Verify the exact resulting witness through the real consensus
    // witness-v2 PQ verifier.
    std::vector<CTxOut> spent_outputs{
        funding_tx->vout.at(0)
    };

    PrecomputedTransactionData txdata;

    txdata.Init(
        spend,
        std::move(spent_outputs),
        /*force=*/true);

    BOOST_REQUIRE(
        txdata.m_pq_ready);

    MutableTransactionSignatureChecker checker{
        &spend,
        /*nIn=*/0,
        funding_tx->vout.at(0).nValue,
        txdata,
        MissingDataBehavior::FAIL,
        std::optional<uint256>{
            Params().GetConsensus().hashGenesisBlock}
    };

    ScriptError error{
        SCRIPT_ERR_UNKNOWN_ERROR
    };

    BOOST_CHECK(
        VerifyScript(
            spend.vin.at(0).scriptSig,
            funding_tx->vout.at(0).scriptPubKey,
            &spend.vin.at(0).scriptWitness,
            STANDARD_SCRIPT_VERIFY_FLAGS,
            checker,
            &error));

    BOOST_CHECK_EQUAL(
        error,
        SCRIPT_ERR_OK);

    WaitForDeleteWallet(
        std::move(wallet));
}


BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_alternate_sighash_rejection, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    auto wallet{
        TestCreateWallet(
            CreateMockableWalletDatabase(),
            context,
            WALLET_FLAG_DESCRIPTORS)
    };

    BOOST_REQUIRE(wallet);

    auto receive_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-sighash-source")
    };

    BOOST_REQUIRE(receive_result);

    const CTxDestination owned_destination{
        *receive_result
    };

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &owned_destination) != nullptr);

    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        10 * COIN,
        GetScriptForDestination(
            owned_destination));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    const COutPoint prevout{
        funding_tx->GetHash(),
        0
    };

    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0xb7);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    CMutableTransaction spend;
    spend.version = 2;

    spend.vin.emplace_back(
        prevout);

    spend.vout.emplace_back(
        9 * COIN,
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                recipient_hash}));

    std::map<COutPoint, Coin> coins;

    coins.emplace(
        prevout,
        Coin{
            funding_tx->vout.at(0),
            /*nHeight=*/0,
            /*fCoinBase=*/false
        });

    std::map<int, bilingual_str> input_errors;

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK(
            !wallet->SignTransaction(
                spend,
                coins,
                SIGHASH_ALL,
                input_errors));
    }

    BOOST_CHECK(
        spend.vin.at(0)
            .scriptSig.empty());

    BOOST_CHECK(
        spend.vin.at(0)
            .scriptWitness.stack.empty());

    BOOST_REQUIRE(
        input_errors.contains(0));

    BOOST_CHECK(
        input_errors.at(0)
            .original.find(
                "does not support alternate sighash modes") !=
        std::string::npos);

    // The identical transaction must succeed with PQ Authorization
    // v1's fixed signing mode.
    input_errors.clear();

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->SignTransaction(
                spend,
                coins,
                SIGHASH_DEFAULT,
                input_errors));
    }

    BOOST_CHECK(
        spend.vin.at(0)
            .scriptSig.empty());

    BOOST_REQUIRE_EQUAL(
        spend.vin.at(0)
            .scriptWitness.stack.size(),
        2U);

    BOOST_CHECK_EQUAL(
        spend.vin.at(0)
            .scriptWitness.stack.at(0).size(),
        MERCATURA_MLDSA65_SIGNATURE_SIZE);

    BOOST_CHECK_EQUAL(
        spend.vin.at(0)
            .scriptWitness.stack.at(1).size(),
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

    BOOST_CHECK(
        input_errors.empty());

    WaitForDeleteWallet(
        std::move(wallet));
}


BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_locked_signing_behavior, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    auto wallet{
        TestCreateWallet(
            CreateMockableWalletDatabase(),
            context,
            WALLET_FLAG_DESCRIPTORS)
    };

    BOOST_REQUIRE(wallet);

    auto receive_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-locked-signing-source")
    };

    BOOST_REQUIRE(receive_result);

    const CTxDestination owned_destination{
        *receive_result
    };

    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        10 * COIN,
        GetScriptForDestination(
            owned_destination));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->AddToWallet(
                funding_tx,
                TxStateInactive{},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);
    }

    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0xb1);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    CMutableTransaction spend;
    spend.version = 2;

    spend.vin.emplace_back(
        COutPoint{
            funding_tx->GetHash(),
            0});

    spend.vout.emplace_back(
        9 * COIN,
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                recipient_hash}));

    SecureString passphrase;
    passphrase.assign(
        "mercatura-pq-signing-test-passphrase");

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->EncryptWallet(
                passphrase));

        BOOST_CHECK(
            wallet->IsLocked());

        // A locked encrypted wallet must fail closed.
        BOOST_CHECK(
            !wallet->SignTransaction(
                spend));

        // Failure must not leave a partial PQ witness.
        BOOST_CHECK(
            spend.vin.at(0)
                .scriptSig.empty());

        BOOST_CHECK(
            spend.vin.at(0)
                .scriptWitness.IsNull());

        BOOST_REQUIRE(
            wallet->Unlock(
                passphrase));

        BOOST_CHECK(
            !wallet->IsLocked());

        // The same transaction must sign after unlocking.
        BOOST_REQUIRE(
            wallet->SignTransaction(
                spend));
    }

    BOOST_REQUIRE_EQUAL(
        spend.vin.at(0)
            .scriptWitness.stack.size(),
        2U);

    BOOST_CHECK_EQUAL(
        spend.vin.at(0)
            .scriptWitness.stack.at(0).size(),
        MERCATURA_MLDSA65_SIGNATURE_SIZE);

    BOOST_CHECK_EQUAL(
        spend.vin.at(0)
            .scriptWitness.stack.at(1).size(),
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

    WaitForDeleteWallet(
        std::move(wallet));
}


BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_multi_input_signing, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    auto wallet{
        TestCreateWallet(
            CreateMockableWalletDatabase(),
            context,
            WALLET_FLAG_DESCRIPTORS)
    };

    BOOST_REQUIRE(wallet);

    auto receive_a{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-multi-a")
    };

    auto receive_b{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-multi-b")
    };

    BOOST_REQUIRE(receive_a);
    BOOST_REQUIRE(receive_b);

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &*receive_a) != nullptr);

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &*receive_b) != nullptr);

    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        6 * COIN,
        GetScriptForDestination(
            *receive_a));

    funding.vout.emplace_back(
        7 * COIN,
        GetScriptForDestination(
            *receive_b));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->AddToWallet(
                funding_tx,
                TxStateInactive{},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);
    }

    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0xc2);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    CMutableTransaction spend;
    spend.version = 2;

    spend.vin.emplace_back(
        COutPoint{
            funding_tx->GetHash(),
            0});

    spend.vin.emplace_back(
        COutPoint{
            funding_tx->GetHash(),
            1});

    spend.vout.emplace_back(
        12 * COIN,
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                recipient_hash}));

    // Verify the fixed PQ Authorization v1 marginal input
    // sizing for both previous outputs.
    for (const CTxOut& prevout : funding_tx->vout) {
        BOOST_CHECK_EQUAL(
            CalculateMaximumSignedInputSize(
                prevout,
                wallet.get(),
                /*coin_control=*/nullptr),
            5309);

        BOOST_CHECK_EQUAL(
            CalculateMaximumSignedInputWeight(
                prevout,
                wallet.get(),
                /*coin_control=*/nullptr),
            5432);
    }

    // Estimate the complete unsigned transaction before witnesses
    // are attached. With two native PQ inputs and one PQ output,
    // the estimator must account for both fixed-size ML-DSA
    // authorization witnesses.
    const TxSize estimated{
        CalculateMaximumSignedTxSize(
            CTransaction{spend},
            wallet.get(),
            std::vector<CTxOut>{
                funding_tx->vout.at(0),
                funding_tx->vout.at(1)},
            /*coin_control=*/nullptr)
    };

    BOOST_CHECK_EQUAL(
        estimated.vsize,
        10673);

    BOOST_CHECK_EQUAL(
        estimated.weight,
        11078);

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->SignTransaction(
                spend));
    }

    BOOST_REQUIRE_EQUAL(
        spend.vin.size(),
        2U);

    for (const CTxIn& input : spend.vin) {
        BOOST_CHECK(
            input.scriptSig.empty());

        BOOST_REQUIRE_EQUAL(
            input.scriptWitness.stack.size(),
            2U);

        BOOST_CHECK_EQUAL(
            input.scriptWitness.stack.at(0).size(),
            MERCATURA_MLDSA65_SIGNATURE_SIZE);

        BOOST_CHECK_EQUAL(
            input.scriptWitness.stack.at(1).size(),
            MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);
    }

    // Compare the pre-signing estimator against the exact
    // serialization of the real two-input ML-DSA transaction.
    const CTransaction serialized_tx{
        spend
    };

    BOOST_CHECK_EQUAL(
        estimated.vsize,
        static_cast<int64_t>(
            GetSerializeSize(
                TX_WITH_WITNESS(serialized_tx))));

    BOOST_CHECK_EQUAL(
        estimated.weight,
        GetTransactionWeight(
            serialized_tx));

    std::vector<CTxOut> spent_outputs{
        funding_tx->vout.at(0),
        funding_tx->vout.at(1)
    };

    PrecomputedTransactionData txdata;

    txdata.Init(
        spend,
        std::move(spent_outputs),
        /*force=*/true);

    BOOST_REQUIRE(
        txdata.m_pq_ready);

    for (size_t i = 0; i < spend.vin.size(); ++i) {
        MutableTransactionSignatureChecker checker{
            &spend,
            static_cast<unsigned int>(i),
            funding_tx->vout.at(i).nValue,
            txdata,
            MissingDataBehavior::FAIL,
            std::optional<uint256>{
                Params().GetConsensus().hashGenesisBlock}
        };

        ScriptError error{
            SCRIPT_ERR_UNKNOWN_ERROR
        };

        BOOST_CHECK(
            VerifyScript(
                spend.vin.at(i).scriptSig,
                funding_tx->vout.at(i).scriptPubKey,
                &spend.vin.at(i).scriptWitness,
                STANDARD_SCRIPT_VERIFY_FLAGS,
                checker,
                &error));

        BOOST_CHECK_EQUAL(
            error,
            SCRIPT_ERR_OK);
    }

    // Each input commits to its own current-input index,
    // so the two valid signatures must not be interchangeable.
    auto swapped{
        spend
    };

    std::swap(
        swapped.vin.at(0).scriptWitness,
        swapped.vin.at(1).scriptWitness);

    std::vector<CTxOut> swapped_spent_outputs{
        funding_tx->vout.at(0),
        funding_tx->vout.at(1)
    };

    PrecomputedTransactionData swapped_txdata;

    swapped_txdata.Init(
        swapped,
        std::move(swapped_spent_outputs),
        /*force=*/true);

    MutableTransactionSignatureChecker swapped_checker{
        &swapped,
        /*nIn=*/0,
        funding_tx->vout.at(0).nValue,
        swapped_txdata,
        MissingDataBehavior::FAIL,
        std::optional<uint256>{
            Params().GetConsensus().hashGenesisBlock}
    };

    ScriptError swapped_error{
        SCRIPT_ERR_UNKNOWN_ERROR
    };

    BOOST_CHECK(
        !VerifyScript(
            swapped.vin.at(0).scriptSig,
            funding_tx->vout.at(0).scriptPubKey,
            &swapped.vin.at(0).scriptWitness,
            STANDARD_SCRIPT_VERIFY_FLAGS,
            swapped_checker,
            &swapped_error));

    WaitForDeleteWallet(
        std::move(wallet));
}



BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_multi_input_atomic_failure, BasicTestingSetup)
{
    CWallet wallet{
        m_node.chain.get(),
        "",
        CreateMockableWalletDatabase()
    };

    CKeyingMaterial master_seed(
        MERCATURA_PQ_WALLET_MASTER_SEED_SIZE);

    for (size_t i = 0; i < master_seed.size(); ++i) {
        master_seed[i] =
            static_cast<unsigned char>(0x90 + i);
    }

    const auto derive_commitment =
        [&](uint8_t branch, uint32_t index) {
            const std::span<
                const unsigned char,
                MERCATURA_PQ_MASTER_SEED_SIZE>
                seed_span{
                    master_seed.data(),
                    MERCATURA_PQ_MASTER_SEED_SIZE
                };

            auto child_seed{
                DeriveMercaturaPQChildSeed(
                    seed_span,
                    Params().GetConsensus().hashGenesisBlock,
                    /*account=*/0,
                    branch,
                    index)
            };

            BOOST_REQUIRE(
                child_seed.has_value());

            std::array<
                uint8_t,
                MERCATURA_MLDSA65_PUBLIC_KEY_SIZE>
                public_key{};

            std::array<
                uint8_t,
                MERCATURA_MLDSA65_SECRET_KEY_SIZE>
                secret_key{};

            BOOST_REQUIRE_EQUAL(
                mercatura_mldsa65_keypair_from_seed(
                    public_key.data(),
                    public_key.size(),
                    secret_key.data(),
                    secret_key.size(),
                    child_seed->data(),
                    child_seed->size()),
                1);

            MercaturaPQKeyCommitment commitment{};

            BOOST_REQUIRE(
                ComputeMercaturaPQKeyCommitmentV1(
                    commitment,
                    public_key));

            memory_cleanse(
                child_seed->data(),
                child_seed->size());

            memory_cleanse(
                secret_key.data(),
                secret_key.size());

            return commitment;
        };

    const MercaturaPQKeyCommitment commitment_a{
        derive_commitment(
            /*branch=*/0,
            /*index=*/0)
    };

    const MercaturaPQKeyCommitment commitment_b{
        derive_commitment(
            /*branch=*/0,
            /*index=*/1)
    };

    const MercaturaPQKeyCommitment wrong_commitment{
        derive_commitment(
            /*branch=*/1,
            /*index=*/0)
    };

    BOOST_REQUIRE(
        commitment_a != commitment_b);

    BOOST_REQUIRE(
        commitment_b != wrong_commitment);

    const MercaturaPQKeyLocator locator_a{
        /*account=*/0,
        /*branch=*/0,
        /*index=*/0,
    };

    // Deliberately wrong for commitment_b.
    //
    // This locator is structurally and state-valid, so PQ signing
    // reaches the signing loop. It derives branch 1 / index 0,
    // whose key commitment does not match commitment_b.
    const MercaturaPQKeyLocator wrong_locator_b{
        /*account=*/0,
        /*branch=*/1,
        /*index=*/0,
    };

    MercaturaPQWalletState state;
    state.account = 0;
    state.next_external_index = 1;
    state.next_internal_index = 1;

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
                commitment_a,
                locator_a));

        BOOST_REQUIRE(
            wallet.LoadMercaturaPQKeyLocator(
                commitment_b,
                wrong_locator_b));

        BOOST_REQUIRE(
            wallet.ValidateMercaturaPQKeyLocators());
    }

    memory_cleanse(
        master_seed.data(),
        master_seed.size());

    master_seed.clear();

    const uint256 commitment_a_hash{
        std::span<const unsigned char>{
            commitment_a}
    };

    const uint256 commitment_b_hash{
        std::span<const unsigned char>{
            commitment_b}
    };

    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        6 * COIN,
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                commitment_a_hash}));

    funding.vout.emplace_back(
        7 * COIN,
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                commitment_b_hash}));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    {
        LOCK(wallet.cs_wallet);

        BOOST_REQUIRE(
            wallet.AddToWallet(
                funding_tx,
                TxStateInactive{},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);
    }

    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0xd4);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    CMutableTransaction spend;
    spend.version = 2;

    spend.vin.emplace_back(
        COutPoint{
            funding_tx->GetHash(),
            0});

    spend.vin.emplace_back(
        COutPoint{
            funding_tx->GetHash(),
            1});

    spend.vout.emplace_back(
        12 * COIN,
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                recipient_hash}));

    BOOST_REQUIRE_EQUAL(
        spend.vin.size(),
        2U);

    for (const CTxIn& input : spend.vin) {
        BOOST_CHECK(
            input.scriptSig.empty());

        BOOST_CHECK(
            input.scriptWitness.stack.empty());
    }

    {
        LOCK(wallet.cs_wallet);

        BOOST_CHECK(
            !wallet.SignTransaction(
                spend));
    }

    // Input 0 successfully reaches signing and stages a witness.
    // Input 1 then fails its derived-key commitment check.
    //
    // No staged witness may be committed unless every PQ input
    // completes successfully.
    for (const CTxIn& input : spend.vin) {
        BOOST_CHECK(
            input.scriptSig.empty());

        BOOST_CHECK(
            input.scriptWitness.stack.empty());
    }
}


BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_randomized_signing, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    auto wallet{
        TestCreateWallet(
            CreateMockableWalletDatabase(),
            context,
            WALLET_FLAG_DESCRIPTORS)
    };

    BOOST_REQUIRE(wallet);

    auto receive_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-randomized-signing-source")
    };

    BOOST_REQUIRE(receive_result);

    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        10 * COIN,
        GetScriptForDestination(
            *receive_result));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->AddToWallet(
                funding_tx,
                TxStateInactive{},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);
    }

    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0xd3);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    CMutableTransaction unsigned_spend;
    unsigned_spend.version = 2;

    unsigned_spend.vin.emplace_back(
        COutPoint{
            funding_tx->GetHash(),
            0});

    unsigned_spend.vout.emplace_back(
        9 * COIN,
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                recipient_hash}));

    CMutableTransaction spend_a{
        unsigned_spend
    };

    CMutableTransaction spend_b{
        unsigned_spend
    };

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->SignTransaction(
                spend_a));

        BOOST_REQUIRE(
            wallet->SignTransaction(
                spend_b));
    }

    BOOST_REQUIRE_EQUAL(
        spend_a.vin.at(0)
            .scriptWitness.stack.size(),
        2U);

    BOOST_REQUIRE_EQUAL(
        spend_b.vin.at(0)
            .scriptWitness.stack.size(),
        2U);

    const auto& signature_a{
        spend_a.vin.at(0)
            .scriptWitness.stack.at(0)
    };

    const auto& signature_b{
        spend_b.vin.at(0)
            .scriptWitness.stack.at(0)
    };

    const auto& public_key_a{
        spend_a.vin.at(0)
            .scriptWitness.stack.at(1)
    };

    const auto& public_key_b{
        spend_b.vin.at(0)
            .scriptWitness.stack.at(1)
    };

    BOOST_REQUIRE_EQUAL(
        signature_a.size(),
        MERCATURA_MLDSA65_SIGNATURE_SIZE);

    BOOST_REQUIRE_EQUAL(
        signature_b.size(),
        MERCATURA_MLDSA65_SIGNATURE_SIZE);

    BOOST_REQUIRE_EQUAL(
        public_key_a.size(),
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

    BOOST_REQUIRE_EQUAL(
        public_key_b.size(),
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

    // Deterministic wallet derivation must reproduce the same
    // public key for the same owned output.
    BOOST_CHECK_EQUAL_COLLECTIONS(
        public_key_a.begin(),
        public_key_a.end(),
        public_key_b.begin(),
        public_key_b.end());

    // Production signing supplies fresh 32-byte CSPRNG input to
    // ML-DSA. Signing the identical authorization digest twice
    // should therefore produce independently randomized signatures.
    BOOST_CHECK(
        signature_a != signature_b);

    auto verify_signed_tx =
        [&](CMutableTransaction& spend) {
            std::vector<CTxOut> spent_outputs{
                funding_tx->vout.at(0)
            };

            PrecomputedTransactionData txdata;

            txdata.Init(
                spend,
                std::move(spent_outputs),
                /*force=*/true);

            BOOST_REQUIRE(
                txdata.m_pq_ready);

            MutableTransactionSignatureChecker checker{
                &spend,
                /*nIn=*/0,
                funding_tx->vout.at(0).nValue,
                txdata,
                MissingDataBehavior::FAIL,
                std::optional<uint256>{
                    Params().GetConsensus().hashGenesisBlock}
            };

            ScriptError error{
                SCRIPT_ERR_UNKNOWN_ERROR
            };

            BOOST_CHECK(
                VerifyScript(
                    spend.vin.at(0).scriptSig,
                    funding_tx->vout.at(0).scriptPubKey,
                    &spend.vin.at(0).scriptWitness,
                    STANDARD_SCRIPT_VERIFY_FLAGS,
                    checker,
                    &error));

            BOOST_CHECK_EQUAL(
                error,
                SCRIPT_ERR_OK);
        };

    verify_signed_tx(
        spend_a);

    verify_signed_tx(
        spend_b);

    WaitForDeleteWallet(
        std::move(wallet));
}



BOOST_FIXTURE_TEST_CASE(mercatura_pq_wallet_no_classical_fallback, ListCoinsTestingSetup)
{
    // Construct a transaction containing both native Mercatura PQ
    // ownership and an inherited classical ownership script.
    //
    // Once any PQ input exists, the dedicated PQ signer must own the
    // entire transaction and reject the classical input before the
    // inherited ScriptPubKeyMan compatibility path can be reached.
    {
        LOCK(wallet->cs_wallet);

        if (!wallet->HasMercaturaPQState()) {
            BOOST_REQUIRE(
                wallet->InitializeMercaturaPQWallet());
        }
    }

    auto pq_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-no-classical-fallback")
    };

    BOOST_REQUIRE(pq_result);

    const CTxDestination pq_destination{
        *pq_result
    };

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &pq_destination) != nullptr);

    const CScript classical_script{
        GetScriptForDestination(
            PKHash{
                coinbaseKey.GetPubKey()})
    };

    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        8 * COIN,
        GetScriptForDestination(
            pq_destination));

    funding.vout.emplace_back(
        8 * COIN,
        classical_script);

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    const COutPoint pq_outpoint{
        funding_tx->GetHash(),
        0
    };

    const COutPoint classical_outpoint{
        funding_tx->GetHash(),
        1
    };

    std::map<COutPoint, Coin> coins;

    coins.emplace(
        pq_outpoint,
        Coin{
            funding_tx->vout.at(0),
            /*height=*/0,
            /*coinbase=*/false
        });

    coins.emplace(
        classical_outpoint,
        Coin{
            funding_tx->vout.at(1),
            /*height=*/0,
            /*coinbase=*/false
        });

    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0xac);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    const CScript recipient_script{
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                recipient_hash})
    };

    // ------------------------------------------------------------
    // Mixed ownership case:
    //
    // Once any native Mercatura PQ input exists, the dedicated PQ
    // signer owns the entire transaction. A classical ownership input
    // is forbidden and must never fall through to ScriptPubKeyMan.
    // ------------------------------------------------------------
    CMutableTransaction mixed;
    mixed.version = 2;

    mixed.vin.emplace_back(
        pq_outpoint);

    mixed.vin.emplace_back(
        classical_outpoint);

    mixed.vout.emplace_back(
        15 * COIN,
        recipient_script);

    BOOST_REQUIRE_EQUAL(
        mixed.vin.size(),
        2U);

    for (const CTxIn& input :
         mixed.vin) {
        BOOST_CHECK(
            input.scriptSig.empty());

        BOOST_CHECK(
            input.scriptWitness.stack.empty());
    }

    std::map<int, bilingual_str> mixed_errors;

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK(
            !wallet->SignTransaction(
                mixed,
                coins,
                SIGHASH_DEFAULT,
                mixed_errors));
    }

    BOOST_REQUIRE(
        mixed_errors.contains(1));

    BOOST_CHECK_MESSAGE(
        mixed_errors.at(1)
            .original.find(
                "contains a non-PQ ownership input") !=
        std::string::npos,
        "Unexpected mixed-input error: " <<
            mixed_errors.at(1).original);

    // No partial PQ witness or classical signature may appear.
    // The specific non-PQ error above proves rejection occurred inside
    // the dedicated PQ branch, which returns before the inherited
    // ScriptPubKeyMan compatibility loop.
    for (const CTxIn& input :
         mixed.vin) {
        BOOST_CHECK(
            input.scriptSig.empty());

        BOOST_CHECK(
            input.scriptWitness.stack.empty());
    }
}


BOOST_FIXTURE_TEST_CASE(mercatura_pq_signed_size_estimation, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;

    auto wallet{
        TestCreateWallet(
            CreateMockableWalletDatabase(),
            context,
            WALLET_FLAG_DESCRIPTORS)
    };

    BOOST_REQUIRE(wallet);

    MercaturaPQKeyCommitment commitment{};

    for (size_t i = 0; i < commitment.size(); ++i) {
        commitment[i] =
            static_cast<unsigned char>(i);
    }

    const uint256 commitment_hash{
        std::span<const unsigned char>{
            commitment}
    };

    const CTxOut pq_prevout{
        10 * COIN,
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                commitment_hash})
    };

    // PQ estimation must not depend on a Bitcoin descriptor
    // or secp256k1 SigningProvider.
    BOOST_CHECK_EQUAL(
        CalculateMaximumSignedInputSize(
            pq_prevout,
            wallet.get(),
            /*coin_control=*/nullptr),
        5309);

    BOOST_CHECK_EQUAL(
        CalculateMaximumSignedInputWeight(
            pq_prevout,
            wallet.get(),
            /*coin_control=*/nullptr),
        5432);

    CMutableTransaction unsigned_tx;
    unsigned_tx.version = 2;

    unsigned_tx.vin.emplace_back(
        COutPoint{
            Txid{},
            0});

    unsigned_tx.vout.emplace_back(
        9 * COIN,
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                commitment_hash}));

    const TxSize estimated{
        CalculateMaximumSignedTxSize(
            CTransaction{
                unsigned_tx},
            wallet.get(),
            std::vector<CTxOut>{
                pq_prevout},
            /*coin_control=*/nullptr)
    };

    BOOST_CHECK_EQUAL(
        estimated.vsize,
        5364);

    BOOST_CHECK_EQUAL(
        estimated.weight,
        5646);

    // Construct the exact fixed PQ Authorization v1 witness and
    // confirm the estimator matches real serialization exactly.
    CMutableTransaction signed_tx{
        unsigned_tx
    };

    signed_tx.vin.at(0)
        .scriptWitness.stack = {
            std::vector<unsigned char>(
                MERCATURA_MLDSA65_SIGNATURE_SIZE,
                0x11),
            std::vector<unsigned char>(
                MERCATURA_MLDSA65_PUBLIC_KEY_SIZE,
                0x22),
        };

    const CTransaction serialized_tx{
        signed_tx
    };

    BOOST_CHECK_EQUAL(
        estimated.vsize,
        static_cast<int64_t>(
            GetSerializeSize(
                TX_WITH_WITNESS(serialized_tx))));

    BOOST_CHECK_EQUAL(
        estimated.weight,
        GetTransactionWeight(
            serialized_tx));

    WaitForDeleteWallet(
        std::move(wallet));
}



BOOST_FIXTURE_TEST_CASE(mercatura_pq_available_coins_spendability, ListCoinsTestingSetup)
{
    {
        LOCK(wallet->cs_wallet);

        if (!wallet->HasMercaturaPQState()) {
            BOOST_REQUIRE(
                wallet->InitializeMercaturaPQWallet());
        }
    }

    auto receive_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-available-coins")
    };

    BOOST_REQUIRE(receive_result);

    const CTxDestination owned_destination{
        *receive_result
    };

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &owned_destination) != nullptr);

    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        15 * COIN,
        GetScriptForDestination(
            owned_destination));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    uint256 tip_hash;
    int tip_height;

    {
        LOCK(
            Assert(m_node.chainman)
                ->GetMutex());

        const CBlockIndex* tip{
            m_node.chainman
                ->ActiveChain()
                .Tip()
        };

        BOOST_REQUIRE(tip);

        tip_hash =
            tip->GetBlockHash();

        tip_height =
            tip->nHeight;
    }

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->AddToWallet(
                funding_tx,
                TxStateConfirmed{
                    tip_hash,
                    tip_height,
                    /*position=*/1},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);
    }

    const COutPoint funding_outpoint{
        funding_tx->GetHash(),
        0
    };

    const auto check_available =
        [&]() {
            LOCK(wallet->cs_wallet);

            const CoinsResult available{
                AvailableCoins(
                    *wallet)
            };

            bool found{false};

            for (const COutput& output :
                 available.All()) {
                if (output.outpoint !=
                    funding_outpoint) {
                    continue;
                }

                found = true;

                BOOST_CHECK(
                    output.solvable);

                BOOST_CHECK(
                    output.safe);

                BOOST_CHECK_EQUAL(
                    output.input_bytes,
                    5309);

                BOOST_CHECK_EQUAL(
                    output.input_weight,
                    5432);

                BOOST_CHECK_EQUAL(
                    output.txout.nValue,
                    15 * COIN);
            }

            BOOST_CHECK(
                found);
        };

    // Normal unlocked wallet discovery.
    check_available();

    SecureString passphrase;
    passphrase.assign(
        "mercatura-pq-available-coins-passphrase");

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->EncryptWallet(
                passphrase));

        BOOST_REQUIRE(
            wallet->IsLocked());

        // Ownership is public commitment/locator metadata and must
        // remain known while the secret PQ master seed is locked.
        BOOST_CHECK(
            wallet->IsMine(
                funding_tx->vout.at(0)));
    }

    // Locking prevents signing/key derivation, but must not hide an
    // already-owned PQ UTXO or make its fixed spend size unsolvable.
    check_available();

    CMutableTransaction spend;
    spend.version = 2;

    spend.vin.emplace_back(
        funding_outpoint);

    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0xe5);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    spend.vout.emplace_back(
        14 * COIN,
        GetScriptForDestination(
            WitnessV2MercaturaPQ{
                recipient_hash}));

    {
        LOCK(wallet->cs_wallet);

        BOOST_CHECK(
            !wallet->SignTransaction(
                spend));

        BOOST_CHECK(
            spend.vin.at(0)
                .scriptWitness.stack.empty());

        BOOST_REQUIRE(
            wallet->Unlock(
                passphrase));

        BOOST_REQUIRE(
            wallet->SignTransaction(
                spend));
    }

    BOOST_REQUIRE_EQUAL(
        spend.vin.at(0)
            .scriptWitness.stack.size(),
        2U);
}


BOOST_FIXTURE_TEST_CASE(mercatura_pq_create_transaction_signed, ListCoinsTestingSetup)
{
    // Some inherited test-wallet construction paths bypass normal
    // Mercatura wallet creation. Ensure this fixture has PQ state.
    {
        LOCK(wallet->cs_wallet);

        if (!wallet->HasMercaturaPQState()) {
            BOOST_REQUIRE(
                wallet->InitializeMercaturaPQWallet());
        }
    }

    // ------------------------------------------------------------
    // Create an owned PQ funding output.
    // ------------------------------------------------------------
    auto source_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-create-transaction-source")
    };

    BOOST_REQUIRE(source_result);

    const CTxDestination source_destination{
        *source_result
    };

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &source_destination) != nullptr);

    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        20 * COIN,
        GetScriptForDestination(
            source_destination));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    // Give the synthetic wallet funding transaction a confirmed state
    // at the active tip so normal AvailableCoins/coin-selection logic
    // treats it as spendable.
    uint256 tip_hash;
    int tip_height;

    {
        LOCK(
            Assert(m_node.chainman)
                ->GetMutex());

        const CBlockIndex* tip{
            m_node.chainman
                ->ActiveChain()
                .Tip()
        };

        BOOST_REQUIRE(tip);

        tip_hash =
            tip->GetBlockHash();

        tip_height =
            tip->nHeight;
    }

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->AddToWallet(
                funding_tx,
                TxStateConfirmed{
                    tip_hash,
                    tip_height,
                    /*index=*/1},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);

        BOOST_CHECK(
            wallet->IsMine(
                funding_tx->vout.at(0)));
    }

    const COutPoint funding_outpoint{
        funding_tx->GetHash(),
        0
    };

    // ------------------------------------------------------------
    // Construct an unrelated PQ recipient.
    // ------------------------------------------------------------
    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0xe4);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    const CTxDestination recipient_destination{
        WitnessV2MercaturaPQ{
            recipient_hash}
    };

    BOOST_CHECK(
        !wallet->IsMine(
            recipient_destination));

    // ------------------------------------------------------------
    // Force normal CreateTransaction() to use exactly our PQ UTXO.
    // This exercises:
    //
    // AvailableCoins / selected-input handling
    // PQ size estimation
    // fee calculation
    // PQ change generation
    // CWallet::SignTransaction
    // ------------------------------------------------------------
    CCoinControl coin_control;

    coin_control.Select(
        funding_outpoint);

    coin_control.m_allow_other_inputs =
        false;

    // Use the Mercatura minimum fee rate deterministically rather
    // than relying on the fixture's fee estimator.
    coin_control.m_feerate =
        CFeeRate{1};

    coin_control.fOverrideFeeRate =
        true;

    const CRecipient recipient{
        recipient_destination,
        10 * COIN,
        /*subtract_fee=*/false
    };

    auto result{
        CreateTransaction(
            *wallet,
            {recipient},
            /*change_pos=*/std::nullopt,
            coin_control,
            /*sign=*/true)
    };

    const std::string result_error{
        result
            ? std::string{}
            : util::ErrorString(result).original
    };

    BOOST_REQUIRE_MESSAGE(
        static_cast<bool>(result),
        result_error);

    const CTransactionRef& tx{
        result->tx
    };

    // ------------------------------------------------------------
    // Input must be the selected PQ UTXO and fully ML-DSA signed.
    // ------------------------------------------------------------
    BOOST_REQUIRE_EQUAL(
        tx->vin.size(),
        1U);

    BOOST_CHECK(
        tx->vin.at(0).prevout ==
        funding_outpoint);

    BOOST_CHECK(
        tx->vin.at(0)
            .scriptSig.empty());

    BOOST_REQUIRE_EQUAL(
        tx->vin.at(0)
            .scriptWitness.stack.size(),
        2U);

    BOOST_CHECK_EQUAL(
        tx->vin.at(0)
            .scriptWitness.stack.at(0).size(),
        MERCATURA_MLDSA65_SIGNATURE_SIZE);

    BOOST_CHECK_EQUAL(
        tx->vin.at(0)
            .scriptWitness.stack.at(1).size(),
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

    // ------------------------------------------------------------
    // We intentionally funded 20 MCA and sent 10 MCA, so normal
    // transaction construction should create a PQ change output.
    // ------------------------------------------------------------
    BOOST_REQUIRE(
        result->change_pos.has_value());

    BOOST_REQUIRE(
        *result->change_pos <
        tx->vout.size());

    const CTxOut& change_output{
        tx->vout.at(
            *result->change_pos)
    };

    CTxDestination change_destination;

    BOOST_REQUIRE(
        ExtractDestination(
            change_output.scriptPubKey,
            change_destination));

    BOOST_CHECK(
        std::get_if<WitnessV2MercaturaPQ>(
            &change_destination) != nullptr);

    BOOST_CHECK(
        wallet->IsMine(
            change_destination));

    BOOST_CHECK_GT(
        change_output.nValue,
        0);

    // The unrelated recipient must remain non-owned.
    for (size_t i = 0;
         i < tx->vout.size();
         ++i) {
        if (i ==
            *result->change_pos) {
            continue;
        }

        CTxDestination output_destination;

        if (ExtractDestination(
                tx->vout.at(i).scriptPubKey,
                output_destination)) {
            BOOST_CHECK(
                !wallet->IsMine(
                    output_destination));
        }
    }

    // ------------------------------------------------------------
    // Fee accounting must use Mercatura's full serialized bytes.
    // ------------------------------------------------------------
    const int64_t actual_size{
        static_cast<int64_t>(
            GetSerializeSize(
                TX_WITH_WITNESS(*tx)))
    };

    BOOST_CHECK_GT(
        actual_size,
        5309);

    BOOST_CHECK_GT(
        result->fee,
        0);

    BOOST_CHECK_EQUAL(
        result->fee,
        CFeeRate{1}.GetFee(
            actual_size));

    // ------------------------------------------------------------
    // Finally verify the wallet-generated witness using the actual
    // witness-v2 PQ consensus verifier.
    // ------------------------------------------------------------
    std::vector<CTxOut> spent_outputs{
        funding_tx->vout.at(0)
    };

    PrecomputedTransactionData txdata;

    txdata.Init(
        *tx,
        std::move(spent_outputs),
        /*force=*/true);

    BOOST_REQUIRE(
        txdata.m_pq_ready);

    TransactionSignatureChecker checker{
        tx.get(),
        /*nIn=*/0,
        funding_tx->vout.at(0).nValue,
        txdata,
        MissingDataBehavior::FAIL,
        std::optional<uint256>{
            Params()
                .GetConsensus()
                .hashGenesisBlock}
    };

    ScriptError error{
        SCRIPT_ERR_UNKNOWN_ERROR
    };

    BOOST_CHECK(
        VerifyScript(
            tx->vin.at(0).scriptSig,
            funding_tx->vout.at(0).scriptPubKey,
            &tx->vin.at(0).scriptWitness,
            STANDARD_SCRIPT_VERIFY_FLAGS,
            checker,
            &error));

    BOOST_CHECK_EQUAL(
        error,
        SCRIPT_ERR_OK);
}



BOOST_FIXTURE_TEST_CASE(mercatura_pq_create_transaction_multi_input_fee, ListCoinsTestingSetup)
{
    {
        LOCK(wallet->cs_wallet);

        if (!wallet->HasMercaturaPQState()) {
            BOOST_REQUIRE(
                wallet->InitializeMercaturaPQWallet());
        }
    }

    auto source_a{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-multi-fee-a")
    };

    auto source_b{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-multi-fee-b")
    };

    BOOST_REQUIRE(source_a);
    BOOST_REQUIRE(source_b);

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &*source_a) != nullptr);

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &*source_b) != nullptr);

    // Create two separately owned PQ UTXOs.
    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        8 * COIN,
        GetScriptForDestination(
            *source_a));

    funding.vout.emplace_back(
        8 * COIN,
        GetScriptForDestination(
            *source_b));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    uint256 tip_hash;
    int tip_height;

    {
        LOCK(
            Assert(m_node.chainman)
                ->GetMutex());

        const CBlockIndex* tip{
            m_node.chainman
                ->ActiveChain()
                .Tip()
        };

        BOOST_REQUIRE(tip);

        tip_hash =
            tip->GetBlockHash();

        tip_height =
            tip->nHeight;
    }

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->AddToWallet(
                funding_tx,
                TxStateConfirmed{
                    tip_hash,
                    tip_height,
                    /*index=*/1},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);
    }

    const COutPoint outpoint_a{
        funding_tx->GetHash(),
        0
    };

    const COutPoint outpoint_b{
        funding_tx->GetHash(),
        1
    };

    // Force both PQ inputs into the transaction. This ensures the
    // fee calculation cannot satisfy the payment using only one.
    CCoinControl coin_control;

    coin_control.Select(
        outpoint_a);

    coin_control.Select(
        outpoint_b);

    coin_control.m_allow_other_inputs =
        false;

    const CFeeRate fee_rate{1};

    coin_control.m_feerate =
        fee_rate;

    coin_control.fOverrideFeeRate =
        true;

    MercaturaPQKeyCommitment recipient_commitment{};
    recipient_commitment.fill(0x93);

    const uint256 recipient_hash{
        std::span<const unsigned char>{
            recipient_commitment}
    };

    const CTxDestination recipient{
        WitnessV2MercaturaPQ{
            recipient_hash}
    };

    std::vector<CRecipient> recipients{
        CRecipient{
            recipient,
            15 * COIN,
            /*subtract_fee=*/false},
    };

    auto result{
        CreateTransaction(
            *wallet,
            recipients,
            /*change_pos=*/std::nullopt,
            coin_control,
            /*sign=*/true)
    };

    const std::string result_error{
        result
            ? std::string{}
            : util::ErrorString(result).original
    };

    BOOST_REQUIRE_MESSAGE(
        static_cast<bool>(result),
        result_error);

    const CTransactionRef& tx{
        result->tx
    };

    // Both explicitly selected PQ inputs must be present.
    BOOST_REQUIRE_EQUAL(
        tx->vin.size(),
        2U);

    bool found_a{false};
    bool found_b{false};

    for (const CTxIn& input :
         tx->vin) {
        if (input.prevout == outpoint_a) {
            found_a = true;
        }

        if (input.prevout == outpoint_b) {
            found_b = true;
        }

        BOOST_CHECK(
            input.scriptSig.empty());

        BOOST_REQUIRE_EQUAL(
            input.scriptWitness.stack.size(),
            2U);

        BOOST_CHECK_EQUAL(
            input.scriptWitness.stack.at(0).size(),
            MERCATURA_MLDSA65_SIGNATURE_SIZE);

        BOOST_CHECK_EQUAL(
            input.scriptWitness.stack.at(1).size(),
            MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);
    }

    BOOST_CHECK(found_a);
    BOOST_CHECK(found_b);

    // Fee reported by CreateTransaction must equal the value actually
    // removed from the selected inputs.
    CAmount output_total{0};

    for (const CTxOut& output :
         tx->vout) {
        output_total +=
            output.nValue;
    }

    const CAmount input_total{
        16 * COIN
    };

    BOOST_CHECK_EQUAL(
        result->fee,
        input_total - output_total);

    // Re-estimate the completed transaction through the wallet's
    // production PQ sizing path.
    const TxSize estimated{
        CalculateMaximumSignedTxSize(
            *tx,
            wallet.get(),
            &coin_control)
    };

    BOOST_REQUIRE(
        estimated.vsize > 0);

    BOOST_REQUIRE(
        estimated.weight > 0);

    const int64_t actual_size{
        static_cast<int64_t>(
            GetSerializeSize(
                TX_WITH_WITNESS(*tx)))
    };

    // The normal CreateTransaction transaction must match the exact
    // two-input PQ full-byte estimator.
    BOOST_CHECK_EQUAL(
        estimated.vsize,
        actual_size);

    // With the explicitly requested fee rate, the transaction must
    // never pay less than its real full serialized PQ size requires.
    const CAmount required_fee{
        fee_rate.GetFee(
            actual_size)
    };

    BOOST_CHECK_GE(
        result->fee,
        required_fee);

    // Regression guard against accidentally reverting to inherited
    // Bitcoin/P2WPKH-style ~68-byte input accounting.
    //
    // Replace each real 5309-byte PQ input hypothetically with a
    // 68-byte inherited input and confirm the resulting fee would
    // be materially lower than the fee CreateTransaction selected.
    constexpr int64_t PQ_INPUT_FEE_SIZE{5309};
    constexpr int64_t INHERITED_SMALL_INPUT_SIZE{68};

    const int64_t inherited_size{
        actual_size -
        2 * PQ_INPUT_FEE_SIZE +
        2 * INHERITED_SMALL_INPUT_SIZE
    };

    BOOST_REQUIRE(
        inherited_size > 0);

    const CAmount inherited_fee{
        fee_rate.GetFee(
            inherited_size)
    };

    BOOST_CHECK_GT(
        result->fee,
        inherited_fee);
}


BOOST_FIXTURE_TEST_CASE(mercatura_pq_create_transaction_multi_output, ListCoinsTestingSetup)
{
    {
        LOCK(wallet->cs_wallet);

        if (!wallet->HasMercaturaPQState()) {
            BOOST_REQUIRE(
                wallet->InitializeMercaturaPQWallet());
        }
    }

    auto source_result{
        wallet->GetNewDestination(
            OutputType::BECH32,
            "pq-batched-source")
    };

    BOOST_REQUIRE(source_result);

    const CTxDestination source_destination{
        *source_result
    };

    BOOST_REQUIRE(
        std::get_if<WitnessV2MercaturaPQ>(
            &source_destination) != nullptr);

    CMutableTransaction funding;
    funding.version = 2;

    funding.vout.emplace_back(
        30 * COIN,
        GetScriptForDestination(
            source_destination));

    const CTransactionRef funding_tx{
        MakeTransactionRef(
            std::move(funding))
    };

    uint256 tip_hash;
    int tip_height;

    {
        LOCK(
            Assert(m_node.chainman)
                ->GetMutex());

        const CBlockIndex* tip{
            m_node.chainman
                ->ActiveChain()
                .Tip()
        };

        BOOST_REQUIRE(tip);

        tip_hash =
            tip->GetBlockHash();

        tip_height =
            tip->nHeight;
    }

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(
            wallet->AddToWallet(
                funding_tx,
                TxStateConfirmed{
                    tip_hash,
                    tip_height,
                    /*index=*/1},
                [](CWalletTx&,
                   bool /*new_tx*/) {
                    return true;
                }) != nullptr);
    }

    const COutPoint funding_outpoint{
        funding_tx->GetHash(),
        0
    };

    MercaturaPQKeyCommitment commitment_a{};
    MercaturaPQKeyCommitment commitment_b{};

    commitment_a.fill(0x31);
    commitment_b.fill(0x72);

    const uint256 hash_a{
        std::span<const unsigned char>{
            commitment_a}
    };

    const uint256 hash_b{
        std::span<const unsigned char>{
            commitment_b}
    };

    const CTxDestination destination_a{
        WitnessV2MercaturaPQ{
            hash_a}
    };

    const CTxDestination destination_b{
        WitnessV2MercaturaPQ{
            hash_b}
    };

    const CScript script_a{
        GetScriptForDestination(
            destination_a)
    };

    const CScript script_b{
        GetScriptForDestination(
            destination_b)
    };

    BOOST_CHECK(
        !wallet->IsMine(
            destination_a));

    BOOST_CHECK(
        !wallet->IsMine(
            destination_b));

    CCoinControl coin_control;

    coin_control.Select(
        funding_outpoint);

    coin_control.m_allow_other_inputs =
        false;

    coin_control.m_feerate =
        CFeeRate{1};

    coin_control.fOverrideFeeRate =
        true;

    std::vector<CRecipient> recipients{
        CRecipient{
            destination_a,
            6 * COIN,
            /*subtract_fee=*/false},
        CRecipient{
            destination_b,
            7 * COIN,
            /*subtract_fee=*/false},
    };

    auto result{
        CreateTransaction(
            *wallet,
            recipients,
            /*change_pos=*/std::nullopt,
            coin_control,
            /*sign=*/true)
    };

    const std::string result_error{
        result
            ? std::string{}
            : util::ErrorString(result).original
    };

    BOOST_REQUIRE_MESSAGE(
        static_cast<bool>(result),
        result_error);

    const CTransactionRef& tx{
        result->tx
    };

    BOOST_REQUIRE_EQUAL(
        tx->vin.size(),
        1U);

    BOOST_REQUIRE_EQUAL(
        tx->vout.size(),
        3U);

    BOOST_CHECK(
        tx->vin.at(0).prevout ==
        funding_outpoint);

    BOOST_CHECK(
        tx->vin.at(0)
            .scriptSig.empty());

    BOOST_REQUIRE_EQUAL(
        tx->vin.at(0)
            .scriptWitness.stack.size(),
        2U);

    BOOST_CHECK_EQUAL(
        tx->vin.at(0)
            .scriptWitness.stack.at(0).size(),
        MERCATURA_MLDSA65_SIGNATURE_SIZE);

    BOOST_CHECK_EQUAL(
        tx->vin.at(0)
            .scriptWitness.stack.at(1).size(),
        MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

    BOOST_REQUIRE(
        result->change_pos.has_value());

    BOOST_REQUIRE(
        *result->change_pos <
        tx->vout.size());

    bool found_a{false};
    bool found_b{false};

    for (size_t i = 0;
         i < tx->vout.size();
         ++i) {
        const CTxOut& output{
            tx->vout.at(i)
        };

        if (i ==
            *result->change_pos) {
            CTxDestination change_destination;

            BOOST_REQUIRE(
                ExtractDestination(
                    output.scriptPubKey,
                    change_destination));

            BOOST_CHECK(
                std::get_if<WitnessV2MercaturaPQ>(
                    &change_destination) != nullptr);

            BOOST_CHECK(
                wallet->IsMine(
                    change_destination));

            BOOST_CHECK_GT(
                output.nValue,
                0);

            continue;
        }

        if (output.scriptPubKey ==
            script_a) {
            BOOST_CHECK_EQUAL(
                output.nValue,
                6 * COIN);

            found_a = true;
        }

        if (output.scriptPubKey ==
            script_b) {
            BOOST_CHECK_EQUAL(
                output.nValue,
                7 * COIN);

            found_b = true;
        }
    }

    BOOST_CHECK(found_a);
    BOOST_CHECK(found_b);

    const int64_t actual_size{
        static_cast<int64_t>(
            GetSerializeSize(
                TX_WITH_WITNESS(*tx)))
    };

    BOOST_CHECK_EQUAL(
        result->fee,
        CFeeRate{1}.GetFee(
            actual_size));

    std::vector<CTxOut> spent_outputs{
        funding_tx->vout.at(0)
    };

    PrecomputedTransactionData txdata;

    txdata.Init(
        *tx,
        std::move(spent_outputs),
        /*force=*/true);

    BOOST_REQUIRE(
        txdata.m_pq_ready);

    TransactionSignatureChecker checker{
        tx.get(),
        /*nIn=*/0,
        funding_tx->vout.at(0).nValue,
        txdata,
        MissingDataBehavior::FAIL,
        std::optional<uint256>{
            Params()
                .GetConsensus()
                .hashGenesisBlock}
    };

    ScriptError error{
        SCRIPT_ERR_UNKNOWN_ERROR
    };

    BOOST_CHECK(
        VerifyScript(
            tx->vin.at(0).scriptSig,
            funding_tx->vout.at(0).scriptPubKey,
            &tx->vin.at(0).scriptWitness,
            STANDARD_SCRIPT_VERIFY_FLAGS,
            checker,
            &error));

    BOOST_CHECK_EQUAL(
        error,
        SCRIPT_ERR_OK);
}


BOOST_FIXTURE_TEST_CASE(CreateWalletWithoutChain, BasicTestingSetup)
{
    WalletContext context;
    context.args = &m_args;
    auto wallet = TestCreateWallet(context);
    BOOST_CHECK(wallet);
    WaitForDeleteWallet(std::move(wallet));
}

BOOST_FIXTURE_TEST_CASE(RemoveTxs, TestChain100Setup)
{
    m_args.ForceSetArg("-unsafesqlitesync", "1");
    WalletContext context;
    context.args = &m_args;
    context.chain = m_node.chain.get();
    auto wallet = TestCreateWallet(context);
    CKey key = GenerateRandomKey();
    AddKey(*wallet, key);

    std::string error;
    m_coinbase_txns.push_back(CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);
    auto block_tx = TestSimpleSpend(*m_coinbase_txns[0], 0, coinbaseKey, GetScriptForRawPubKey(key.GetPubKey()));
    CreateAndProcessBlock({block_tx}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

    m_node.validation_signals->SyncWithValidationInterfaceQueue();

    {
        auto block_hash = block_tx.GetHash();
        auto prev_tx = m_coinbase_txns[0];

        LOCK(wallet->cs_wallet);
        BOOST_CHECK(wallet->HasWalletSpend(prev_tx));
        BOOST_CHECK(wallet->mapWallet.contains(block_hash));

        std::vector<Txid> vHashIn{ block_hash };
        BOOST_CHECK(wallet->RemoveTxs(vHashIn));

        BOOST_CHECK(!wallet->HasWalletSpend(prev_tx));
        BOOST_CHECK(!wallet->mapWallet.contains(block_hash));
    }

    TestUnloadWallet(std::move(wallet));
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
