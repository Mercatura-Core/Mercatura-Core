// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <net_pq.h>
#include <mercatura-pq-config.h>
#include <chainparams.h>
#include <random.h>
#include <logging.h>
#include <util/strencodings.h>
#include <util/chaintype.h>
#include <algorithm>
#include <stdexcept>
#ifdef MERCATURA_PQ_TESTING
#include <test/util/pq_lifecycle.h>
#endif
#ifdef __linux__
#include <pthread.h>
#endif

namespace pq {
// Seven maximum record allocations conservatively cover moved input, existing
// send storage, response construction/preface, and temporary public messages.
// Worker stacks are separately reserved, never charged to session heap storage.
static_assert(sizeof(Handshake) + sizeof(Identity) + sizeof(PQTransport) + sizeof(Work) + 7 * 4409 + 4096 < 65536);
namespace {
Random SecureRandom()
{
    return [](std::span<uint8_t> out) {
        // Mercatura's strong RNG API permits at most 32 output bytes per draw.
        // ML-KEM key generation needs 64 independent bytes, supplied as two
        // secure draws rather than exceeding the underlying API contract.
        while (!out.empty()) {
            const auto count = std::min(out.size(),size_t{32});
            GetStrongRandBytes(out.first(count)); out=out.subspan(count);
        }
        return true;
    };
}
int64_t Micros(Clock::time_point time) { return std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count(); }
}
Context::Context(Network net, std::shared_ptr<const Identity> id, std::map<std::string, Digest> expected)
    : network(net), identity(std::move(id)), pins(std::move(expected))
{
    try {
        for (size_t i = 0; i < m_workers.size(); ++i) {
#ifdef WIN32
            m_workers[i] = std::thread([this, i] { Run(i); });
#else
            pthread_attr_t attr;
            if (pthread_attr_init(&attr)) throw std::runtime_error("Cannot initialize PQ worker attributes");
            if (pthread_attr_setstacksize(&attr, 1024*1024)) { pthread_attr_destroy(&attr); throw std::runtime_error("Cannot enforce PQ worker stack reservation"); }
            m_worker_arguments[i] = {this,i};
            const int error = pthread_create(&m_workers[i].thread, &attr, [](void* argument) -> void* {
                auto* worker = static_cast<WorkerArgument*>(argument); worker->context->Run(worker->index); return nullptr;
            }, &m_worker_arguments[i]);
            pthread_attr_destroy(&attr);
            if (error) throw std::runtime_error("Cannot start bounded PQ worker");
            m_workers[i].joinable = true;
#endif
        }
    } catch (...) { Stop(); throw; }
}
Context::~Context() { Stop(); }
void Work::Discard()
{
    ok = false;
    if (handshake) pin_mismatch = handshake->PinMismatch();
    handshake.reset(); record.clear(); response.clear(); admission.reset();
}
void Context::Stop()
{
    std::lock_guard stop_lock(m_stop_mutex);
    { std::lock_guard lock(m_quota->mutex); m_quota->stopped = true; }
    std::deque<std::shared_ptr<Work>> abandoned;
    {
        std::lock_guard lock(m_mutex); m_stop = true;
        for (auto& weak : m_running) if (auto job = weak.lock()) job->cancelled = true;
        for (auto& job : m_queue) job->cancelled = true;
        abandoned.swap(m_queue);
    }
    for (auto& job : abandoned) {
        job->Discard(); job->done.store(true, std::memory_order_release);
    }
    m_cv.notify_all(); abandoned.clear();
    for (auto& worker : m_workers) {
#ifdef WIN32
        if (worker.joinable()) worker.join();
#else
        if (worker.joinable) { pthread_join(worker.thread,nullptr); worker.joinable=false; }
#endif
    }
}
void Context::Run(size_t index)
{
#ifdef __linux__
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        size_t stack_size{0};
        if (pthread_attr_getstacksize(&attr, &stack_size) == 0) { std::lock_guard lock(m_mutex); m_stats.stack_reservations[index] = stack_size; }
        pthread_attr_destroy(&attr);
    }
#endif
    for (;;) {
        std::shared_ptr<Work> job;
        {
            std::unique_lock lock(m_mutex); m_cv.wait(lock, [&] { return m_stop || !m_queue.empty(); });
            if (m_stop) return;
            job = std::move(m_queue.front()); m_queue.pop_front(); m_running[index] = job;
        }
        job->started_us = Micros(Clock::now()); job->started = true;
        if (!job->cancelled && Clock::now() - job->queued <= std::chrono::seconds(2)) {
            try {
#ifdef MERCATURA_PQ_TESTING
                test::LifecyclePoint(test::Event::BEFORE_CALCULATE, job.get());
#endif
                auto rng = SecureRandom();
                if (!job->handshake) {
                    // An invalid public encoding must not cause an avoidable
                    // identity keygen before the handshake's identical check.
                    if (!job->initiator && (job->record.size()!=RECORD_SIZES[1] ||
                        !ValidHeader(std::span{job->record}.first(12),1) ||
                        !Equal(std::span{job->record}.subspan(12,85),network) ||
                        !ValidKEMPublic(std::span{job->record}.subspan(129,1184)))) {
                        throw std::runtime_error("Invalid PQ hello");
                    }
                    std::shared_ptr<const Identity> id = identity;
                    if (!id) {
                        auto fresh = std::make_shared<Identity>();
                        if (!fresh->Generate(rng)) throw std::runtime_error("PQ identity RNG failed");
                        id = std::move(fresh);
                    }
                    job->handshake = std::make_unique<Handshake>(job->initiator, network, job->pin, std::move(id), rng);
                }
                job->ok = job->record.empty() ? job->handshake->Start(job->response) : job->handshake->Advance(job->record, job->response);
            } catch (...) { job->ok = false; }
        }
#ifdef MERCATURA_PQ_TESTING
        test::LifecyclePoint(test::Event::AFTER_CALCULATE, job.get());
#endif
        if (job->cancelled || Micros(Clock::now()) - job->started_us > 5'000'000) job->ok = false;
        if (!job->ok) job->Discard();
        job->record.clear();
        const auto elapsed = uint64_t(std::max(int64_t{0}, Micros(Clock::now())-job->started_us));
        {
            std::lock_guard lock(m_mutex); m_running[index].reset();
            ++m_stats.completed_jobs; m_stats.total_job_us += elapsed; m_stats.max_job_us = std::max(m_stats.max_job_us, elapsed);
        }
#ifdef MERCATURA_PQ_TESTING
        test::LifecyclePoint(test::Event::BEFORE_PUBLISH, job.get());
#endif
        job->done.store(true, std::memory_order_release);
    }
}
std::shared_ptr<void> Context::Admit(const std::string& source)
{
    std::lock_guard lock(m_quota->mutex);
    auto& quota = *m_quota;
    auto now = Clock::now();
    quota.tokens = std::min(32.0, quota.tokens + 8.0 * std::chrono::duration<double>(now - quota.last).count()); quota.last = now;
    if (quota.stopped || quota.pending >= 64 || quota.tokens < 1 || (source.empty() && quota.anonymous >= 16)) return {};
    Source* entry{nullptr};
    if (!source.empty()) {
        for (auto it = quota.sources.begin(); it != quota.sources.end();) {
            if (!it->second.pending && now - it->second.last > std::chrono::seconds(120)) it = quota.sources.erase(it); else ++it;
        }
        auto it = quota.sources.find(source);
        if (it == quota.sources.end()) {
            // Fail closed when the bounded accounting table is full; never admit an untracked source.
            if (quota.sources.size() >= 256) return {};
            it = quota.sources.emplace(source, Source{}).first;
        }
        entry = &it->second;
        entry->tokens = std::min(4.0, entry->tokens + std::chrono::duration<double>(now - entry->last).count()); entry->last = now;
        if (entry->pending >= 4 || entry->tokens < 1) return {};
    }
    // All potentially throwing lease/control-block/string allocations precede
    // charging. The previous custom deleter could reenter Release while this
    // mutex was held if shared_ptr's control-block allocation failed.
    auto admission=std::make_shared<Admission>(m_quota,source);
    if (entry) { --entry->tokens; ++entry->pending; } else { ++quota.anonymous; }
    --quota.tokens; ++quota.pending;
    quota.peak_pending = std::max(quota.peak_pending, quota.pending);
    admission->charged=true;
    return admission;
}
void Context::Release(const std::shared_ptr<Quota>& quota, const std::string& source)
{
    std::lock_guard lock(quota->mutex); assert(quota->pending); --quota->pending;
    if (source.empty()) { assert(quota->anonymous); --quota->anonymous; }
    else { auto it = quota->sources.find(source); assert(it != quota->sources.end() && it->second.pending); --it->second.pending; }
}
unsigned Context::Pending() { std::lock_guard lock(m_quota->mutex); return m_quota->pending; }
bool Context::Stopped() const { std::lock_guard lock(m_quota->mutex); return m_quota->stopped; }
Context::ResourceStats Context::Resources()
{
    std::lock_guard lock(m_mutex); auto stats = m_stats;
    { std::lock_guard quota_lock(m_quota->mutex); stats.pending = m_quota->pending; stats.peak_pending = m_quota->peak_pending; }
    stats.queued = m_queue.size();
    for (auto& weak : m_running) if (!weak.expired()) ++stats.active;
    return stats;
}
void Context::RecordEstablished() { std::lock_guard lock(m_mutex); ++m_stats.established; }
void Context::ReportFailure(std::string_view reason, Failure failure)
{
    {
        std::lock_guard lock(m_mutex); ++m_stats.failures[unsigned(failure)]; auto now = Clock::now();
        m_log_tokens = std::min(20.0, m_log_tokens + 10.0 * std::chrono::duration<double>(now-m_log_last).count()); m_log_last = now;
        if (m_log_tokens < 1) return;
        --m_log_tokens;
    }
    LogWarning("Experimental PQ transport rejected: %s", reason);
}
bool Context::Submit(const std::shared_ptr<Work>& job)
{
    std::lock_guard lock(m_mutex);
    if (m_stop || !job->admission || m_queue.size() >= 32) return false;
    m_queue.push_back(job); m_stats.peak_queued = std::max(m_stats.peak_queued, unsigned(m_queue.size())); m_cv.notify_one(); return true;
}
bool TestnetEnforcedBuild() { return MERCATURA_REQUIRE_PQ_TESTNET; }
bool RequiredMode(ChainType chain, std::optional<std::string_view> option, bool testnet_enforced)
{
    if (option && *option != "off" && *option != "required") throw std::runtime_error("-pqp2p must be off or required.");
    switch (chain) {
    case ChainType::MAIN:
        if (option && *option == "off") throw std::runtime_error("PQ P2P is compulsory on Mercatura mainnet; -pqp2p=off is forbidden.");
        return true;
    case ChainType::TESTNET:
        if (testnet_enforced) {
            if (option && *option == "off") throw std::runtime_error("This qualification build requires public-testnet PQ; -pqp2p=off is forbidden. Public activation remains pending.");
            return true;
        }
        return option && *option == "required";
    case ChainType::REGTEST:
        return option && *option == "required";
    case ChainType::SIGNET:
    case ChainType::TESTNET4:
        if (option && *option == "required") throw std::runtime_error("PQ P2P is not enabled on signet or testnet4.");
        return false;
    }
    throw std::runtime_error("Unknown PQ network policy");
}
Network NetworkForChain(const CChainParams& params)
{
    Network out{};
    switch (params.GetChainType()) {
    case ChainType::MAIN: out[0] = 0; break;
    case ChainType::TESTNET: out[0] = 1; break;
    case ChainType::SIGNET: out[0] = 2; break;
    case ChainType::REGTEST: out[0] = 3; break;
    case ChainType::TESTNET4: out[0] = 4; break;
    default: throw std::runtime_error("Unknown PQ network discriminator");
    }
    std::copy(params.MessageStart().begin(), params.MessageStart().end(), out.begin()+1);
    const auto& genesis = params.GetConsensus().hashGenesisBlock;
    std::copy(genesis.begin(), genesis.end(), out.begin()+5);
    const auto challenge = Hash(params.GetChainType() == ChainType::SIGNET ?
        std::span<const uint8_t>{params.GetConsensus().signet_challenge} : std::span<const uint8_t>{});
    std::copy(challenge.begin(), challenge.end(), out.begin()+37);
    return out;
}
Network SelectedNetwork() { return NetworkForChain(Params()); }
std::string SourceGroup(const CNetAddr& address, bool anonymous)
{
    if (anonymous || (!address.IsIPv4() && !address.IsIPv6())) return {};
    auto bytes = address.GetAddrBytes();
    // Network tag prevents an IPv4 address colliding with an IPv6 /64 prefix.
    return std::string(address.IsIPv4() ? "4:" : "6:") + HexStr(address.IsIPv4() ? std::span{bytes}.last(4) : std::span{bytes}.first(8));
}
std::unique_ptr<Transport> MakePQTransport(const std::shared_ptr<Context>& context, bool initiator,
                                           const CService& address, const std::string& name, bool anonymous)
{
    std::optional<Digest> pin;
    if (initiator) {
        const auto destination = name.empty() ? address.ToStringAddrPort() : name;
        auto it = context->pins.find(destination);
        if (it != context->pins.end()) pin = it->second;
    }
    return std::make_unique<PQTransport>(context, initiator, pin, SourceGroup(address, anonymous));
}
PQTransport::PQTransport(std::shared_ptr<Context> context, bool initiator, std::optional<Digest> pin, std::string source)
    : m_context(std::move(context)), m_initiator(initiator), m_pin(pin), m_source(std::move(source)), m_expected(initiator ? 2 : 1)
{
    // Charge the accepted socket immediately, including silent/partial-preface
    // inbound peers. They must not bypass the global/source/anonymous pending
    // limits while waiting for their first complete header. No crypto is queued
    // for an inbound peer until a complete, structurally valid M1 is received.
    m_admission = m_context->Admit(m_source);
    if (!m_admission || (initiator && !Queue({}))) Fail(true, Failure::RESOURCE);
}
PQTransport::~PQTransport() { Disconnect(); }
bool PQTransport::Fail(bool report, Failure failure)
{
    if (report && !m_failed) m_context->ReportFailure("framing, authentication, deadline or resource policy failure", failure);
    if (!m_failed) m_pin_mismatch = failure == Failure::PIN;
    m_failed = true; m_established = false; ++m_generation;
    if (m_work) { m_work->cancelled = true; m_work.reset(); }
    m_handshake.reset(); m_admission.reset(); m_send_cipher.reset(); m_recv_cipher.reset();
    // Keep the send buffer stable until the caller finishes using its span.
    m_recv.clear(); m_decoded.clear(); m_peer_fp.clear(); m_session.fill(0); m_message = false;
    return false;
}
void PQTransport::Disconnect() { std::lock_guard lock(m_mutex); Fail(false); }
bool PQTransport::Queue(Bytes record, bool initial)
{
    if (m_work) return false;
    auto job = std::make_shared<Work>(m_generation);
    job->initiator = m_initiator; job->pin = m_pin; job->handshake = std::move(m_handshake);
    job->record = std::move(record); job->admission = m_admission;
    if (!m_context->Submit(job)) return false;
    m_work = std::move(job); return true;
}
void PQTransport::InstallKeys()
{
    assert(m_handshake && m_handshake->Ready());
    auto keys = m_handshake->TakeKeys(); m_session = keys.id;
    m_peer_fp = "mcapq1:" + HexStr(Fingerprint(m_context->network, m_handshake->Peer()));
    auto& send_l = m_initiator ? keys.i_length : keys.r_length;
    auto& send_p = m_initiator ? keys.i_payload : keys.r_payload;
    auto& recv_l = m_initiator ? keys.r_length : keys.i_length;
    auto& recv_p = m_initiator ? keys.r_payload : keys.i_payload;
    m_send_cipher = std::make_unique<PacketCipher>(send_l.bytes, send_p.bytes, keys.h5, m_initiator ? 0 : 1);
    m_recv_cipher = std::make_unique<PacketCipher>(recv_l.bytes, recv_p.bytes, keys.h5, m_initiator ? 1 : 0);
    m_handshake.reset(); m_admission.reset();
    m_context->RecordEstablished();
}
bool PQTransport::Poll()
{
    std::lock_guard lock(m_mutex);
    try {
        if (m_context->Stopped()) return Fail(false);
        if (m_failed) return false;
        const auto now = Clock::now();
        if (!m_established && (now - m_created > std::chrono::seconds(120) || now - m_wait > std::chrono::seconds(30) || (m_preface && now - m_created > std::chrono::seconds(5)))) return Fail(true, Failure::DEADLINE);
        if (!m_work) return true;
        if (m_work->cancelled) return Fail(false);
        if (!m_work->done.load(std::memory_order_acquire)) {
            if ((!m_work->started && now - m_work->queued > std::chrono::seconds(2)) || (m_work->started && Micros(now) - m_work->started_us > 5'000'000)) return Fail(true, Failure::DEADLINE);
            return true;
        }
        auto result = std::move(m_work);
        if (result->generation != m_generation || result->cancelled || !result->ok) {
            m_context->ReportFailure(result->pin_mismatch ? "expected identity mismatch (pin is never replaced)" : "authentication or cancelled crypto job", result->pin_mismatch ? Failure::PIN : Failure::AUTHENTICATION_OR_CRYPTO);
            return Fail(false, result->pin_mismatch ? Failure::PIN : Failure::AUTHENTICATION_OR_CRYPTO);
        }
        m_handshake = std::move(result->handshake); m_expected = m_handshake->Next();
        assert(m_send.empty()); m_send = std::move(result->response); m_send_pos = 0; m_wait = now;
        if (m_handshake->Ready()) {
            if (m_initiator) { m_final_send = true; }
            else { InstallKeys(); m_established = true; }
        }
        return true;
    } catch (...) {
        return Fail(false);
    }
}
Transport::Info PQTransport::GetInfo() const noexcept
{
    std::lock_guard lock(m_mutex);
    try {
        Info info{.transport_type = TransportProtocolType::PQ_V1, .session_id = {}};
        info.pq_failed = m_failed || m_context->Stopped(); info.pq_pin_mismatch = m_pin_mismatch;
        if (m_established && !m_context->Stopped()) {
            info.session_id = uint256{m_session}; info.pq_encrypted = true; info.pq_authenticated = true;
            info.pq_identity_verified = m_pin.has_value(); info.pq_identity = m_peer_fp;
        }
        return info;
    } catch (...) {
        return Info{.transport_type = TransportProtocolType::PQ_V1, .session_id = {}};
    }
}
bool PQTransport::ReceivedMessageComplete() const { std::lock_guard lock(m_mutex); return m_established && m_message && !m_failed && !m_context->Stopped(); }
bool PQTransport::ReceivedBytes(std::span<const uint8_t>& bytes)
{
    if (!Poll()) return false;
    std::lock_guard lock(m_mutex);
    try {
        if (m_context->Stopped()) return Fail(false);
        if (m_failed || m_work || (!m_established && !m_send.empty()) || m_message) return Fail();
        while (!bytes.empty()) {
            if (!m_established) {
                if (m_preface) {
                    if (bytes.front() != uint8_t(PREFACE[m_preface_pos++])) return Fail();
                    bytes = bytes.subspan(1);
                    if (m_preface_pos == PREFACE.size()) { m_preface = false; m_wait = Clock::now(); }
                    continue;
                }
                size_t target = m_recv.size() < 12 ? 12 : RECORD_SIZES[m_expected];
                const auto count = std::min(bytes.size(), target - m_recv.size());
                m_recv.insert(m_recv.end(), bytes.begin(), bytes.begin()+count); bytes = bytes.subspan(count);
                if (m_recv.size() == 12) {
                    if (!ValidHeader(m_recv, m_expected)) return Fail();
                    if (!m_admission) return Fail();
                    m_recv.reserve(RECORD_SIZES[m_expected]);
                }
                if ((m_expected == 1 || m_expected == 2) && m_recv.size() > 12) {
                    const auto network_bytes = std::min(size_t{85}, m_recv.size()-12);
                    if (!Equal(std::span{m_recv}.subspan(12, network_bytes), std::span{m_context->network}.first(network_bytes))) return Fail();
                }
                if (m_recv.size() == RECORD_SIZES[m_expected]) {
                    // M5 performs only MAC/HKDF and cipher initialization; no KEM or
                    // signature operation. Verify it inline so a coalesced first
                    // packet can be consumed without an unbounded deferred queue.
                    if (m_expected == 5) {
                        Bytes response;
                        if (!m_handshake || !m_handshake->Advance(m_recv, response) || !m_handshake->Ready() || !response.empty()) return Fail(true, Failure::AUTHENTICATION_OR_CRYPTO);
                        m_recv.clear(); InstallKeys(); m_established = true;
                        continue;
                    }
                    if (!bytes.empty()) return Fail();
                    auto record = std::move(m_recv); m_recv.clear();
                    if (!Queue(std::move(record))) return Fail(true, Failure::RESOURCE);
                    return true;
                }
            } else {
                const size_t target = m_packet_length ? size_t{*m_packet_length}+20 : 3;
                const auto count = std::min(bytes.size(), target - m_recv.size());
                m_recv.insert(m_recv.end(), bytes.begin(), bytes.begin()+count); bytes = bytes.subspan(count);
                if (!m_packet_length && m_recv.size() == 3) {
                    m_packet_length = m_recv_cipher->DecryptLength(m_recv);
                    if (!m_packet_length) return Fail(true, Failure::PACKET);
                    m_recv.reserve(size_t{*m_packet_length}+20);
                }
                if (m_packet_length && m_recv.size() == size_t{*m_packet_length}+20) {
                    bool decoy{false};
                    if (!m_recv_cipher->Decrypt(std::span{m_recv}.subspan(3), m_decoded, decoy)) return Fail(true, Failure::PACKET);
                    Bytes{}.swap(m_recv); m_packet_length.reset();
                    if (!decoy) { m_message = true; return true; }
                }
            }
        }
        return true;
    } catch (...) {
        return Fail(false);
    }
}
CNetMessage PQTransport::GetReceivedMessage(std::chrono::microseconds time, bool& reject)
{
    std::lock_guard lock(m_mutex);
    try {
        if (m_context->Stopped()) { Fail(false); reject = true; return CNetMessage{DataStream{}}; }
        assert(m_established && m_message && !m_failed);
        std::span<const uint8_t> contents{m_decoded}; auto command = DecodeCommand(contents);
        CNetMessage msg{DataStream{}}; msg.m_raw_message_size = m_decoded.size()+20;
        reject = !command.has_value() || contents.size() > MAX_PROTOCOL_MESSAGE_LENGTH;
        if (!reject) {
            msg.m_type = *command; msg.m_time = time; msg.m_message_size = contents.size(); msg.m_recv.resize(contents.size());
            std::copy(contents.begin(), contents.end(), reinterpret_cast<uint8_t*>(msg.m_recv.data()));
        }
        m_message = false; Bytes{}.swap(m_decoded); return msg;
    } catch (...) {
        Fail(false); reject = true;
        return CNetMessage{DataStream{}};
    }
}
bool PQTransport::SetMessageToSend(CSerializedNetMsg& msg) noexcept
{
    std::lock_guard lock(m_mutex);
    try {
        if (m_context->Stopped()) return Fail(false);
        if (!m_established || m_failed || !m_send.empty()) return false;
        if (msg.data.size() > MAX_PROTOCOL_MESSAGE_LENGTH || msg.m_type.empty() || msg.m_type.size() > 12 || !std::ranges::all_of(msg.m_type, [](unsigned char c) { return c >= 32 && c <= 127; })) { Fail(); return false; }
        Bytes contents;
        auto it = std::find(COMMANDS.begin()+1, COMMANDS.end(), msg.m_type);
        if (it != COMMANDS.end()) contents.push_back(it - COMMANDS.begin());
        else { contents.resize(13); std::copy(msg.m_type.begin(), msg.m_type.end(), contents.begin()+1); }
        contents.insert(contents.end(), msg.data.begin(), msg.data.end());
        if (!m_send_cipher->Encrypt(contents, 0, m_send)) { Fail(); return false; }
        m_type = msg.m_type; msg.data.clear(); return true;
    } catch (...) {
        return Fail(false);
    }
}
Transport::BytesToSend PQTransport::GetBytesToSend(bool have_next) const noexcept
{
    std::lock_guard lock(m_mutex);
    if (m_failed || m_context->Stopped()) return {{}, false, m_type};
    return {std::span{m_send}.subspan(m_send_pos), m_established && have_next, m_type};
}
void PQTransport::MarkBytesSent(size_t count) noexcept
{
    std::lock_guard lock(m_mutex);
    try {
        if (m_context->Stopped()) { Fail(false); return; }
        if (m_failed || !count) return;
        assert(count <= m_send.size()-m_send_pos); m_send_pos += count;
        if (m_send_pos == m_send.size()) {
            Bytes{}.swap(m_send); m_send_pos = 0; m_type.clear(); m_wait = Clock::now();
            if (m_final_send) { m_final_send = false; InstallKeys(); m_established = true; }
        }
    } catch (...) {
        Fail(false);
    }
}
size_t PQTransport::GetSendMemoryUsage() const noexcept { std::lock_guard lock(m_mutex); return m_send.capacity(); }
} // namespace pq
