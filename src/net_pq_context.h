// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_NET_PQ_CONTEXT_H
#define BITCOIN_NET_PQ_CONTEXT_H

#include <net_pq_protocol.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#ifndef WIN32
#include <pthread.h>
#endif

class CNetAddr;
class CService;
class Transport;

namespace pq {
namespace test { struct Access; }
using Clock = std::chrono::steady_clock;
struct Work {
    const uint64_t generation;
    const Clock::time_point queued{Clock::now()};
    std::atomic<bool> cancelled{false}, started{false}, done{false};
    std::atomic<int64_t> started_us{0};
    bool ok{false};
    bool pin_mismatch{false};
    bool initiator{false};
    std::optional<Digest> pin;
    std::unique_ptr<Handshake> handshake;
    Bytes record, response;
    std::shared_ptr<void> admission;
    Work(uint64_t gen) : generation(gen) {}
    // Only the exclusive worker, or Stop after removing a queued job, may call.
    void Discard();
};

/** Shared by a single CConnman; absent entirely with default options. */
class Context {
    friend struct test::Access;
public:
    struct ResourceStats {
        unsigned pending{0}, queued{0}, active{0}, peak_pending{0}, peak_queued{0};
        uint64_t completed_jobs{0}, total_job_us{0}, max_job_us{0};
        std::array<size_t, 2> stack_reservations{};
    };
private:
    struct Source { unsigned pending{0}; double tokens{4}; Clock::time_point last{Clock::now()}; };
    struct Quota {
        std::mutex mutex;
        std::map<std::string, Source> sources;
        unsigned pending{0}, anonymous{0}, peak_pending{0};
        double tokens{32}; Clock::time_point last{Clock::now()};
        bool stopped{false};
    };
    struct Admission {
        const std::shared_ptr<Quota> quota;
        const std::string source;
        bool charged{false};
        Admission(const std::shared_ptr<Quota>& q,const std::string& s) : quota(q),source(s) {}
        ~Admission() { if (charged) Release(quota,source); }
    };
    // Leases keep only accounting alive. A worker releasing the last lease
    // must never become the owner/destructor of its own thread pool.
    const std::shared_ptr<Quota> m_quota{std::make_shared<Quota>()};
    // Stop may be requested by more than one owner. Serialize thread joins.
    std::mutex m_stop_mutex;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<Work>> m_queue;
#ifdef WIN32
    std::array<std::thread, 2> m_workers;
#else
    struct Worker { pthread_t thread{}; bool joinable{false}; };
    struct WorkerArgument { Context* context; size_t index; };
    std::array<Worker, 2> m_workers;
    std::array<WorkerArgument, 2> m_worker_arguments{};
#endif
    std::array<std::weak_ptr<Work>, 2> m_running;
    double m_log_tokens{20}; Clock::time_point m_log_last{Clock::now()};
    bool m_stop{false};
    ResourceStats m_stats;
    void Run(size_t index);
    static void Release(const std::shared_ptr<Quota>& quota, const std::string& source);
public:
    const Network network;
    const std::shared_ptr<const Identity> identity;
    const std::map<std::string, Digest> pins;
    Context(Network net, std::shared_ptr<const Identity> id = {}, std::map<std::string, Digest> expected = {});
    ~Context();
    void Stop();
    bool Stopped() const;
    std::shared_ptr<void> Admit(const std::string& source);
    bool Submit(const std::shared_ptr<Work>& job);
    unsigned Pending();
    void ReportFailure(std::string_view reason);
    ResourceStats Resources();
};

Network RegtestNetwork();
std::shared_ptr<const Identity> LoadIdentity(const std::string& path, bool create, const Network& network);
std::string SourceGroup(const CNetAddr& address, bool anonymous);

/** Construct a PQ adapter without exposing its net.h dependency to net.cpp. */
std::unique_ptr<Transport> MakePQTransport(const std::shared_ptr<Context>& context, bool initiator,
                                           const CService& address, const std::string& name, bool anonymous);
} // namespace pq
#endif // BITCOIN_NET_PQ_CONTEXT_H
