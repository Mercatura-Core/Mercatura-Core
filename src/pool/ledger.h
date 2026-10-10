// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_LEDGER_H
#define BITCOIN_POOL_LEDGER_H

#include <pool/accounting.h>
#include <sqlite3.h>

#include <memory>
#include <string>
#include <vector>

namespace pool {
class Statement
{
    sqlite3_stmt* m_stmt{nullptr};

public:
    Statement(sqlite3* db, const std::string& sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement& Bind(int index, const std::string& value);
    Statement& Bind(int index, int64_t value);
    bool Row();
    std::string Text(int column) const;
    int64_t Integer(int column) const;
};
// Calls are serialized by the owning reactor. WAL + FULL, including before ACK.
class Ledger
{
    struct Lock;
    std::unique_ptr<Lock> m_lock;
    sqlite3* m_db{nullptr};

public:
    Ledger(const std::string& path, const std::string& network, const std::string& genesis, const std::string& policy);
    ~Ledger();
    Ledger(const Ledger&) = delete;
    Ledger& operator=(const Ledger&) = delete;
    void Exec(const std::string& sql);
    Statement Query(const std::string& sql) { return Statement{m_db, sql}; }
    void Begin() { Exec("BEGIN IMMEDIATE"); }
    void Commit() { Exec("COMMIT"); }
    void Rollback() noexcept;
    int64_t Counter(const std::string& name);
    int64_t Cutoff();
    Window Pplns(int64_t cutoff, const Number& limit);
    size_t IdentityCount();
    void Admit(const std::string& script, size_t cap);
    // Caller encloses registration and session insertion in one transaction.
    bool Register(const std::string& script, const std::string& ip, int64_t now, int64_t window, size_t per_ip, size_t global, size_t cap);
    bool SessionAllowed(const std::string& ip, int64_t now, int64_t window, size_t per_ip, size_t global);
    void RecordSession(int64_t session, const std::string& ip, int64_t now);
    // Returns the durable receipt sequence; duplicate work never inserts again.
    int64_t Accept(const std::string& work, const std::string& job, int64_t session, const std::string& script, const Number& score, int64_t now);
    int64_t LastInsert() const { return sqlite3_last_insert_rowid(m_db); }
};
class Transaction
{
    Ledger& m_db;
    bool m_committed{false};

public:
    explicit Transaction(Ledger& db) : m_db{db} { m_db.Begin(); }
    ~Transaction()
    {
        if (!m_committed) m_db.Rollback();
    }
    void Commit()
    {
        m_db.Commit();
        m_committed = true;
    }
};
} // namespace pool
#endif // BITCOIN_POOL_LEDGER_H
