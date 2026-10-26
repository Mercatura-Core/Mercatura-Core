// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_NET_PQ_H
#define BITCOIN_NET_PQ_H

#include <net.h>
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

class PQTransport final : public Transport {
    friend struct test::Access;
    std::shared_ptr<Context> m_context;
    const bool m_initiator;
    const std::optional<Digest> m_pin;
    const std::string m_source;
    mutable std::mutex m_mutex;
    const Clock::time_point m_created{Clock::now()};
    Clock::time_point m_wait{m_created};
    uint64_t m_generation{1};
    std::shared_ptr<void> m_admission;
    std::unique_ptr<Handshake> m_handshake;
    std::shared_ptr<Work> m_work;
    std::unique_ptr<PacketCipher> m_send_cipher, m_recv_cipher;
    Bytes m_send, m_recv, m_decoded;
    size_t m_send_pos{0}, m_preface_pos{0};
    uint8_t m_expected;
    bool m_preface{true}, m_failed{false}, m_established{false}, m_final_send{false}, m_message{false};
    std::optional<uint32_t> m_packet_length;
    std::string m_type, m_peer_fp;
    std::array<uint8_t, 32> m_session{};
    bool Fail(bool report = true);
    bool Queue(Bytes record, bool initial = false);
    void InstallKeys();
public:
    PQTransport(std::shared_ptr<Context> context, bool initiator, std::optional<Digest> pin, std::string source);
    ~PQTransport() override;
    bool Poll() override;
    void Disconnect() override;
    Info GetInfo() const noexcept override;
    bool ReceivedMessageComplete() const override;
    bool ReceivedBytes(std::span<const uint8_t>& bytes) override;
    CNetMessage GetReceivedMessage(std::chrono::microseconds time, bool& reject) override;
    bool SetMessageToSend(CSerializedNetMsg& msg) noexcept override;
    BytesToSend GetBytesToSend(bool have_next) const noexcept override;
    void MarkBytesSent(size_t count) noexcept override;
    size_t GetSendMemoryUsage() const noexcept override;
    bool ShouldReconnectV1() const noexcept override { return false; }
};
} // namespace pq
#endif // BITCOIN_NET_PQ_H
