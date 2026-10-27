// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_NET_PQ_STATUS_H
#define BITCOIN_NET_PQ_STATUS_H
#include <array>
#include <cstdint>

namespace pq {
enum class Failure : unsigned { FRAMING, AUTHENTICATION_OR_CRYPTO, PIN, DEADLINE, RESOURCE, PACKET, COUNT };
struct Status {
    bool required{false};
    std::array<uint8_t,85> network{};
    unsigned pending{0}, queued{0}, active{0}, connected{0};
    uint64_t established{0}, known_addresses{0};
    std::array<uint64_t, unsigned(Failure::COUNT)> failures{};
};
} // namespace pq
#endif // BITCOIN_NET_PQ_STATUS_H
