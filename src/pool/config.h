// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_CONFIG_H
#define BITCOIN_POOL_CONFIG_H

#include <univalue.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace pool {
struct Config {
    std::string network{"regtest"};
    std::string genesis;
    std::string rpc_url{"http://127.0.0.1:27773/"};
    std::string rpc_user;
    std::string rpc_password;
    std::string rpc_cookie;
    std::string rpc_ca;
    std::string database{"pool.sqlite"};
    std::string listen{"127.0.0.1"};
    uint16_t port{19444};
    std::string tls_certificate;
    std::string tls_key;
    bool plaintext_regtest{false};
    bool warmup{false};
    std::string share_target{"7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    unsigned int window_blocks{2};
    size_t payout_cap{1000};
    int64_t refresh_seconds{15};
    int64_t lifetime_seconds{45};
    size_t verifier_workers{1};
    size_t verifier_queue{16};
    size_t verifier_memory_mib{160};
    size_t max_sessions{128};
    size_t max_sessions_per_ip{16};
    size_t messages_per_session{20};
    size_t messages_per_ip{80};
    size_t max_message_bytes{4096};
    size_t max_output_bytes{8 * 1024 * 1024};
    int64_t heartbeat_seconds{60};
    int64_t handshake_seconds{30};
    int64_t reconcile_seconds{15};
    int64_t rpc_timeout_seconds{10};
    int64_t rpc_connect_timeout_seconds{10};
    int64_t stats_seconds{60};
    int64_t registration_window_seconds{3600};
    size_t new_identities_per_ip{4};
    size_t new_identities_global{32};
    size_t new_sessions_per_ip{16};
    size_t new_sessions_global{128};
    static Config Load(const std::string& path);
    void Validate() const;
    std::string Policy() const;
};
int64_t Now();
} // namespace pool
#endif // BITCOIN_POOL_CONFIG_H
