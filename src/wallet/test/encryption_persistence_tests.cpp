// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <wallet/wallet.h>

#include <interfaces/chain.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/server.h>
#include <streams.h>
#include <util/translation.h>
#include <wallet/sqlite.h>
#include <wallet/context.h>
#include <wallet/rpc/wallet.h>
#include <wallet/test/wallet_test_fixture.h>
#include <wallet/walletdb.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <memory>
#include <string>

namespace wallet {
namespace {

// Only this test translation unit can install faults. All successful operations
// are delegated to a real SQLite database, including commit, rollback and reload.
enum class EncryptionFault { NONE, BEGIN, WRITE, ERASE, COMMIT, ABORT, REWRITE };

struct EncryptionFaultControl {
    EncryptionFault fault{EncryptionFault::NONE};
    std::string record;
    unsigned hits{0};

    bool Fail(EncryptionFault operation, const std::string& type = {})
    {
        if (fault != operation || (!record.empty() && type != record)) return false;
        ++hits;
        fault = EncryptionFault::NONE;
        return true;
    }
};

class EncryptionFaultBatch : public DatabaseBatch
{
    std::unique_ptr<DatabaseBatch> m_inner;
    EncryptionFaultControl& m_control;

    static std::string Type(const DataStream& key)
    {
        SpanReader reader{std::span<const std::byte>{key}};
        std::string type;
        reader >> type;
        return type;
    }

    bool ReadKey(DataStream&& key, DataStream& value) override
    {
        auto cursor = m_inner->GetNewPrefixCursor(std::span<const std::byte>{key});
        DataStream actual_key;
        if (cursor->Next(actual_key, value) != DatabaseCursor::Status::MORE) return false;
        return std::equal(key.begin(), key.end(), actual_key.begin(), actual_key.end());
    }
    bool WriteKey(DataStream&& key, DataStream&& value, bool overwrite) override
    {
        if (m_control.Fail(EncryptionFault::WRITE, Type(key))) return false;
        return m_inner->Write(std::span<const std::byte>{key}, std::span<const std::byte>{value}, overwrite);
    }
    bool EraseKey(DataStream&& key) override
    {
        if (m_control.Fail(EncryptionFault::ERASE, Type(key))) return false;
        return m_inner->Erase(std::span<const std::byte>{key});
    }
    bool HasKey(DataStream&& key) override { return m_inner->Exists(std::span<const std::byte>{key}); }
    bool ErasePrefix(std::span<const std::byte> prefix) override { return m_inner->ErasePrefix(prefix); }

public:
    EncryptionFaultBatch(std::unique_ptr<DatabaseBatch> inner, EncryptionFaultControl& control)
        : m_inner{std::move(inner)}, m_control{control} {}
    void Close() override { m_inner->Close(); }
    std::unique_ptr<DatabaseCursor> GetNewCursor() override { return m_inner->GetNewCursor(); }
    std::unique_ptr<DatabaseCursor> GetNewPrefixCursor(std::span<const std::byte> prefix) override { return m_inner->GetNewPrefixCursor(prefix); }
    bool TxnBegin() override { return !m_control.Fail(EncryptionFault::BEGIN) && m_inner->TxnBegin(); }
    bool TxnCommit() override { return !m_control.Fail(EncryptionFault::COMMIT) && m_inner->TxnCommit(); }
    bool TxnAbort() override { return !m_control.Fail(EncryptionFault::ABORT) && m_inner->TxnAbort(); }
    bool HasActiveTxn() override { return m_inner->HasActiveTxn(); }
};

class EncryptionFaultDatabase : public WalletDatabase
{
    std::unique_ptr<WalletDatabase> m_inner;

public:
    EncryptionFaultControl control;
    explicit EncryptionFaultDatabase(std::unique_ptr<WalletDatabase> inner) : m_inner{std::move(inner)} {}
    void Open() override { m_inner->Open(); }
    void Close() override { m_inner->Close(); }
    bool Rewrite() override { return !control.Fail(EncryptionFault::REWRITE) && m_inner->Rewrite(); }
    bool Backup(const std::string& dest) const override { return m_inner->Backup(dest); }
    std::string Filename() override { return m_inner->Filename(); }
    std::vector<fs::path> Files() override { return m_inner->Files(); }
    std::string Format() override { return m_inner->Format(); }
    std::unique_ptr<DatabaseBatch> MakeBatch() override { return std::make_unique<EncryptionFaultBatch>(m_inner->MakeBatch(), control); }
};

std::unique_ptr<WalletDatabase> OpenEncryptionDatabase(const fs::path& path)
{
    DatabaseOptions options;
    DatabaseStatus status;
    bilingual_str error;
    auto database = MakeSQLiteDatabase(path, options, status, error);
    BOOST_REQUIRE_MESSAGE(database, error.original);
    return database;
}

const SecureString OLD_PASSPHRASE{"old-pq-encryption-test-passphrase"};
const SecureString NEW_PASSPHRASE{"new-pq-encryption-test-passphrase"};

} // namespace

BOOST_FIXTURE_TEST_SUITE(encryption_persistence_tests, WalletTestingSetup)

BOOST_AUTO_TEST_CASE(passphrase_master_key_write_failure)
{
    const auto path = m_path_root / "passphrase-write-failure";
    auto database = std::make_unique<EncryptionFaultDatabase>(OpenEncryptionDatabase(path));
    auto& control = database->control;
    auto wallet = std::make_unique<CWallet>(m_node.chain.get(), "fault-wallet", std::move(database));
    {
        LOCK(wallet->cs_wallet);
        wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS | WALLET_FLAG_BLANK_WALLET | WALLET_FLAG_LAST_HARDENED_XPUB_CACHED);
        BOOST_REQUIRE(wallet->InitializeMercaturaPQWallet());
    }
    const auto receive = wallet->GetNewMercaturaPQDestination(false);
    const auto change = wallet->GetNewMercaturaPQDestination(true);
    BOOST_REQUIRE(receive);
    BOOST_REQUIRE(change);
    BOOST_REQUIRE(wallet->EncryptWallet(OLD_PASSPHRASE));

    control.fault = EncryptionFault::WRITE;
    control.record = DBKeys::MASTER_KEY;
    BOOST_CHECK(!wallet->ChangeWalletPassphrase(OLD_PASSPHRASE, NEW_PASSPHRASE));
    BOOST_CHECK_EQUAL(control.hits, 1U);
    BOOST_CHECK(wallet->IsLocked());
    BOOST_CHECK(wallet->Unlock(OLD_PASSPHRASE));
    wallet->Lock();
    BOOST_CHECK(!wallet->Unlock(NEW_PASSPHRASE));
    wallet->Lock();
    wallet.reset();

    // Closing and reopening the actual SQLite file must agree with live state.
    CWallet reloaded{m_node.chain.get(), "reloaded", OpenEncryptionDatabase(path)};
    bilingual_str error;
    std::vector<bilingual_str> warnings;
    BOOST_REQUIRE(reloaded.PopulateWalletFromDB(error, warnings) == DBErrors::LOAD_OK);
    BOOST_CHECK(reloaded.IsLocked());
    BOOST_CHECK(reloaded.Unlock(OLD_PASSPHRASE));
    reloaded.Lock();
    BOOST_CHECK(!reloaded.Unlock(NEW_PASSPHRASE));
    {
        LOCK(reloaded.cs_wallet);
        BOOST_CHECK_EQUAL(reloaded.GetMercaturaPQKeyLocatorCount(), 2U);
        BOOST_CHECK(reloaded.IsMine(*receive));
        BOOST_CHECK(reloaded.IsMine(*change));
    }
}

BOOST_AUTO_TEST_CASE(passphrase_transaction_failures)
{
    for (const auto fault : {EncryptionFault::BEGIN, EncryptionFault::COMMIT}) {
        for (const bool locked : {false, true}) {
            const auto path = m_path_root / (fault == EncryptionFault::BEGIN
                ? (locked ? "begin-locked" : "begin-unlocked")
                : (locked ? "commit-locked" : "commit-unlocked"));
            auto database = std::make_unique<EncryptionFaultDatabase>(OpenEncryptionDatabase(path));
            auto& control = database->control;
            CWallet wallet{m_node.chain.get(), "transaction-failure", std::move(database)};
            {
                LOCK(wallet.cs_wallet);
                wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS | WALLET_FLAG_BLANK_WALLET | WALLET_FLAG_LAST_HARDENED_XPUB_CACHED);
                BOOST_REQUIRE(wallet.InitializeMercaturaPQWallet());
            }
            BOOST_REQUIRE(wallet.EncryptWallet(OLD_PASSPHRASE));
            if (!locked) BOOST_REQUIRE(wallet.Unlock(OLD_PASSPHRASE));
            control.fault = fault;
            bilingual_str error;
            BOOST_CHECK(!wallet.ChangeWalletPassphrase(OLD_PASSPHRASE, NEW_PASSPHRASE, &error));
            BOOST_CHECK(!error.empty());
            BOOST_CHECK_EQUAL(control.hits, 1U);
            BOOST_CHECK_EQUAL(wallet.IsLocked(), locked);
            wallet.Lock();
            BOOST_CHECK(wallet.Unlock(OLD_PASSPHRASE));
            wallet.Lock();
            BOOST_CHECK(!wallet.Unlock(NEW_PASSPHRASE));
            wallet.Lock();
            CMasterKey stored;
            BOOST_REQUIRE(wallet.GetDatabase().MakeBatch()->Read(std::make_pair(DBKeys::MASTER_KEY, wallet.nMasterKeyMaxID), stored));
            LOCK(wallet.cs_wallet);
            BOOST_CHECK(stored.vchCryptedKey == wallet.mapMasterKeys.at(wallet.nMasterKeyMaxID).vchCryptedKey);
        }
    }
}

BOOST_AUTO_TEST_CASE(passphrase_rpc_failure_and_success)
{
    const auto path = m_path_root / "passphrase-rpc";
    auto database = std::make_unique<EncryptionFaultDatabase>(OpenEncryptionDatabase(path));
    auto& control = database->control;
    auto wallet = std::make_shared<CWallet>(m_node.chain.get(), "rpc-fault-wallet", std::move(database));
    {
        LOCK(wallet->cs_wallet);
        wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS | WALLET_FLAG_BLANK_WALLET | WALLET_FLAG_LAST_HARDENED_XPUB_CACHED);
        BOOST_REQUIRE(wallet->InitializeMercaturaPQWallet());
    }
    BOOST_REQUIRE(wallet->EncryptWallet(OLD_PASSPHRASE));
    {
        WalletContext context;
        context.chain = m_node.chain.get();
        context.args = m_node.args;
        BOOST_REQUIRE(AddWallet(context, wallet));
        JSONRPCRequest request;
        request.context = &context;
        request.strMethod = "walletpassphrasechange";
        request.params = UniValue{UniValue::VARR};
        request.params.push_back(std::string{OLD_PASSPHRASE.begin(), OLD_PASSPHRASE.end()});
        request.params.push_back(std::string{NEW_PASSPHRASE.begin(), NEW_PASSPHRASE.end()});
        if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
        CRPCTable rpc;
        for (const auto& command : GetWalletRPCCommands()) rpc.appendCommand(command.name, &command);
        control.fault = EncryptionFault::WRITE;
        control.record = DBKeys::MASTER_KEY;
        BOOST_CHECK_EXCEPTION(rpc.execute(request), UniValue, [](const UniValue& response) {
            const UniValue& code = response.find_value("code");
            return code.isNum() && code.getInt<int>() == RPC_DATABASE_ERROR;
        });
        BOOST_CHECK(wallet->Unlock(OLD_PASSPHRASE));
        wallet->Lock();
        BOOST_CHECK(rpc.execute(request).isNull());
        BOOST_CHECK(wallet->IsLocked());
        BOOST_CHECK(!wallet->Unlock(OLD_PASSPHRASE));
        BOOST_CHECK(wallet->Unlock(NEW_PASSPHRASE));
        wallet->Lock();
    }
    wallet.reset();
    CWallet reloaded{m_node.chain.get(), "reloaded", OpenEncryptionDatabase(path)};
    bilingual_str error;
    std::vector<bilingual_str> warnings;
    BOOST_REQUIRE(reloaded.PopulateWalletFromDB(error, warnings) == DBErrors::LOAD_OK);
    BOOST_CHECK(!reloaded.Unlock(OLD_PASSPHRASE));
    BOOST_CHECK(reloaded.Unlock(NEW_PASSPHRASE));
    reloaded.Lock();
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
