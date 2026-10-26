// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_NET_PQ_PROTOCOL_H
#define BITCOIN_NET_PQ_PROTOCOL_H

#include <crypto/mercatura_p2p_crypto.h>
#include <crypto/chacha20.h>
#include <crypto/chacha20poly1305.h>
#include <crypto/sha3.h>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace pq {
using Bytes = std::vector<uint8_t>;
using Network = std::array<uint8_t, 85>;
inline constexpr std::string_view PREFACE{"Mercatura/P2P/PQ"};
inline constexpr size_t MAX_CONTENTS{4'000'013};
inline constexpr std::array<size_t, 6> RECORD_SIZES{0, 3265, 4353, 4409, 3369, 60};
Bytes Domain(std::string_view label);
Digest Hash(std::span<const uint8_t> bytes);
Digest Fingerprint(const Network& network, std::span<const uint8_t> pub);
Bytes Header(uint8_t type);
bool ValidHeader(std::span<const uint8_t> header, uint8_t expected);
inline constexpr std::array<std::string_view, 29> COMMANDS{"", "addr", "block", "blocktxn", "cmpctblock", "feefilter", "filteradd", "filterclear", "filterload", "getblocks", "getblocktxn", "getdata", "getheaders", "headers", "inv", "mempool", "merkleblock", "notfound", "ping", "pong", "sendcmpct", "tx", "getcfilters", "cfilter", "getcfheaders", "cfheaders", "getcfcheckpt", "cfcheckpt", "addrv2"};
std::optional<std::string> DecodeCommand(std::span<const uint8_t>& contents);

struct SessionKeys {
    Secret<32> i_length, i_payload, r_length, r_payload;
    std::array<uint8_t, 32> id{};
    Digest h5{};
};

/** Worker-owned: never access while the job is running. No sockets or locks. */
class Handshake {
    const bool m_initiator;
    const Network m_network;
    const std::optional<Digest> m_pin;
    std::shared_ptr<const Identity> m_identity;
    Random m_rng;
    KEMKey m_kem;
    IdentityPublic m_peer{};
    Secret<32> m_ss_r, m_ss_i;
    Secret<48> m_prk, m_kc_r, m_kc_i;
    SHA3_384 m_transcript;
    Digest m_h3{};
    uint8_t m_next;
    unsigned m_decapsulations{0};
    bool m_failed{false}, m_ready{false}, m_started{false}, m_pin_mismatch{false};
    void Add(std::span<const uint8_t> data);
    Digest Transcript(std::span<const uint8_t> tail = {}) const;
    Bytes SignMessage(uint8_t role, const Digest& hash) const;
    bool CheckPeer(std::span<const uint8_t> pub);
    bool Schedule();
    bool Finish();
    bool Fail();
    SessionKeys m_keys;
public:
    Handshake(bool initiator, Network network, std::optional<Digest> pin, std::shared_ptr<const Identity> identity, Random rng);
    ~Handshake();
    Handshake(const Handshake&) = delete;
    Handshake& operator=(const Handshake&) = delete;
    bool Start(Bytes& out);
    bool Advance(std::span<const uint8_t> record, Bytes& out);
    uint8_t Next() const { return m_next; }
    bool Ready() const { return m_ready && !m_failed; }
    bool PinMismatch() const { return m_pin_mismatch; }
    unsigned Decapsulations() const { return m_decapsulations; }
    const IdentityPublic& Peer() const { return m_peer; }
    SessionKeys TakeKeys() { return std::move(m_keys); }
};

/** One directional packet state. Never reuse after any decoding error. */
class PacketCipher {
    FSChaCha20 m_length;
    FSChaCha20Poly1305 m_payload;
    const Digest m_h5;
    const uint8_t m_role;
    uint64_t m_packets{0};
    bool m_failed{false};
    std::optional<uint32_t> m_pending;
    std::array<uint8_t, 3> m_wire_length{};
    Bytes AAD() const;
public:
    PacketCipher(std::span<const uint8_t> length, std::span<const uint8_t> payload, Digest h5, uint8_t role);
    bool Encrypt(std::span<const uint8_t> contents, uint8_t control, Bytes& out);
    std::optional<uint32_t> DecryptLength(std::span<const uint8_t> wire);
    bool Decrypt(std::span<const uint8_t> body, Bytes& contents, bool& decoy);
    uint64_t Packets() const { return m_packets; }
    uint64_t Epoch() const { return m_packets / 224; }
};
} // namespace pq
#endif // BITCOIN_NET_PQ_PROTOCOL_H
