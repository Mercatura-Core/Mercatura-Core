// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/config.h>

#include <pool/job.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace pool {
int64_t Now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
Config Config::Load(const std::string& path)
{
    std::ifstream file{path, std::ios::binary};
    if (!file) throw std::runtime_error("cannot open configuration");
    std::string raw;
    char c;
    while (file.get(c)) {
        if (raw.size() >= 16384) throw std::runtime_error("configuration too large");
        raw += c;
    }
    UniValue json;
    if (!json.read(raw) || !json.isObject()) throw std::runtime_error("invalid configuration JSON");
    Config cfg;
    std::set<std::string> seen;
    for (const auto& key : json.getKeys())
        if (!seen.insert(key).second) throw std::runtime_error("duplicate configuration field");
    auto str = [&](const char* key, std::string& dest) { if (!json[key].isNull()) { dest=json[key].get_str(); seen.erase(key); } };
    auto number = [&](const char* key, auto& dest) {
        if (!json[key].isNull()) {
            const auto v = json[key].getInt<int64_t>();
            using Type = std::remove_reference_t<decltype(dest)>;
            if (v < 0 || static_cast<uint64_t>(v) > static_cast<uint64_t>(std::numeric_limits<Type>::max())) throw std::runtime_error("configuration number out of range");
            dest = static_cast<Type>(v);
            seen.erase(key);
        }
    };
    auto boolean = [&](const char* key, bool& dest) { if (!json[key].isNull()) { dest=json[key].get_bool(); seen.erase(key); } };
    str("network", cfg.network);
    str("genesis", cfg.genesis);
    str("rpc_url", cfg.rpc_url);
    str("rpc_user", cfg.rpc_user);
    str("rpc_password", cfg.rpc_password);
    str("rpc_cookie", cfg.rpc_cookie);
    str("rpc_ca", cfg.rpc_ca);
    str("database", cfg.database);
    str("listen", cfg.listen);
    str("tls_certificate", cfg.tls_certificate);
    str("tls_key", cfg.tls_key);
    str("share_target", cfg.share_target);
    number("port", cfg.port);
    number("window_blocks", cfg.window_blocks);
    number("payout_cap", cfg.payout_cap);
    number("refresh_seconds", cfg.refresh_seconds);
    number("lifetime_seconds", cfg.lifetime_seconds);
    number("verifier_workers", cfg.verifier_workers);
    number("verifier_queue", cfg.verifier_queue);
    number("verifier_memory_mib", cfg.verifier_memory_mib);
    number("max_sessions", cfg.max_sessions);
    number("max_sessions_per_ip", cfg.max_sessions_per_ip);
    number("messages_per_session", cfg.messages_per_session);
    number("messages_per_ip", cfg.messages_per_ip);
    number("max_message_bytes", cfg.max_message_bytes);
    number("max_output_bytes", cfg.max_output_bytes);
    number("heartbeat_seconds", cfg.heartbeat_seconds);
    number("handshake_seconds", cfg.handshake_seconds);
    number("reconcile_seconds", cfg.reconcile_seconds);
    number("rpc_timeout_seconds", cfg.rpc_timeout_seconds);
    number("rpc_connect_timeout_seconds", cfg.rpc_connect_timeout_seconds);
    number("stats_seconds", cfg.stats_seconds);
    number("registration_window_seconds", cfg.registration_window_seconds);
    number("new_identities_per_ip", cfg.new_identities_per_ip);
    number("new_identities_global", cfg.new_identities_global);
    number("new_sessions_per_ip", cfg.new_sessions_per_ip);
    number("new_sessions_global", cfg.new_sessions_global);
    boolean("plaintext_regtest", cfg.plaintext_regtest);
    boolean("warmup", cfg.warmup);
    if (json["handshake_seconds"].isNull()) cfg.handshake_seconds = std::min(cfg.handshake_seconds, cfg.heartbeat_seconds);
    if (json["rpc_connect_timeout_seconds"].isNull()) cfg.rpc_connect_timeout_seconds = std::min(cfg.rpc_connect_timeout_seconds, cfg.rpc_timeout_seconds);
    if (!seen.empty()) throw std::runtime_error("unknown/null configuration field: " + *seen.begin());
    cfg.Validate();
    return cfg;
}
void Config::Validate() const
{
    if (network != "main" && network != "test" && network != "regtest") throw std::invalid_argument("supported networks: main, test, regtest");
    ParseTarget(genesis);
    ParseTarget(share_target);
    if (rpc_cookie.empty() == rpc_user.empty() || (!rpc_user.empty() && rpc_password.empty())) throw std::invalid_argument("configure exactly one RPC authentication method");
    if (database.empty() || port == 0 || window_blocks == 0 || window_blocks > 100 || payout_cap == 0 || payout_cap > 1000) throw std::invalid_argument("invalid accounting/listener configuration");
    if (refresh_seconds < 1 || refresh_seconds > 3600 || lifetime_seconds < refresh_seconds || lifetime_seconds > 7200) throw std::invalid_argument("invalid job cadence/lifetime");
    if (verifier_workers == 0 || verifier_workers > 64 || verifier_queue == 0 || verifier_queue > 4096 || verifier_memory_mib < verifier_workers * 160) throw std::invalid_argument("verification memory/queue budget invalid");
    if (max_sessions == 0 || max_sessions > 1024 || max_sessions_per_ip == 0 || max_sessions_per_ip > max_sessions || messages_per_session == 0 || messages_per_ip == 0 || messages_per_session > 10000 || messages_per_ip > 100000) throw std::invalid_argument("invalid session/rate bounds");
    if (max_message_bytes < 512 || max_message_bytes > 65536 || max_output_bytes < 65536 || max_output_bytes > 16 * 1024 * 1024 || heartbeat_seconds < 5 || heartbeat_seconds > 3600 || reconcile_seconds < 1 || reconcile_seconds > 3600 || rpc_timeout_seconds < 1 || rpc_timeout_seconds > 60) throw std::invalid_argument("invalid I/O bounds");
    if (stats_seconds > 86400) throw std::invalid_argument("invalid statistics log cadence");
    if (handshake_seconds < 5 || handshake_seconds > heartbeat_seconds || rpc_connect_timeout_seconds < 1 || rpc_connect_timeout_seconds > rpc_timeout_seconds) throw std::invalid_argument("invalid handshake/RPC connection deadline");
    if (registration_window_seconds < 60 || registration_window_seconds > 86400 || new_identities_per_ip == 0 || new_identities_global < new_identities_per_ip || new_identities_global > 1000) throw std::invalid_argument("invalid new identity registration limits");
    if (new_sessions_per_ip == 0 || new_sessions_global < new_sessions_per_ip || new_sessions_global > 10000) throw std::invalid_argument("invalid new session registration limits");
    if (plaintext_regtest) {
        if (network != "regtest" || listen != "127.0.0.1" || !tls_certificate.empty() || !tls_key.empty()) throw std::invalid_argument("plaintext requires explicit IPv4 localhost regtest");
    } else if (tls_certificate.empty() || tls_key.empty())
        throw std::invalid_argument("TLS certificate and key required; no plaintext fallback");
}
std::string Config::Policy() const { return "MCA-PPLNS/1:blocks=" + std::to_string(window_blocks) + ":lifetime-identities=" + std::to_string(payout_cap); }
} // namespace pool
