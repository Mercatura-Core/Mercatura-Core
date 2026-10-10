// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/service.h>

#include <consensus/consensus.h>
#include <crypto/hex_base.h>
#include <openssl/rand.h>
#include <util/strencodings.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace pool {
namespace {
UniValue Params(const std::string& value)
{
    UniValue p{UniValue::VARR};
    p.push_back(value);
    return p;
}
std::string RandomToken()
{
    unsigned char bytes[32];
    if (RAND_bytes(bytes, 32) != 1) throw std::runtime_error("session entropy unavailable");
    return HexStr(bytes);
}
} // namespace
Service::Service(const Config& config) : m_cfg{config}, m_rpc{config}, m_db{config.database, config.network, config.genesis, config.Policy()}, m_verifier{config.verifier_workers, config.verifier_queue, config.verifier_memory_mib}
{
    config.Validate();
    m_rpc.CheckIdentity();
    Refresh();
    Reconcile();
}
Service::~Service() { Shutdown(); }
void Service::Refresh()
{
    const auto now = Now();
    // Stop issuing and accepting work when the authoritative node is not
    // reachable/current. Previously durable shares remain in the ledger.
    m_healthy = false;
    m_rpc.CheckIdentity();
    const auto parent = m_rpc.Call("getbestblockhash").get_str();
    if (parent != m_parent || now - m_template_time >= m_cfg.refresh_seconds || m_template.isNull()) {
        auto next = m_rpc.Template();
        if (next["previousblockhash"].get_str() != parent) throw std::runtime_error("Core parent changed during template fetch");
        TemplateTarget(next);
        m_template = std::move(next);
        m_template_time = now;
    }
    m_parent = parent;
    m_healthy = true;
    m_health = "ready";
    m_poll_time = now;
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        if (it->second.expires <= now || it->second.block.hashPrevBlock.GetHex() != m_parent)
            it = m_jobs.erase(it);
        else
            ++it;
    }
}
Job Service::LoadJob(const std::string& id)
{
    auto q = m_db.Query("SELECT serial,session,snapshot,target,block,issued,expires,manifest FROM jobs WHERE id=?");
    q.Bind(1, id);
    if (!q.Row()) throw ProtocolError{STALE, "unknown job"};
    Job j;
    j.id = id;
    j.serial = q.Integer(0);
    j.session = q.Integer(1);
    j.snapshot = q.Text(2);
    j.share_target = ParseTarget(q.Text(3));
    j.block = ParseBlock(q.Text(4));
    j.issued = q.Integer(5);
    j.expires = q.Integer(6);
    if (!j.manifest.read(q.Text(7))) throw std::runtime_error("corrupt job manifest");
    j.network_target = TemplateTarget(j.manifest["template"]);
    auto snapshot = m_db.Query("SELECT manifest FROM snapshots WHERE id=?");
    snapshot.Bind(1, j.snapshot);
    if (!snapshot.Row() || !j.snapshot_manifest.read(snapshot.Text(0)) || Digest("MCA-POOL/1/snapshot", j.snapshot_manifest.write()) != j.snapshot) throw std::runtime_error("corrupt frozen snapshot");
    Window weights;
    for (const auto& recipient : j.snapshot_manifest["allocations"].getValues()) {
        auto work = Number::Parse(recipient["work"].get_str());
        weights.weights[recipient["script"].get_str()] = work;
        weights.total += work;
    }
    weights.oldest = j.snapshot_manifest["oldest_sequence"].getInt<int64_t>();
    weights.oldest_used = Number::Parse(j.snapshot_manifest["oldest_used"].get_str());
    const auto expected = BuildJob(j.snapshot_manifest["template"], weights, j.snapshot_manifest["cutoff"].getInt<int64_t>(), Number::Parse(j.snapshot_manifest["window_limit"].get_str()), m_cfg.network, m_cfg.genesis, j.session, j.serial, j.share_target.GetHex(), j.issued, j.expires - j.issued, j.snapshot_manifest["warmup"].get_bool());
    if (expected.id != j.id || expected.snapshot != j.snapshot || BlockHex(expected.block) != BlockHex(j.block) || expected.manifest.write() != j.manifest.write()) throw std::runtime_error("issued job differs from its frozen snapshot");
    return j;
}
Job& Service::GetJob(int64_t session)
{
    if (!m_healthy) throw ProtocolError{INTERNAL, "Core is unavailable; work paused"};
    auto& s = m_sessions.at(session);
    const auto now = Now();
    auto old = m_jobs.find(s.latest);
    if (old != m_jobs.end() && old->second.block.hashPrevBlock.GetHex() == m_parent && old->second.expires > now && now - old->second.issued < m_cfg.refresh_seconds) return old->second;
    const auto network_target = TemplateTarget(m_template);
    if (ParseTarget(s.target) < network_target) throw ProtocolError{CAPACITY, "assigned share target is harder than current network target"};
    // Reserve room for all admitted identities, even if a tiny allocation is
    // zero now. No accepted identity is evicted to accommodate a later one.
    if (RequiredBlockBytes(m_template, m_db.IdentityCount()) > m_template["sizelimit"].getInt<uint64_t>()) throw ProtocolError{CAPACITY, "template cannot honor admitted payout identities"};
    const auto limit = WorkScore(Number::Parse(network_target.GetHex(), true)) * Number{m_cfg.window_blocks};
    const auto cutoff = m_db.Cutoff();
    auto window = m_db.Pplns(cutoff, limit);
    bool warmup = false;
    if (window.total.Zero()) {
        if (!m_cfg.warmup) throw ProtocolError{EMPTY, "empty PPLNS window; explicit warm-up required"};
        warmup = true;
        window.total = Number{1};
        window.weights[s.script] = Number{1};
    }
    const auto serial = m_db.Counter("job_namespace");
    Job job = BuildJob(m_template, window, cutoff, limit, m_cfg.network, m_cfg.genesis, session, serial, s.target, now, m_cfg.lifetime_seconds, warmup);
    // Ask normal Core proposal validation before publishing a finalized job.
    // Proposal mode deliberately skips PoW; actual solutions use submitblock.
    UniValue proposal{UniValue::VOBJ}, args{UniValue::VARR};
    proposal.pushKV("mode", "proposal");
    proposal.pushKV("data", BlockHex(job.block));
    args.push_back(proposal);
    if (!m_rpc.Call("getblocktemplate", args).isNull()) throw ProtocolError{CAPACITY, "Core rejected the finalized block proposal"};
    Transaction tx{m_db};
    auto snapshot = m_db.Query("INSERT OR IGNORE INTO snapshots VALUES(?,?)");
    snapshot.Bind(1, job.snapshot).Bind(2, job.snapshot_manifest.write()).Row();
    auto insert = m_db.Query("INSERT INTO jobs(id,serial,session,snapshot,parent,target,block,issued,expires,manifest) VALUES(?,?,?,?,?,?,?,?,?,?)");
    insert.Bind(1, job.id).Bind(2, job.serial).Bind(3, job.session).Bind(4, job.snapshot).Bind(5, m_parent).Bind(6, s.target).Bind(7, BlockHex(job.block)).Bind(8, job.issued).Bind(9, job.expires).Bind(10, job.manifest.write()).Row();
    tx.Commit();
    s.latest = job.id;
    ++m_jobs_issued;
    // Keep one current job per connection. Older jobs remain durable and can
    // still be submitted by loading them, without an unbounded reconnect cache.
    for (auto old = m_jobs.begin(); old != m_jobs.end();) {
        if (old->second.session == session)
            old = m_jobs.erase(old);
        else
            ++old;
    }
    auto [it, added] = m_jobs.emplace(job.id, std::move(job));
    return it->second;
}
UniValue Service::Hello(int64_t& session, const std::string& ip, const UniValue& p)
{
    if (session != 0) throw ProtocolError{SESSION, "session already established"};
    Fields(p, {"version", "network", "genesis", "algorithm"}, {"payout", "resume_token"});
    if (p["version"].getInt<int>() != 1 || p["algorithm"].get_str() != "MercaHash-V1") throw ProtocolError{VERSION, "unsupported protocol/algorithm"};
    if (p["network"].get_str() != m_cfg.network || p["genesis"].get_str() != m_cfg.genesis) throw ProtocolError{IDENTITY, "network/genesis mismatch"};
    if (p["payout"].isNull() == p["resume_token"].isNull()) throw ProtocolError{MALFORMED, "specify payout or resume_token"};
    if (!m_healthy) throw ProtocolError{INTERNAL, "Core unavailable"};
    int64_t id = 0;
    Session state;
    state.ip = ip;
    state.started = Now();
    if (m_connections == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("connection namespace exhausted");
    state.connection = ++m_connections;
    std::string token;
    if (!p["resume_token"].isNull()) {
        token = p["resume_token"].get_str();
        if (token.size() != 64 || !IsHex(token)) throw ProtocolError{SESSION, "invalid resume token"};
        auto q = m_db.Query("SELECT id,script,target FROM sessions WHERE token_hash=?");
        q.Bind(1, Digest("MCA-POOL/1/session", token));
        if (!q.Row()) throw ProtocolError{SESSION, "unknown resume token"};
        id = q.Integer(0);
        state.script = q.Text(1);
        state.target = q.Text(2);
        if (m_sessions.contains(id)) throw ProtocolError{SESSION, "session already connected"};
        auto latest = m_db.Query("SELECT id FROM jobs WHERE session=? ORDER BY serial DESC LIMIT 1");
        latest.Bind(1, id);
        if (latest.Row()) {
            auto j = LoadJob(latest.Text(0));
            if (j.expires > Now() && j.block.hashPrevBlock.GetHex() == m_parent) {
                state.latest = j.id;
                m_jobs.emplace(j.id, std::move(j));
            }
        }
        auto update = m_db.Query("UPDATE sessions SET connected=1 WHERE id=?");
        update.Bind(1, id).Row();
    } else {
        if (!m_db.SessionAllowed(ip, Now(), m_cfg.registration_window_seconds, m_cfg.new_sessions_per_ip, m_cfg.new_sessions_global)) throw ProtocolError{RATE, "new session registration limit reached; resume existing session"};
        try {
            state.script = m_rpc.ValidatePayout(p["payout"].get_str());
        } catch (const std::invalid_argument&) {
            throw ProtocolError{PAYOUT, "native PQ payout address required"};
        }
        state.target = m_cfg.share_target;
        auto known = m_db.Query("SELECT script FROM identities WHERE script=?");
        known.Bind(1, state.script);
        const auto count = m_db.IdentityCount() + (!known.Row());
        if (count > m_cfg.payout_cap || RequiredBlockBytes(m_template, count) > m_template["sizelimit"].getInt<uint64_t>()) throw ProtocolError{CAPACITY, "payout admission exceeds recipient/template capacity"};
        if (ParseTarget(state.target) < TemplateTarget(m_template)) throw ProtocolError{CAPACITY, "share target harder than network target"};
        token = RandomToken();
        Transaction tx{m_db};
        if (!m_db.Register(state.script, ip, Now(), m_cfg.registration_window_seconds, m_cfg.new_identities_per_ip, m_cfg.new_identities_global, m_cfg.payout_cap)) throw ProtocolError{RATE, "new payout identity registration limit reached"};
        auto q = m_db.Query("INSERT INTO sessions(script,target,token_hash,created) VALUES(?,?,?,?)");
        q.Bind(1, state.script).Bind(2, state.target).Bind(3, Digest("MCA-POOL/1/session", token)).Bind(4, Now()).Row();
        id = m_db.LastInsert();
        m_db.RecordSession(id, ip, Now());
        tx.Commit();
    }
    session = id;
    state.pending = std::count_if(m_pending.begin(), m_pending.end(), [&](const auto& item) { return item.second.job.session == id; });
    m_sessions.emplace(id, std::move(state));
    UniValue reply{UniValue::VOBJ};
    reply.pushKV("version", 1);
    reply.pushKV("algorithm", "MercaHash-V1");
    reply.pushKV("network", m_cfg.network);
    reply.pushKV("genesis", m_cfg.genesis);
    reply.pushKV("session", std::to_string(id));
    reply.pushKV("resume_token", token);
    reply.pushKV("extranonce_namespace", std::to_string(id));
    reply.pushKV("pool_name", "Mercatura Pool");
    reply.pushKV("fee_base_units", 0);
    reply.pushKV("heartbeat_seconds", m_cfg.heartbeat_seconds);
    return reply;
}
std::optional<UniValue> Service::Handle(int64_t& session, const std::string& ip, const Request& request)
{
    try {
        if (request.method == "hello") return Success(request.id, Hello(session, ip, request.params));
        if (session == 0 || !m_sessions.contains(session)) throw ProtocolError{SESSION, "hello required"};
        auto& s = m_sessions.at(session);
        if (request.method == "ping") {
            Fields(request.params, {});
            UniValue o{UniValue::VOBJ};
            o.pushKV("time", Now());
            return Success(request.id, o);
        }
        if (request.method == "status") {
            Fields(request.params, {});
            return Success(request.id, Stats());
        }
        if (request.method == "getjob") {
            Fields(request.params, {});
            return Success(request.id, GetJob(session).Message());
        }
        if (request.method == "manifest") {
            Fields(request.params, {"job_id"});
            const auto id = request.params["job_id"].get_str();
            if (id.size() != 64 || !IsHex(id)) throw ProtocolError{MALFORMED, "invalid job identifier"};
            auto job = LoadJob(id);
            UniValue audit = job.manifest;
            UniValue candidates{UniValue::VARR};
            auto records = m_db.Query("SELECT hash,state,confirmations FROM candidates WHERE job=?");
            records.Bind(1, id);
            while (records.Row()) {
                UniValue c{UniValue::VOBJ};
                c.pushKV("hash", records.Text(0));
                c.pushKV("state", records.Text(1));
                c.pushKV("confirmations", records.Integer(2));
                candidates.push_back(std::move(c));
            }
            audit.pushKV("candidates", candidates);
            return Success(request.id, audit); // Public audit data, no tokens.
        }
        if (request.method != "submit") throw ProtocolError{MALFORMED, "unknown method"};
        Fields(request.params, {"job_id", "nonce"}, {"ntime"});
        const auto id = request.params["job_id"].get_str();
        if (id.size() != 64 || !IsHex(id)) throw ProtocolError{MALFORMED, "invalid job identifier"};
        const auto nonce = Uint32(request.params["nonce"]);
        auto found = m_jobs.find(id);
        Job job = found == m_jobs.end() ? LoadJob(id) : found->second;
        if (job.session != session) throw ProtocolError{SESSION, "cross-session job rejected"};
        if (!request.params["ntime"].isNull() && Uint32(request.params["ntime"]) != job.block.nTime) throw ProtocolError{MALFORMED, "timestamp rolling is not permitted in protocol v1"};
        job.block.nNonce = nonce;
        const auto work = job.block.GetHash().GetHex();
        auto duplicate = m_db.Query("SELECT status FROM submissions WHERE work=?");
        duplicate.Bind(1, work);
        if (duplicate.Row() && duplicate.Text(0) != "interrupted") {
            auto q = m_db.Query("SELECT sequence FROM shares WHERE work=?");
            q.Bind(1, work);
            UniValue message = Failure(request.id, DUPLICATE, "work already submitted");
            if (q.Row()) {
                auto candidate = m_db.Query("SELECT hash FROM candidates WHERE hash=?");
                candidate.Bind(1, work);
                message.pushKV("receipt", Receipt(work, q.Integer(0), job, candidate.Row()));
            }
            return message;
        }
        const auto received = Now();
        if (!m_healthy || job.expires <= received || job.block.hashPrevBlock.GetHex() != m_parent) throw ProtocolError{STALE, "expired or stale parent job"};
        if (s.pending >= 2) throw ProtocolError{OVERLOAD, "session has two outstanding verification requests"};
        auto verification = m_verifier.Submit(job.block);
        if (!verification) throw ProtocolError{OVERLOAD, "verification queue is full"};
        auto persist = m_db.Query("INSERT INTO submissions VALUES(?,?,?,?,?,?) ON CONFLICT(work) DO UPDATE SET status='queued',received=excluded.received WHERE submissions.status='interrupted'");
        persist.Bind(1, work).Bind(2, job.id).Bind(3, session).Bind(4, static_cast<int64_t>(nonce)).Bind(5, "queued").Bind(6, received).Row();
        // Verification needs exact public block state, not copies of the
        // template and accounting JSON. The originals are already durable.
        job.manifest.setNull();
        job.snapshot_manifest.setNull();
        m_pending.emplace(work, Pending{request.id, std::move(job), work, std::move(*verification), received, s.connection});
        ++s.pending;
        return {};
    } catch (const ProtocolError& error) {
        ++m_request_errors;
        if (request.method == "submit") {
            ++m_rejected;
            if (error.code == STALE) ++m_stale;
        }
        return Failure(request.id, error.code, error.what());
    } catch (const UniValue::type_error&) {
        ++m_request_errors;
        if (request.method == "submit") ++m_rejected;
        return Failure(request.id, MALFORMED, "field type or integer range invalid");
    } catch (const std::exception&) {
        ++m_request_errors;
        if (request.method == "submit") ++m_rejected;
        return Failure(request.id, INTERNAL, "coordinator operation failed");
    }
}
UniValue Service::Receipt(const std::string& work, int64_t sequence, const Job& job, bool candidate)
{
    UniValue o{UniValue::VOBJ};
    o.pushKV("sequence", std::to_string(sequence));
    o.pushKV("receipt_id", Digest("MCA-PPLNS/1/receipt", m_cfg.genesis + ":" + work + ":" + std::to_string(sequence)));
    o.pushKV("work_id", work);
    o.pushKV("job_id", job.id);
    o.pushKV("snapshot_id", job.snapshot);
    o.pushKV("score", WorkScore(Number::Parse(job.share_target.GetHex(), true)).Decimal());
    o.pushKV("accepted", true);
    o.pushKV("network_candidate", candidate);
    return o;
}
std::vector<Response> Service::Tick()
{
    std::vector<Response> replies;
    if (m_stopping && m_pending.empty()) return replies;
    if (!m_stopping && Now() - m_poll_time >= 1) {
        const auto old = m_parent;
        try {
            Refresh();
        } catch (const std::exception&) {
            m_healthy = false;
            m_health = "Core unavailable; work paused";
            m_poll_time = Now();
        }
        if (m_parent != old)
            for (const auto& [id, s] : m_sessions) {
                UniValue p{UniValue::VOBJ};
                p.pushKV("parent", m_parent);
                p.pushKV("clean_jobs", true);
                replies.push_back({id, Notify("parent", p)});
            }
    }
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        auto& p = it->second;
        if (p.verification.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
            ++it;
            continue;
        }
        auto result = p.verification.get();
        UniValue reply;
        const auto session = p.job.session;
        if (auto s = m_sessions.find(session); s != m_sessions.end() && s->second.pending) --s->second.pending;
        try {
            const auto hash = UintToArith256(result.hash);
            const bool candidate = !result.cancelled && result.error.empty() && hash <= p.job.network_target;
            int error = 0;
            std::string message;
            if (result.cancelled) {
                error = CANCELLED;
                message = "verification cancelled during shutdown";
            } else if (!result.error.empty()) {
                error = INTERNAL;
                message = result.error;
            } else if (p.job.block.hashPrevBlock.GetHex() != m_parent || p.job.expires <= p.received) {
                error = STALE;
                message = "job parent became stale during verification";
            } else if (hash > p.job.share_target) {
                error = LOW_WORK;
                message = "hash exceeds assigned share target";
            }
            if (error) {
                auto q = m_db.Query("UPDATE submissions SET status=? WHERE work=?");
                q.Bind(1, error == CANCELLED ? "interrupted" : error == STALE ? "stale" :
                                                                                "rejected")
                    .Bind(2, p.work)
                    .Row();
                ++m_rejected;
                if (error == STALE) ++m_stale;
                reply = Failure(p.request, error, message);
            } else {
                auto identity = m_db.Query("SELECT script FROM sessions WHERE id=?");
                identity.Bind(1, session);
                if (!identity.Row()) throw std::runtime_error("session disappeared");
                const auto script = identity.Text(0);
                const auto score = WorkScore(Number::Parse(p.job.share_target.GetHex(), true));
                Transaction tx{m_db};
                auto share = m_db.Query("INSERT INTO shares(work,job,session,script,score,accepted) VALUES(?,?,?,?,?,?)");
                share.Bind(1, p.work).Bind(2, p.job.id).Bind(3, session).Bind(4, script).Bind(5, score.Decimal()).Bind(6, Now()).Row();
                const auto sequence = m_db.LastInsert();
                auto accepted = m_db.Query("UPDATE submissions SET status='accepted' WHERE work=?");
                accepted.Bind(1, p.work).Row();
                if (candidate) {
                    auto q = m_db.Query("INSERT INTO candidates(hash,job,snapshot,block,state,created) VALUES(?,?,?,?,?,?) ON CONFLICT(hash) DO NOTHING");
                    q.Bind(1, p.work).Bind(2, p.job.id).Bind(3, p.job.snapshot).Bind(4, BlockHex(p.job.block)).Bind(5, "discovered").Bind(6, Now()).Row();
                }
                tx.Commit(); // Accepted share and candidate durable together before ACK/RPC.
                if (auto s = m_sessions.find(session); s != m_sessions.end() && s->second.connection == p.connection) {
                    ++s->second.accepted;
                    s->second.work += score;
                }
                reply = Success(p.request, Receipt(p.work, sequence, p.job, candidate));
                if (candidate) m_reconcile_time = 0;
            }
        } catch (const std::exception&) {
            m_healthy = false;
            m_health = "accounting error; work paused";
            reply = Failure(p.request, INTERNAL, "durable accounting failed; acceptance not acknowledged");
        }
        // A resumed namespace must not receive an old connection's request ID.
        // The durable receipt remains available through duplicate replay.
        if (auto s = m_sessions.find(session); s != m_sessions.end() && s->second.connection == p.connection) replies.push_back({session, std::move(reply)});
        it = m_pending.erase(it);
    }
    if (!m_stopping && Now() - m_reconcile_time >= m_cfg.reconcile_seconds) {
        try {
            Reconcile();
        } catch (const std::exception&) {
            m_health = "chain reconciliation unavailable";
        }
        m_reconcile_time = Now();
    }
    return replies;
}
void Service::Reconcile()
{
    auto q = m_db.Query("SELECT hash,job,block,state,ever_active FROM candidates ORDER BY created");
    while (q.Row()) {
        const auto hash = q.Text(0);
        std::string state = q.Text(3);
        const bool ever = q.Integer(4) != 0;
        int64_t confirmations = 0;
        std::string rpc_result;
        if (state == "discovered" || state == "submitted" || state == "unknown") {
            auto returned = m_rpc.Call("submitblock", Params(q.Text(2)));
            rpc_result = returned.write();
            state = returned.isNull() || (returned.isStr() && returned.get_str() == "duplicate") ? "submitted" : "rejected";
        }
        bool active = false;
        try {
            auto header = m_rpc.Call("getblockheader", Params(hash));
            confirmations = header["confirmations"].getInt<int64_t>();
            if (confirmations > 0) {
                UniValue height{UniValue::VARR};
                height.push_back(header["height"]);
                active = m_rpc.Call("getblockhash", height).get_str() == hash;
            }
            if (active)
                state = confirmations > COINBASE_MATURITY ? "matured" : "active";
            else if (confirmations < 0 || ever)
                state = "orphaned";
        } catch (const RpcError& error) {
            if (error.code != -5 && error.code != -8) throw;
            if (ever)
                state = "orphaned";
            else if (state != "rejected")
                state = "unknown";
        }
        Transaction tx{m_db};
        auto update = m_db.Query("UPDATE candidates SET state=?,rpc_result=CASE WHEN ?='' THEN rpc_result ELSE ? END,confirmations=?,reconciled=?,ever_active=MAX(ever_active,?) WHERE hash=?");
        update.Bind(1, state).Bind(2, rpc_result).Bind(3, rpc_result).Bind(4, confirmations).Bind(5, Now()).Bind(6, static_cast<int64_t>(active)).Bind(7, hash).Row();
        if (state != q.Text(3)) {
            auto event = m_db.Query("INSERT INTO events(kind,identity,detail,time) VALUES('candidate',?,?,?)");
            event.Bind(1, hash).Bind(2, state).Bind(3, Now()).Row();
        }
        tx.Commit();
    }
}
void Service::Disconnect(int64_t session)
{
    if (!session) return;
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        if (it->second.session == session)
            it = m_jobs.erase(it);
        else
            ++it;
    }
    m_sessions.erase(session);
    auto q = m_db.Query("UPDATE sessions SET connected=0 WHERE id=?");
    q.Bind(1, session).Row();
}
void Service::Shutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    m_verifier.Stop();
    // Finish hashes already running and mark queued requests interrupted. A
    // disconnected client can retry interrupted requests using its resume token.
    Tick();
    for (const auto& [id, s] : m_sessions) {
        auto q = m_db.Query("UPDATE sessions SET connected=0 WHERE id=?");
        q.Bind(1, id).Row();
    }
    m_sessions.clear();
}
UniValue Service::Stats()
{
    UniValue o{UniValue::VOBJ};
    o.pushKV("healthy", m_healthy);
    o.pushKV("health", m_health);
    o.pushKV("pool_name", "Mercatura Pool");
    o.pushKV("fee_base_units", 0);
    o.pushKV("parent", m_parent);
    o.pushKV("connected_sessions", static_cast<uint64_t>(m_sessions.size()));
    o.pushKV("rejected_share_requests_since_start", m_rejected);
    o.pushKV("stale_share_requests_since_start", m_stale);
    o.pushKV("request_errors_since_start", m_request_errors);
    auto q = m_db.Query("SELECT COUNT(*) FROM shares");
    q.Row();
    o.pushKV("accepted_shares", q.Integer(0));
    o.pushKV("assigned_target", m_cfg.share_target);
    o.pushKV("assigned_work_score", WorkScore(Number::Parse(m_cfg.share_target, true)).Decimal());
    o.pushKV("verification_queue_depth", static_cast<uint64_t>(m_verifier.Depth()));
    o.pushKV("verification_workers", static_cast<uint64_t>(m_cfg.verifier_workers));
    o.pushKV("verification_completed", m_verifier.completed.load());
    const auto duration = std::max<int64_t>(1, Now() - m_started);
    o.pushKV("verification_per_second", static_cast<double>(m_verifier.completed.load()) / duration);
    o.pushKV("verification_mean_us", m_verifier.completed.load() ? m_verifier.microseconds.load() / m_verifier.completed.load() : 0);
    o.pushKV("jobs_issued_since_start", m_jobs_issued);
    o.pushKV("cached_jobs", static_cast<uint64_t>(m_jobs.size()));
    Number work;
    auto recent = m_db.Query("SELECT score FROM shares WHERE accepted>=?");
    recent.Bind(1, Now() - 300);
    while (recent.Row())
        work += Number::Parse(recent.Text(0));
    o.pushKV("hashrate_estimate", static_cast<double>(std::stold(work.Decimal()) / std::min<int64_t>(300, duration)));
    o.pushKV("hashrate_window_seconds", std::min<int64_t>(300, duration));
    UniValue blocks{UniValue::VOBJ};
    auto states = m_db.Query("SELECT state,COUNT(*) FROM candidates GROUP BY state");
    while (states.Row())
        blocks.pushKV(states.Text(0), states.Integer(1));
    o.pushKV("blocks", blocks);
    auto totals = m_db.Query("SELECT COUNT(*) FROM candidates");
    totals.Row();
    o.pushKV("block_candidates", totals.Integer(0));
    o.pushKV("admitted_identities", static_cast<uint64_t>(m_db.IdentityCount()));
    Number matured;
    auto rewards = m_db.Query("SELECT jobs.manifest FROM candidates JOIN jobs ON candidates.job=jobs.id WHERE candidates.state='matured'");
    while (rewards.Row()) {
        UniValue manifest;
        if (!manifest.read(rewards.Text(0))) throw std::runtime_error("invalid matured reward manifest");
        matured += Number{manifest["reward"].getInt<uint64_t>()};
    }
    o.pushKV("currently_matured_base_units", matured.Decimal());
    UniValue sessions{UniValue::VARR};
    for (const auto& [id, s] : m_sessions) {
        UniValue item{UniValue::VOBJ};
        item.pushKV("session", std::to_string(id));
        item.pushKV("assigned_target", s.target);
        item.pushKV("assigned_score", WorkScore(Number::Parse(s.target, true)).Decimal());
        item.pushKV("accepted_since_connect", s.accepted);
        sessions.push_back(std::move(item));
    }
    o.pushKV("sessions", sessions);
    return o;
}
} // namespace pool
