// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_NET_PQ_H
#define BITCOIN_NET_PQ_H

#include <net.h>
#include <net_pq_context.h>

namespace pq {
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
