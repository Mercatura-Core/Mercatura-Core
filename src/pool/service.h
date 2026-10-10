// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_SERVICE_H
#define BITCOIN_POOL_SERVICE_H

#include <pool/config.h>
#include <pool/job.h>
#include <pool/ledger.h>
#include <pool/protocol.h>
#include <pool/rpc.h>
#include <pool/verifier.h>

#include <map>
#include <optional>

namespace pool {
struct Response {
    int64_t session;
    UniValue message;
};
class Service
{
    struct Session {
        std::string script;
        std::string ip;
        std::string target;
        std::string latest;
        size_t pending{0};
        uint64_t accepted{0};
        Number work;
        int64_t started{0};
        uint64_t connection{0};
    };
    struct Pending {
        int64_t request;
        Job job;
        std::string work;
        std::future<Verification> verification;
        int64_t received;
        uint64_t connection;
    };
    Config m_cfg;
    Rpc m_rpc;
    Ledger m_db;
    Verifier m_verifier;
    UniValue m_template;
    std::string m_parent;
    int64_t m_started{Now()};
    int64_t m_template_time{0};
    int64_t m_poll_time{0};
    int64_t m_reconcile_time{0};
    bool m_healthy{false};
    bool m_stopping{false};
    std::string m_health;
    std::map<int64_t, Session> m_sessions;
    std::map<std::string, Pending> m_pending;
    std::map<std::string, Job> m_jobs;
    uint64_t m_rejected{0};
    uint64_t m_request_errors{0};
    uint64_t m_stale{0};
    uint64_t m_jobs_issued{0};
    uint64_t m_connections{0};
    void Refresh();
    void Reconcile();
    Job& GetJob(int64_t session);
    Job LoadJob(const std::string& id);
    UniValue Receipt(const std::string& work, int64_t sequence, const Job& job, bool candidate);
    UniValue Hello(int64_t& session, const std::string& ip, const UniValue& p);

public:
    explicit Service(const Config& config);
    ~Service();
    // Reactor owns this service. Hash workers never touch RPC, SQL or sessions.
    std::optional<UniValue> Handle(int64_t& session, const std::string& ip, const Request& request);
    std::vector<Response> Tick();
    void Disconnect(int64_t session);
    void Shutdown();
    UniValue Stats();
    const Config& Settings() const { return m_cfg; }
};
} // namespace pool
#endif // BITCOIN_POOL_SERVICE_H
