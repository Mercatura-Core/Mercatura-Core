// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/ledger.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <util/string.h>

#include <limits>
#include <stdexcept>

namespace pool {
namespace {
void CheckSql(sqlite3* db, int result)
{
    if (result != SQLITE_OK) throw std::runtime_error(std::string{"SQLite: "} + sqlite3_errmsg(db));
}
} // namespace
struct Ledger::Lock {
    int fd{-1};
    explicit Lock(const std::string& path)
    {
        fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) throw std::runtime_error("cannot open pool database lock");
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            close(fd);
            fd = -1;
            throw std::runtime_error("pool database already owned or cannot be locked");
        }
    }
    ~Lock()
    {
        if (fd >= 0) close(fd);
    }
};
Statement::Statement(sqlite3* db, const std::string& sql) { CheckSql(db, sqlite3_prepare_v2(db, sql.c_str(), -1, &m_stmt, nullptr)); }
Statement::~Statement() { sqlite3_finalize(m_stmt); }
Statement& Statement::Bind(int i, const std::string& value)
{
    CheckSql(sqlite3_db_handle(m_stmt), sqlite3_bind_text(m_stmt, i, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
    return *this;
}
Statement& Statement::Bind(int i, int64_t value)
{
    CheckSql(sqlite3_db_handle(m_stmt), sqlite3_bind_int64(m_stmt, i, value));
    return *this;
}
bool Statement::Row()
{
    int r = sqlite3_step(m_stmt);
    if (r == SQLITE_ROW) return true;
    if (r == SQLITE_DONE) return false;
    CheckSql(sqlite3_db_handle(m_stmt), r);
    return false;
}
std::string Statement::Text(int c) const
{
    const auto* p = sqlite3_column_text(m_stmt, c);
    return p ? std::string{reinterpret_cast<const char*>(p), static_cast<size_t>(sqlite3_column_bytes(m_stmt, c))} : std::string{};
}
int64_t Statement::Integer(int c) const { return sqlite3_column_int64(m_stmt, c); }
Ledger::Ledger(const std::string& path, const std::string& network, const std::string& genesis, const std::string& policy) : m_lock{std::make_unique<Lock>(path)}
{
    int r = sqlite3_open_v2(path.c_str(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (r != SQLITE_OK) {
        if (m_db) sqlite3_close(m_db);
        m_db = nullptr;
        throw std::runtime_error("cannot open pool database");
    }
    try {
        sqlite3_busy_timeout(m_db, 5000);
        {
            auto mode = Query("PRAGMA journal_mode=WAL");
            if (!mode.Row() || mode.Text(0) != "wal") throw std::runtime_error("WAL storage is required");
        }
        Exec("PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;");
        {
            auto version = Query("PRAGMA user_version");
            if (!version.Row() || version.Integer(0) < 0 || version.Integer(0) > 2) throw std::runtime_error("unsupported accounting database version");
        }
        Transaction tx{*this};
        Exec("CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
             "CREATE TABLE IF NOT EXISTS identities(script TEXT PRIMARY KEY);"
             "CREATE TABLE IF NOT EXISTS registrations(script TEXT PRIMARY KEY REFERENCES identities(script),ip TEXT NOT NULL,created INTEGER NOT NULL);"
             "CREATE INDEX IF NOT EXISTS registrations_time ON registrations(created);"
             "CREATE TABLE IF NOT EXISTS sessions(id INTEGER PRIMARY KEY AUTOINCREMENT,script TEXT NOT NULL REFERENCES identities(script),target TEXT NOT NULL,token_hash TEXT UNIQUE NOT NULL,created INTEGER NOT NULL,connected INTEGER NOT NULL DEFAULT 1);"
             "CREATE TABLE IF NOT EXISTS session_registrations(session INTEGER PRIMARY KEY REFERENCES sessions(id),ip TEXT NOT NULL,created INTEGER NOT NULL);"
             "CREATE INDEX IF NOT EXISTS session_registrations_time ON session_registrations(created);"
             "CREATE TABLE IF NOT EXISTS snapshots(id TEXT PRIMARY KEY,manifest TEXT NOT NULL);"
             "CREATE TABLE IF NOT EXISTS jobs(id TEXT PRIMARY KEY,serial INTEGER UNIQUE NOT NULL,session INTEGER NOT NULL REFERENCES sessions(id),snapshot TEXT NOT NULL REFERENCES snapshots(id),parent TEXT NOT NULL,target TEXT NOT NULL,block TEXT NOT NULL,issued INTEGER NOT NULL,expires INTEGER NOT NULL,manifest TEXT NOT NULL);"
             "CREATE TABLE IF NOT EXISTS submissions(work TEXT PRIMARY KEY,job TEXT NOT NULL REFERENCES jobs(id),session INTEGER NOT NULL REFERENCES sessions(id),nonce INTEGER NOT NULL,status TEXT NOT NULL,received INTEGER NOT NULL);"
             "CREATE TABLE IF NOT EXISTS shares(sequence INTEGER PRIMARY KEY AUTOINCREMENT,work TEXT UNIQUE NOT NULL REFERENCES submissions(work),job TEXT NOT NULL REFERENCES jobs(id),session INTEGER NOT NULL REFERENCES sessions(id),script TEXT NOT NULL REFERENCES identities(script),score TEXT NOT NULL,accepted INTEGER NOT NULL);"
             "CREATE INDEX IF NOT EXISTS shares_identity ON shares(script,sequence);"
             "CREATE TABLE IF NOT EXISTS candidates(hash TEXT PRIMARY KEY,job TEXT NOT NULL REFERENCES jobs(id),snapshot TEXT NOT NULL REFERENCES snapshots(id),block TEXT NOT NULL,state TEXT NOT NULL,created INTEGER NOT NULL,rpc_result TEXT NOT NULL DEFAULT '',confirmations INTEGER NOT NULL DEFAULT 0,reconciled INTEGER NOT NULL DEFAULT 0,ever_active INTEGER NOT NULL DEFAULT 0);"
             "CREATE TABLE IF NOT EXISTS events(sequence INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT NOT NULL,identity TEXT NOT NULL,detail TEXT NOT NULL,time INTEGER NOT NULL);"
             "PRAGMA user_version=2;");
        for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{{"network", network}, {"genesis", genesis}, {"accounting", "1"}, {"policy", policy}}) {
            auto q = Query("SELECT value FROM metadata WHERE key=?");
            q.Bind(1, key);
            if (q.Row()) {
                if (q.Text(0) != value) throw std::runtime_error("database identity/version/policy mismatch: " + key);
            } else {
                auto insert = Query("INSERT INTO metadata VALUES(?,?)");
                insert.Bind(1, key).Bind(2, value).Row();
            }
        }
        Exec("UPDATE sessions SET connected=0; UPDATE submissions SET status='interrupted' WHERE status='queued';");
        tx.Commit();
    } catch (...) {
        sqlite3_close(m_db);
        m_db = nullptr;
        throw;
    }
}
Ledger::~Ledger()
{
    if (m_db) sqlite3_close(m_db);
}
void Ledger::Exec(const std::string& sql)
{
    char* error = nullptr;
    int r = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &error);
    std::string detail = error ? error : "";
    sqlite3_free(error);
    if (r != SQLITE_OK) throw std::runtime_error("SQLite: " + detail);
}
void Ledger::Rollback() noexcept { sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr); }
int64_t Ledger::Counter(const std::string& name)
{
    Transaction tx{*this};
    auto q = Query("SELECT value FROM metadata WHERE key=?");
    q.Bind(1, name);
    int64_t n = 0;
    if (q.Row()) {
        const auto value = Number::Parse(q.Text(0)).Uint64();
        if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) throw std::overflow_error("namespace exhausted");
        n = static_cast<int64_t>(value);
    }
    if (n < 0 || n == std::numeric_limits<int64_t>::max()) throw std::overflow_error("namespace exhausted");
    auto set = Query("INSERT INTO metadata VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    set.Bind(1, name).Bind(2, util::ToString(++n)).Row();
    tx.Commit();
    return n;
}
int64_t Ledger::Cutoff()
{
    auto q = Query("SELECT COALESCE(MAX(sequence),0) FROM shares");
    q.Row();
    return q.Integer(0);
}
Window Ledger::Pplns(int64_t cutoff, const Number& limit)
{
    if (limit.Zero()) throw std::invalid_argument("zero work window");
    // Stream from disk, bounded by the work window. No history-sized allocation.
    Window w;
    auto q = Query("SELECT sequence,script,score FROM shares WHERE sequence<=? ORDER BY sequence DESC");
    q.Bind(1, cutoff);
    while (q.Row() && w.total < limit) {
        const auto score = Number::Parse(q.Text(2));
        if (score.Zero()) throw std::runtime_error("corrupt share score");
        const auto used = std::min(score, limit - w.total);
        w.total += used;
        w.weights[q.Text(1)] += used;
        w.oldest = q.Integer(0);
        w.oldest_used = used;
    }
    return w;
}
size_t Ledger::IdentityCount()
{
    auto q = Query("SELECT COUNT(*) FROM identities");
    q.Row();
    return static_cast<size_t>(q.Integer(0));
}
void Ledger::Admit(const std::string& script, size_t cap)
{
    auto exists = Query("SELECT script FROM identities WHERE script=?");
    exists.Bind(1, script);
    if (exists.Row()) return;
    if (IdentityCount() >= cap) throw std::runtime_error("payout identity capacity reached");
    auto q = Query("INSERT INTO identities VALUES(?)");
    q.Bind(1, script).Row();
}
bool Ledger::Register(const std::string& script, const std::string& ip, int64_t now, int64_t window, size_t per_ip, size_t global, size_t cap)
{
    auto known = Query("SELECT script FROM identities WHERE script=?");
    known.Bind(1, script);
    if (!known.Row()) {
        // Future-dated rows also count after a clock rollback. Reconnects and
        // previously admitted scripts do not spend new-identity capacity.
        auto recent = Query("SELECT COUNT(*),COALESCE(SUM(ip=?),0) FROM registrations WHERE created>?");
        recent.Bind(1, ip).Bind(2, now - window);
        recent.Row();
        if (static_cast<uint64_t>(recent.Integer(0)) >= global || static_cast<uint64_t>(recent.Integer(1)) >= per_ip) return false;
        Admit(script, cap);
        auto record = Query("INSERT INTO registrations VALUES(?,?,?)");
        record.Bind(1, script).Bind(2, ip).Bind(3, now).Row();
    }
    return true;
}
bool Ledger::SessionAllowed(const std::string& ip, int64_t now, int64_t window, size_t per_ip, size_t global)
{
    auto recent = Query("SELECT COUNT(*),COALESCE(SUM(ip=?),0) FROM session_registrations WHERE created>?");
    recent.Bind(1, ip).Bind(2, now - window);
    recent.Row();
    return static_cast<uint64_t>(recent.Integer(0)) < global && static_cast<uint64_t>(recent.Integer(1)) < per_ip;
}
void Ledger::RecordSession(int64_t session, const std::string& ip, int64_t now)
{
    auto record = Query("INSERT INTO session_registrations VALUES(?,?,?)");
    record.Bind(1, session).Bind(2, ip).Bind(3, now).Row();
}
int64_t Ledger::Accept(const std::string& work, const std::string& job, int64_t session, const std::string& script, const Number& score, int64_t now)
{
    Transaction tx{*this};
    auto previous = Query("SELECT sequence FROM shares WHERE work=?");
    previous.Bind(1, work);
    if (previous.Row()) {
        auto n = previous.Integer(0);
        tx.Commit();
        return n;
    }
    auto q = Query("INSERT INTO shares(work,job,session,script,score,accepted) VALUES(?,?,?,?,?,?)");
    q.Bind(1, work).Bind(2, job).Bind(3, session).Bind(4, script).Bind(5, score.Decimal()).Bind(6, now).Row();
    auto seq = LastInsert();
    auto update = Query("UPDATE submissions SET status='accepted' WHERE work=?");
    update.Bind(1, work).Row();
    tx.Commit();
    return seq;
}
} // namespace pool
