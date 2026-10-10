// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_QT_POOLCLIENT_H
#define BITCOIN_QT_POOLCLIENT_H

#include <consensus/params.h>
#include <interfaces/handler.h>
#include <interfaces/mining.h>
#include <mining/cpu_miner.h>
#include <script/script.h>
#include <support/allocators/secure.h>
#include <univalue.h>

#include <chrono>
#include <mutex>

class QSslSocket;

namespace mining {
struct PoolEndpoint {
    std::string host;
    uint16_t port{3333};
    std::string certificate_file;
    bool operator==(const PoolEndpoint&) const = default;
};
struct PoolStats {
    uint64_t accepted{0}, rejected{0}, stale{0}, duplicates{0}, reconnects{0};
    int64_t last_accepted{0};
    bool connected{false};
    bool locally_verified{false};
    std::string status{"Pool mining stopped"};
    std::string commitment;
    std::shared_ptr<const std::string> details;
    std::shared_ptr<const std::string> manifest;
    std::string reported;
};
struct PoolJob {
    MiningJob work;
    std::string id, snapshot;
    std::shared_ptr<const std::string> manifest;
    int64_t expires{0};
    uint64_t serial{0};
    std::shared_ptr<const CBlock> candidate;
};
// Public snapshots deliberately omit bearer credentials. Credentials remain
// memory-only and are bound to an endpoint, network and owned payout script.
class PoolState
{
public:
    PoolStats Snapshot() const;
    void ForgetSession(); // Owner calls only while stopped.
private:
    friend class PoolWorkProvider;
    mutable std::mutex mutex;
    PoolStats stats;
    SecureString token;
    std::string session, last_job;
    uint64_t last_serial{0};
    std::string binding;
    std::optional<std::pair<PoolJob, CBlockHeader>> pending;
};
bool ValidPoolEndpoint(const PoolEndpoint& endpoint);
enum class LocalPoolValidation { UNAVAILABLE,
                                 VALID,
                                 STALE,
                                 INVALID };
LocalPoolValidation VerifyLocalPoolCandidate(interfaces::Mining* local, const CBlock& block);
UniValue ParsePoolFrame(const std::string& frame);
PoolJob ValidatePoolJob(const UniValue& job, const UniValue& manifest,
                        const std::string& network, const std::string& genesis,
                        const std::string& session, const CScript& payout,
                        const Consensus::Params& consensus, int64_t server_time);

// Owned and used entirely by M2's coordinator. Interrupt only sets atomics;
// Qt sockets are never accessed or destroyed by the GUI thread.
class PoolWorkProvider final : public WorkProvider
{
public:
    PoolWorkProvider(PoolEndpoint endpoint, std::string network, std::string genesis,
                     std::string destination, CScript payout, Consensus::Params consensus,
                     std::shared_ptr<PoolState> state,
                     std::shared_ptr<std::atomic<bool>> cancelled = {},
                     std::unique_ptr<interfaces::Handler> unload = {},
                     std::unique_ptr<interfaces::Mining> local = {});
    ~PoolWorkProvider() override;
    std::optional<MiningJob> GetJob() override;
    bool IsCurrent(const MiningJob& job) override;
    SubmissionResult Submit(const MiningJob& job, const CBlockHeader& solution) override;
    void Interrupt() override { m_stopped = true; }
    bool IsCancelled() const override;
    std::string Destination() const override { return m_destination; }
    std::string WaitingStatus() const override { return m_state->Snapshot().status; }

private:
    using Clock = std::chrono::steady_clock;
    struct Offline {
        std::string reason;
    };
    void Connect();
    void Disconnect();
    void Status(const std::string& status);
    void ReadAvailable();
    std::optional<UniValue> NextFrame();
    void Notification(const UniValue& frame);
    UniValue Request(const std::string& method, UniValue params = UniValue{UniValue::VOBJ});
    std::optional<PoolJob> FetchJob();
    bool Receipt(const UniValue& receipt, const PoolJob& job, const CBlockHeader& solution);
    int64_t ServerTime() const;
    const PoolEndpoint m_endpoint;
    const std::string m_network, m_genesis, m_destination;
    const CScript m_payout;
    const Consensus::Params m_consensus;
    const std::shared_ptr<PoolState> m_state;
    const std::shared_ptr<std::atomic<bool>> m_cancelled;
    const std::unique_ptr<interfaces::Handler> m_unload;
    const std::unique_ptr<interfaces::Mining> m_local;
    std::atomic<bool> m_stopped{false};
    std::unique_ptr<QSslSocket> m_socket;
    std::string m_input, m_session, m_last_given, m_next_message, m_notified_parent;
    std::optional<PoolJob> m_job, m_next;
    std::optional<std::pair<PoolJob, CBlockHeader>> m_replay;
    int32_t m_id{0};
    int m_heartbeat{30}, m_backoff{1};
    bool m_clean{false};
    int64_t m_server_time{0};
    Clock::time_point m_time_base{}, m_poll{}, m_ping{}, m_retry{}, m_rate_start{}, m_status_time{};
    unsigned int m_frames{0};
};
} // namespace mining
#endif // BITCOIN_QT_POOLCLIENT_H
