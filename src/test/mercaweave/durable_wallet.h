// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_DURABLE_WALLET_H
#define BITCOIN_TEST_MERCAWEAVE_DURABLE_WALLET_H
#ifndef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
#error "Durable research wallet is forbidden in normal builds"
#endif
#include <util/fs.h>
#include <test/mercaweave/reward_planner.h>
#include <wallet/sqlite.h>
namespace mercaweave::durable_research {
using namespace wallet_research;
enum class Status : uint8_t { Reserved, SimulatedAccepted, Cancelled, Conflicted };
struct Entry {
    uint64_t id{}; B32 body{}; Status status{Status::Reserved}; std::set<COutPoint> inputs;
    bool operator==(const Entry&) const = default;
};
// Genuine SQLiteDatabase, isolated research record namespace. This is not a
// production CWallet migration. One authenticated atomic record contains the
// native encrypted seed envelope, issuance metadata and durable reservations.
// No signatures, validation proofs, timer, unlocking or broadcaster are saved.
class Store final {
    mutable std::mutex m_mutex;
    const B32 m_genesis;
    std::unique_ptr<wallet::SQLiteDatabase> m_database;
    std::unique_ptr<Wallet> m_wallet;
    Bytes m_sealed;
    std::vector<Entry> m_entries;
    uint64_t m_next{1};
    bool m_authenticated{false};
    explicit Store(std::string_view, B32);
    Bytes Encode(const Wallet&, const std::vector<Entry>&, uint64_t) const;
    void Commit(const Bytes&);
public:
    static constexpr size_t MAX_ENTRIES{256};
    static fs::path Root();
    static std::unique_ptr<Store> Create(std::string_view name, std::unique_ptr<Wallet>);
    static std::unique_ptr<Store> Load(std::string_view name, B32);
    bool Unlock(const SecureString&); // explicit caller action; never automatic
    Wallet& Owner() { return *m_wallet; }
    void Checkpoint(); // must hold an unlocked authoritative wallet
    Entry Reserve(const reward_research::Proposal&); // commit before publishing
    void Mark(uint64_t, Status); // simulation/cancellation only, no acceptance proof
    void Reconcile(const integration::ViewPtr&); // spending invalidates reservation
    std::vector<Entry> Entries() const;
    bool Backup(const fs::path&) const;
    // Native backend exposed only in this isolated target for fault injection.
    wallet::SQLiteDatabase& DatabaseForTests() { return *m_database; }
};
}
#endif // BITCOIN_TEST_MERCAWEAVE_DURABLE_WALLET_H
