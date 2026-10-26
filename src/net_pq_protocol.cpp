// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <net_pq_protocol.h>
#include <algorithm>
#include <cassert>

namespace pq {
namespace {
void Append(Bytes& out, std::span<const uint8_t> data) { out.insert(out.end(), data.begin(), data.end()); }
std::span<const uint8_t> Text(std::string_view s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
std::span<const std::byte> B(std::span<const uint8_t> s) { return std::as_bytes(s); }
std::span<std::byte> W(std::span<uint8_t> s) { return std::as_writable_bytes(s); }
bool Derive(std::span<const uint8_t> prk, std::string_view label, const Digest& context, std::span<uint8_t> out)
{
    auto info = Domain("Mercatura/P2PKeySchedule/v1");
    Append(info, Domain(label));
    info.push_back(out.size() & 255); info.push_back(out.size() >> 8);
    Append(info, context);
    return Expand(prk, info, out);
}
Bytes Confirmation(uint8_t role, const Digest& hash)
{
    auto msg = Domain("Mercatura/P2PConfirm/v1"); msg.push_back(role); Append(msg, hash); return msg;
}
}
Bytes Domain(std::string_view label)
{
    assert(label.size() <= 65535);
    Bytes out{uint8_t(label.size() & 255), uint8_t(label.size() >> 8)};
    Append(out, Text(label)); return out;
}
Digest Hash(std::span<const uint8_t> bytes) { Digest out; SHA3_384().Write(bytes).Finalize(out); return out; }
Digest Fingerprint(const Network& network, std::span<const uint8_t> pub)
{
    assert(pub.size() == 1952);
    auto input = Domain("Mercatura/P2PIdentity/v1");
    Append(input, Bytes{1, 0}); Append(input, network); Append(input, Bytes{0xa0, 0x07}); Append(input, pub);
    return Hash(input);
}
Bytes Header(uint8_t type)
{
    assert(type >= 1 && type <= 5);
    const auto size = RECORD_SIZES[type] - 12;
    return {type, uint8_t(type % 2 ? 0 : 1), 1, 0, 1, 0, 0, 0, uint8_t(size), uint8_t(size >> 8), uint8_t(size >> 16), uint8_t(size >> 24)};
}
bool ValidHeader(std::span<const uint8_t> header, uint8_t expected)
{
    if (expected < 1 || expected > 5 || header.size() != 12) return false;
    return std::ranges::equal(header, Header(expected));
}
std::optional<std::string> DecodeCommand(std::span<const uint8_t>& contents)
{
    if (contents.empty()) return {};
    uint8_t id = contents.front(); contents = contents.subspan(1);
    if (id) {
        if (id >= COMMANDS.size()) return {};
        return std::string{COMMANDS[id]};
    }
    if (contents.size() < 12) return {};
    size_t length = 0;
    while (length < 12 && contents[length]) {
        if (contents[length] < 32 || contents[length] > 127) return {};
        ++length;
    }
    if (!length) return {};
    for (size_t i = length; i < 12; ++i) if (contents[i]) return {};
    std::string result(reinterpret_cast<const char*>(contents.data()), length);
    contents = contents.subspan(12); return result;
}
Handshake::Handshake(bool initiator, Network network, std::optional<Digest> pin, std::shared_ptr<const Identity> identity, Random rng)
    : m_initiator(initiator), m_network(network), m_pin(pin), m_identity(std::move(identity)), m_rng(std::move(rng)), m_next(initiator ? 2 : 1)
{
    Add(Domain("Mercatura/P2PTranscript/v1")); Add(Domain("Mercatura/P2P/v1")); Add(Text(PREFACE));
}
Handshake::~Handshake() { memory_cleanse(&m_transcript, sizeof(m_transcript)); }
void Handshake::Add(std::span<const uint8_t> data) { m_transcript.Write(data); }
Digest Handshake::Transcript(std::span<const uint8_t> tail) const { auto state = m_transcript; Digest out; state.Write(tail).Finalize(out); return out; }
Bytes Handshake::SignMessage(uint8_t role, const Digest& hash) const
{
    auto msg = Domain("Mercatura/P2PSignature/v1"); msg.push_back(role); Append(msg, hash); return msg;
}
bool Handshake::Fail()
{
    m_failed = true; m_ready = false; m_kem.priv.Clear(); m_ss_r.Clear(); m_ss_i.Clear();
    m_prk.Clear(); m_kc_r.Clear(); m_kc_i.Clear(); m_identity.reset(); m_keys = SessionKeys{};
    return false;
}
bool Handshake::CheckPeer(std::span<const uint8_t> pub)
{
    if (pub.size() != m_peer.size()) return false;
    std::copy(pub.begin(), pub.end(), m_peer.begin());
    m_pin_mismatch = m_pin && !Equal(*m_pin, Fingerprint(m_network, m_peer));
    return !m_pin_mismatch;
}
bool Handshake::Start(Bytes& out)
{
    if (!m_initiator || m_started || m_failed || m_next != 2 || !m_identity) return Fail();
    m_started = true;
    if (!m_kem.Generate(m_rng)) return Fail();
    Secret<32> nonce;
    if (!m_rng(nonce.bytes)) return Fail();
    auto record = Header(1); record.reserve(RECORD_SIZES[1]);
    Append(record, m_network); Append(record, nonce.bytes); Append(record, m_kem.pub); Append(record, m_identity->pub);
    Add(record); out.assign(PREFACE.begin(), PREFACE.end()); Append(out, record); return true;
}
bool Handshake::Schedule()
{
    Secret<68> ikm;
    ikm.bytes[0] = ikm.bytes[34] = 32;
    std::copy(m_ss_r.bytes.begin(), m_ss_r.bytes.end(), ikm.bytes.begin() + 2);
    std::copy(m_ss_i.bytes.begin(), m_ss_i.bytes.end(), ikm.bytes.begin() + 36);
    auto salt_input = Domain("Mercatura/P2PExtract/v1"); Append(salt_input, m_h3);
    auto salt = Hash(salt_input);
    Extract(salt, ikm.bytes, m_prk.bytes);
    m_ss_r.Clear(); m_ss_i.Clear(); m_kem.priv.Clear();
    return Derive(m_prk.bytes, "confirm/r2i", m_h3, m_kc_r.bytes) && Derive(m_prk.bytes, "confirm/i2r", m_h3, m_kc_i.bytes);
}
bool Handshake::Finish()
{
    m_keys.h5 = Transcript();
    if (!(Derive(m_prk.bytes, "app/i2r/length", m_keys.h5, m_keys.i_length.bytes) &&
          Derive(m_prk.bytes, "app/i2r/payload", m_keys.h5, m_keys.i_payload.bytes) &&
          Derive(m_prk.bytes, "app/r2i/length", m_keys.h5, m_keys.r_length.bytes) &&
          Derive(m_prk.bytes, "app/r2i/payload", m_keys.h5, m_keys.r_payload.bytes) &&
          Derive(m_prk.bytes, "session/id", m_keys.h5, m_keys.id))) return Fail();
    m_prk.Clear(); m_kc_r.Clear(); m_kc_i.Clear(); m_identity.reset();
    m_ready = true; m_next = 0; return true;
}
bool Handshake::Advance(std::span<const uint8_t> record, Bytes& out)
{
    out.clear();
    if ((m_initiator && !m_started) || m_failed || m_ready || m_next < 1 || m_next > 5 || record.size() != RECORD_SIZES[m_next] || !ValidHeader(record.first(12), m_next)) return Fail();
    auto body = record.subspan(12);
    if (m_next == 1 || m_next == 2) {
        if (!Equal(body.first(85), m_network) || !CheckPeer(body.last(1952))) return Fail();
    }
    if (m_next == 1) {
        Add(record);
        if (!m_identity || !m_kem.Generate(m_rng)) return Fail();
        KEMCiphertext ct;
        Secret<32> nonce;
        if (!Encapsulate(body.subspan(117, 1184), ct, m_ss_r, m_rng) || !m_rng(nonce.bytes)) return Fail();
        auto response = Header(2); response.reserve(RECORD_SIZES[2]);
        Append(response, m_network); Append(response, nonce.bytes); Append(response, m_kem.pub); Append(response, ct); Append(response, m_identity->pub);
        Add(Text(PREFACE)); Add(response);
        out.assign(PREFACE.begin(), PREFACE.end()); Append(out, response); m_next = 3;
    } else if (m_next == 2) {
        Add(Text(PREFACE)); Add(record);
        KEMCiphertext ct;
        ++m_decapsulations;
        if (!m_kem.Decapsulate(body.subspan(1301, 1088), m_ss_r) || !Encapsulate(body.subspan(117, 1184), ct, m_ss_i, m_rng)) return Fail();
        out = Header(3); out.reserve(RECORD_SIZES[3]); Append(out, ct); m_h3 = Transcript(out);
        Signature sig;
        if (!m_identity->Sign(SignMessage(0, m_h3), sig, m_rng)) return Fail();
        Append(out, sig); Add(out);
        if (!Schedule()) return Fail();
        m_next = 4;
    } else if (m_next == 3) {
        // Correction A: pin and signature verification MUST precede decapsulation.
        if (m_pin && !Equal(*m_pin, Fingerprint(m_network, m_peer))) return Fail();
        m_h3 = Transcript(record.first(12 + 1088));
        if (!Verify(m_peer, SignMessage(0, m_h3), body.subspan(1088))) return Fail();
        ++m_decapsulations;
        if (!m_kem.Decapsulate(body.first(1088), m_ss_i) || !Schedule()) return Fail();
        Add(record); out = Header(4); out.reserve(RECORD_SIZES[4]);
        Signature sig;
        if (!m_identity->Sign(SignMessage(1, Transcript(out)), sig, m_rng)) return Fail();
        Append(out, sig); Add(out);
        auto tag = MAC(m_kc_r.bytes, Confirmation(1, Transcript())); Append(out, tag); Add(tag); m_next = 5;
    } else if (m_next == 4) {
        if ((m_pin && !Equal(*m_pin, Fingerprint(m_network, m_peer))) || !Verify(m_peer, SignMessage(1, Transcript(record.first(12))), body.first(3309))) return Fail();
        Add(record.first(12 + 3309));
        if (!Equal(body.last(48), MAC(m_kc_r.bytes, Confirmation(1, Transcript())))) return Fail();
        Add(body.last(48)); out = Header(5); out.reserve(RECORD_SIZES[5]);
        auto tag = MAC(m_kc_i.bytes, Confirmation(0, Transcript(out))); Append(out, tag); Add(out);
        if (!Finish()) return false;
    } else {
        if (!Equal(body, MAC(m_kc_i.bytes, Confirmation(0, Transcript(record.first(12)))))) return Fail();
        Add(record); if (!Finish()) return false;
    }
    return true;
}

PacketCipher::PacketCipher(std::span<const uint8_t> length, std::span<const uint8_t> payload, Digest h5, uint8_t role)
    : m_length(B(length), 224), m_payload(B(payload), 224), m_h5(h5), m_role(role) { assert(role <= 1); }
Bytes PacketCipher::AAD() const
{
    auto aad = Domain("Mercatura/P2PPacket/v1"); Append(aad, m_h5); aad.push_back(m_role); Append(aad, m_wire_length); return aad;
}
bool PacketCipher::Encrypt(std::span<const uint8_t> contents, uint8_t control, Bytes& out)
{
    if (m_failed || m_pending || m_packets >= (uint64_t{1} << 32) || contents.size() > MAX_CONTENTS || (control != 0 && control != 128) || (control == 0 && contents.empty())) { m_failed = true; return false; }
    std::array<uint8_t, 3> length{uint8_t(contents.size()), uint8_t(contents.size() >> 8), uint8_t(contents.size() >> 16)};
    m_length.Crypt(B(length), W(m_wire_length));
    out.resize(contents.size() + 20); std::copy(m_wire_length.begin(), m_wire_length.end(), out.begin());
    m_payload.Encrypt(B({&control, 1}), B(contents), B(AAD()), W(std::span{out}.subspan(3)));
    ++m_packets; return true;
}
std::optional<uint32_t> PacketCipher::DecryptLength(std::span<const uint8_t> wire)
{
    if (m_failed || m_pending || wire.size() != 3 || m_packets >= (uint64_t{1} << 32)) { m_failed = true; return {}; }
    std::copy(wire.begin(), wire.end(), m_wire_length.begin());
    std::array<uint8_t, 3> length;
    m_length.Crypt(B(wire), W(length));
    uint32_t value = uint32_t{length[0]} | (uint32_t{length[1]} << 8) | (uint32_t{length[2]} << 16);
    if (value > MAX_CONTENTS) { m_failed = true; return {}; }
    m_pending = value; return value;
}
bool PacketCipher::Decrypt(std::span<const uint8_t> body, Bytes& contents, bool& decoy)
{
    if (m_failed || !m_pending || body.size() != size_t{*m_pending} + 17) { m_failed = true; return false; }
    uint8_t control{0}; Bytes plain(*m_pending);
    if (!m_payload.Decrypt(B(body), B(AAD()), W({&control, 1}), W(plain)) || (control != 0 && control != 128) || (control == 0 && plain.empty())) { m_failed = true; return false; }
    decoy = control == 128; if (!decoy) contents = std::move(plain);
    ++m_packets; m_pending.reset(); return true;
}
} // namespace pq
