// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <crypto/mercatura_mldsa.h>
#include <net_pq_protocol.h>
#include <test/data/pq_p2p_candidate1.json.h>
#include <univalue.h>
#include <boost/test/unit_test.hpp>
#include <limits>

namespace pq::test {
/** Defined only in this test translation unit; no production state setter. */
struct PacketCipherAccess {
    static void NearLimit(PacketCipher& cipher, uint64_t packets) {
        BOOST_REQUIRE_EQUAL(cipher.m_packets, 0U);
        BOOST_REQUIRE(!cipher.m_failed && !cipher.m_pending);
        BOOST_REQUIRE(packets >= (uint64_t{1} << 32) - 1);
        cipher.m_packets = packets;
    }
    static bool Failed(const PacketCipher& cipher) { return cipher.m_failed; }
    // Destructive private probe, used ONLY after all terminal API checks. Compare
    // with an independent control to detect FS advancement on rejected calls,
    // without reading padding/uninitialized buffers or copying secret objects.
    // This never resets m_failed or enables a successful PacketCipher retry.
    static std::array<std::byte,20> Probe(PacketCipher& cipher) {
        std::array<std::byte,3> length{};
        std::array<std::byte,1> plain{};
        std::array<std::byte,20> result{};
        cipher.m_length.Crypt(length,std::span{result}.first(3));
        cipher.m_payload.Encrypt(plain,{},std::span{result}.subspan(3));
        return result;
    }
};
}

namespace {
using namespace pq;
Bytes Hex(const UniValue& value)
{
    const auto& text = value.get_str();
    auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    Bytes bytes;
    for (size_t n = 0; n < text.size(); n += 2) bytes.push_back(16 * nibble(text[n]) + nibble(text[n + 1]));
    return bytes;
}
const UniValue& Vector()
{
    static const auto fixture = [] { UniValue v; BOOST_REQUIRE(v.read(json_tests::pq_p2p_candidate1)); return v; }();
    return fixture;
}
Random Draws(std::initializer_list<const char*> names, uint8_t session)
{
    std::vector<Bytes> draws;
    for (auto name : names) {
        auto bytes = Hex(Vector()["inputs"][name]);
        bytes[0] ^= session; // Public, deterministic, fresh coins/nonces in every test session.
        draws.push_back(std::move(bytes));
    }
    return [draws = std::move(draws), n = size_t{0}](std::span<uint8_t> out) mutable {
        if (n == draws.size() || draws[n].size() != out.size()) return false;
        std::copy(draws[n].begin(), draws[n].end(), out.begin()); ++n; return true;
    };
}
struct Pair {
    Network network{};
    std::shared_ptr<Identity> idi{std::make_shared<Identity>()}, idr{std::make_shared<Identity>()};
    std::unique_ptr<Handshake> i, r;
    explicit Pair(uint8_t session = 0, bool pinned = true)
    {
        const auto n = Hex(Vector()["N"]); std::copy(n.begin(), n.end(), network.begin());
        BOOST_REQUIRE(idi->FromSeed(Hex(Vector()["inputs"]["identity_seed_I"])));
        BOOST_REQUIRE(idr->FromSeed(Hex(Vector()["inputs"]["identity_seed_R"])));
        i = std::make_unique<Handshake>(true, network, pinned ? std::optional{Fingerprint(network, idr->pub)} : std::nullopt, idi,
            Draws({"kem_coins_I", "nonce_I", "enc_coins_I", "sign_randomness_I"}, session));
        r = std::make_unique<Handshake>(false, network, pinned ? std::optional{Fingerprint(network, idi->pub)} : std::nullopt, idr,
            Draws({"kem_coins_R", "enc_coins_R", "nonce_R", "sign_randomness_R"}, session));
    }
    Handshake& Receiver(unsigned type) { return type % 2 ? *r : *i; }
    Bytes At(unsigned type)
    {
        Bytes wire, next; BOOST_REQUIRE(i->Start(wire)); wire.erase(wire.begin(), wire.begin() + 16);
        for (unsigned step = 1; step < type; ++step) {
            BOOST_REQUIRE(Receiver(step).Advance(wire, next)); wire = std::move(next);
            if (step == 1) wire.erase(wire.begin(), wire.begin() + 16);
        }
        return wire;
    }
};
void Dead(Handshake& receiver, const Bytes& retry)
{
    Bytes out{42};
    BOOST_CHECK(!receiver.Ready());
    BOOST_CHECK(!receiver.Advance(retry, out)); BOOST_CHECK(out.empty());
    auto keys = receiver.TakeKeys();
    auto zero = [](const auto& value) { return std::ranges::all_of(value, [](auto b) { return b == 0; }); };
    BOOST_CHECK(zero(keys.i_length.bytes) && zero(keys.i_payload.bytes) && zero(keys.r_length.bytes) && zero(keys.r_payload.bytes));
    BOOST_CHECK(zero(keys.id) && zero(keys.h5));
}
}

BOOST_AUTO_TEST_SUITE(pq_assurance_tests)
BOOST_AUTO_TEST_CASE(canonical_headers_all_bits_and_exact_record_sizes)
{
    for (uint8_t type = 1; type <= 5; ++type) {
        const auto header = Header(type);
        BOOST_CHECK(ValidHeader(header, type));
        for (size_t offset = 0; offset < 12; ++offset) {
            for (unsigned bit = 0; bit < 8; ++bit) {
                auto altered = header; altered[offset] ^= 1U << bit;
                BOOST_CHECK(!ValidHeader(altered, type));
            }
            // Also exercise rejection in the actual state, not only the header helper.
            Pair p; auto record = p.At(type); record[offset] ^= 1;
            Bytes out; auto& receiver = p.Receiver(type);
            const auto decaps = receiver.Decapsulations();
            BOOST_CHECK(!receiver.Advance(record, out)); BOOST_CHECK(out.empty());
            BOOST_CHECK_EQUAL(receiver.Decapsulations(), decaps); Dead(receiver, record);
        }
        for (size_t length : {size_t{0}, size_t{1}, size_t{11}, size_t{12}, RECORD_SIZES[type] - 1, RECORD_SIZES[type] + 1}) {
            Pair p; auto record = p.At(type); record.resize(length);
            Bytes out; BOOST_CHECK(!p.Receiver(type).Advance(record, out)); Dead(p.Receiver(type), record);
        }
    }
}
BOOST_AUTO_TEST_CASE(order_roles_duplicates_and_terminal_states)
{
    for (unsigned expected = 1; expected <= 5; ++expected) {
        for (unsigned supplied = 1; supplied <= 5; ++supplied) {
            if (expected == supplied) continue;
            Pair p; p.At(expected); auto wrong = Hex(Vector()["messages"][supplied - 1]["wire"]);
            Bytes out; BOOST_CHECK(!p.Receiver(expected).Advance(wrong, out)); Dead(p.Receiver(expected), wrong);
        }
    }
    Pair never_started; Bytes out;
    BOOST_CHECK(!never_started.i->Advance(Hex(Vector()["messages"][1]["wire"]), out)); Dead(*never_started.i, {});
    Pair twice; BOOST_REQUIRE(twice.i->Start(out)); BOOST_CHECK(!twice.i->Start(out)); Dead(*twice.i, {});
    Pair wrong_role; BOOST_CHECK(!wrong_role.r->Start(out)); Dead(*wrong_role.r, {});
    Pair finished; auto m5 = finished.At(5); BOOST_REQUIRE(finished.r->Advance(m5, out));
    BOOST_REQUIRE(finished.i->Ready() && finished.r->Ready());
    BOOST_CHECK(!finished.r->Advance(m5, out)); Dead(*finished.r, m5);
    BOOST_CHECK(!finished.i->Advance(Hex(Vector()["messages"][3]["wire"]), out)); Dead(*finished.i, {});
}
BOOST_AUTO_TEST_CASE(in_transit_transcript_fields_cannot_complete_authentication)
{
    // Route actual generated responses after each alteration. Replaying an
    // unrelated frozen suffix could otherwise conceal a missing transcript bind.
    struct Field { unsigned type; size_t offset; };
    for (auto [type, offset] : std::vector<Field>{{1,12}, {1,97}, {1,129}, {1,1313},
             {2,12}, {2,97}, {2,129}, {2,1313}, {2,2401}, {3,12}, {3,1100}, {4,12}, {4,3321}, {5,12}}) {
        BOOST_TEST_CONTEXT("record " << type << " offset " << offset) {
            Pair p(0, false); auto wire = p.At(type); wire[offset] ^= 1;
            bool failed{false}; Bytes next;
            for (unsigned step = type; step <= 5; ++step) {
                auto& receiver = p.Receiver(step);
                if (!receiver.Advance(wire, next)) { failed = true; Dead(receiver, wire); break; }
                wire = std::move(next);
                if (step == 1) wire.erase(wire.begin(), wire.begin() + 16);
            }
            BOOST_CHECK(failed); BOOST_CHECK(!(p.i->Ready() && p.r->Ready()));
        }
    }
    // Replace with another correctly encoded public KEM key/identity so success
    // cannot be prevented solely by public-key modulus checking.
    for (unsigned type : {1U, 2U}) for (bool identity : {false, true}) {
        Pair p(0, false), other(1, false); auto wire = p.At(type), replacement = other.At(type);
        if (identity) {
            const auto& pub = type == 1 ? p.idr->pub : p.idi->pub;
            std::copy(pub.begin(), pub.end(), wire.end() - pub.size());
        } else std::copy_n(replacement.begin() + 129, 1184, wire.begin() + 129);
        bool failed{false}; Bytes next;
        for (unsigned step = type; step <= 5; ++step) {
            if (!p.Receiver(step).Advance(wire, next)) { failed = true; break; }
            wire = std::move(next); if (step == 1) wire.erase(wire.begin(), wire.begin() + 16);
        }
        BOOST_CHECK(failed); BOOST_CHECK(!(p.i->Ready() && p.r->Ready()));
    }
}
BOOST_AUTO_TEST_CASE(pure_mldsa_context_and_role_separation)
{
    Pair p;
    const auto message = Hex(Vector()["signature_inputs"][0]["message"]);
    const auto signature = Hex(Vector()["signature_inputs"][0]["signature"]);
    BOOST_REQUIRE(Verify(p.idi->pub, message, signature));
    for (std::string_view context : std::array<std::string_view, 4>{"", "Mercatura/P2PAuth/v1", std::string_view{"Mercatura/P2PAuth/v1\0", 21}, "Mercatura/PQAuth/v1"}) {
        const bool valid = mercatura_mldsa65_verify(signature.data(), signature.size(), message.data(), message.size(),
            reinterpret_cast<const uint8_t*>(context.data()), context.size(), p.idi->pub.data(), p.idi->pub.size());
        BOOST_CHECK_EQUAL(valid, context == "Mercatura/P2PAuth/v1");
    }
    auto reflected = message; reflected[reflected.size() - 49] ^= 1;
    BOOST_CHECK(!Verify(p.idi->pub, reflected, signature));
    BOOST_CHECK(!Verify(p.idr->pub, message, signature));
}
BOOST_AUTO_TEST_CASE(both_hellos_check_each_network_byte_before_crypto)
{
    Pair p;
    for (unsigned type : {1U,2U}) for (size_t offset=0;offset<p.network.size();++offset) {
        unsigned calls{0};
        auto draws=Draws({"kem_coins_I","nonce_I","enc_coins_I","sign_randomness_I"},0);
        auto rng=[&](std::span<uint8_t> out) { ++calls; return draws(out); };
        Handshake receiver(type==2,p.network,{},type==2 ? p.idi : p.idr,rng);
        Bytes out; if (type==2) BOOST_REQUIRE(receiver.Start(out));
        auto record=Hex(Vector()["messages"][type-1]["wire"]); record[12+offset]^=1;
        const auto before=calls;
        BOOST_CHECK(!receiver.Advance(record,out)); BOOST_CHECK(out.empty());
        BOOST_CHECK_EQUAL(calls,before); BOOST_CHECK_EQUAL(receiver.Decapsulations(),0U);
        Dead(receiver,record);
    }
}
BOOST_AUTO_TEST_CASE(all_confirmation_bytes_required_after_valid_signature)
{
    for (unsigned type : {4U, 5U}) for (size_t byte = 0; byte < 48; ++byte) {
        Pair p; auto wire = p.At(type); Bytes out;
        if (type == 4) {
            BOOST_REQUIRE(Verify(p.idr->pub, Hex(Vector()["signature_inputs"][1]["message"]), std::span{wire}.subspan(12, 3309)));
        }
        wire[wire.size() - 48 + byte] ^= 1;
        auto& receiver = p.Receiver(type);
        BOOST_CHECK(!receiver.Advance(wire, out)); BOOST_CHECK(out.empty()); Dead(receiver, wire);
    }
}
BOOST_AUTO_TEST_CASE(four_interleaved_sessions_fresh_coins_and_cross_confirmation)
{
    std::array<std::unique_ptr<Pair>, 4> pairs;
    std::array<Bytes, 4> wires;
    for (unsigned n = 0; n < pairs.size(); ++n) { pairs[n] = std::make_unique<Pair>(n); wires[n] = pairs[n]->At(1); }
    for (unsigned type = 1; type <= 5; ++type) {
        for (unsigned n = 0; n < pairs.size(); ++n) {
            BOOST_CHECK(!pairs[n]->r->Ready());
            for (unsigned k = 0; k < n; ++k) {
                BOOST_CHECK(wires[n] != wires[k]);
                if (type <= 2) {
                    BOOST_CHECK(!Equal(std::span{wires[n]}.subspan(97,32), std::span{wires[k]}.subspan(97,32)));
                    BOOST_CHECK(!Equal(std::span{wires[n]}.subspan(129,1184), std::span{wires[k]}.subspan(129,1184)));
                }
            }
        }
        for (unsigned n = 0; n < pairs.size(); ++n) {
            Bytes next; BOOST_REQUIRE(pairs[n]->Receiver(type).Advance(wires[n], next));
            if (type == 1) next.erase(next.begin(), next.begin() + 16);
            wires[n] = std::move(next);
        }
    }
    std::array<SessionKeys, 4> keys;
    for (unsigned n = 0; n < pairs.size(); ++n) {
        BOOST_REQUIRE(pairs[n]->i->Ready() && pairs[n]->r->Ready());
        keys[n] = pairs[n]->i->TakeKeys(); auto peer = pairs[n]->r->TakeKeys();
        BOOST_CHECK(Equal(keys[n].id, peer.id)); BOOST_CHECK(Equal(keys[n].i_payload.bytes, peer.i_payload.bytes));
        BOOST_CHECK(Equal(keys[n].i_length.bytes, peer.i_length.bytes)); BOOST_CHECK(Equal(keys[n].r_length.bytes, peer.r_length.bytes));
        BOOST_CHECK(Equal(keys[n].r_payload.bytes, peer.r_payload.bytes)); BOOST_CHECK(Equal(keys[n].h5, peer.h5));
        BOOST_CHECK(!Equal(keys[n].i_length.bytes, keys[n].r_length.bytes));
        BOOST_CHECK(!Equal(keys[n].i_payload.bytes, keys[n].r_payload.bytes));
        for (unsigned k = 0; k < n; ++k) {
            BOOST_CHECK(!Equal(keys[n].id, keys[k].id)); BOOST_CHECK(!Equal(keys[n].i_payload.bytes, keys[k].i_payload.bytes));
            BOOST_CHECK(!Equal(keys[n].i_length.bytes, keys[k].i_length.bytes)); BOOST_CHECK(!Equal(keys[n].r_length.bytes, keys[k].r_length.bytes));
            BOOST_CHECK(!Equal(keys[n].r_payload.bytes, keys[k].r_payload.bytes));
        }
    }
    for (unsigned type : {3U, 4U, 5U}) for (unsigned source = 0; source < 4; ++source) for (unsigned target = 0; target < 4; ++target) {
        if (source == target) continue;
        Pair a(source), b(target); auto foreign = a.At(type); b.At(type); Bytes out;
        BOOST_CHECK(!b.Receiver(type).Advance(foreign, out)); Dead(b.Receiver(type), foreign);
        if (type == 3) BOOST_CHECK_EQUAL(b.r->Decapsulations(), 0U);
        // Failure in B does not change the correctly owned state in A.
        BOOST_REQUIRE(a.Receiver(type).Advance(foreign, out));
    }
}
BOOST_AUTO_TEST_CASE(packet_rekey_boundaries_stale_state_and_terminal_errors)
{
    Pair p; auto m5 = p.At(5); Bytes out; BOOST_REQUIRE(p.r->Advance(m5, out));
    auto keys = p.i->TakeKeys();
    for (uint8_t role : {0, 1}) for (unsigned boundary : {0U, 222U, 223U, 224U, 225U, 447U, 448U, 449U, 671U, 672U}) {
        const auto& length = role ? keys.r_length.bytes : keys.i_length.bytes;
        const auto& payload = role ? keys.r_payload.bytes : keys.i_payload.bytes;
        PacketCipher enc(length, payload, keys.h5, role), dec(length, payload, keys.h5, role);
        PacketCipher corrupted(length, payload, keys.h5, role);
        Bytes wire, first;
        for (unsigned n = 0; n <= boundary; ++n) {
            const bool discard = n % 7 == 0;
            BOOST_REQUIRE(enc.Encrypt(Bytes{18, uint8_t(n)}, discard ? 128 : 0, wire));
            if (n == 0) first = wire;
            BOOST_REQUIRE(dec.DecryptLength(std::span{wire}.first(3)));
            Bytes plain{42}; bool decoy{false};
            BOOST_REQUIRE(dec.Decrypt(std::span{wire}.subspan(3), plain, decoy));
            BOOST_CHECK_EQUAL(decoy, discard); BOOST_CHECK(plain == (discard ? Bytes{42} : Bytes{18, uint8_t(n)}));
            BOOST_CHECK_EQUAL(dec.Packets(), n + 1); BOOST_CHECK_EQUAL(dec.Epoch(), (n + 1) / 224);
            auto altered = wire; if (n == boundary) altered.back() ^= 1;
            BOOST_REQUIRE(corrupted.DecryptLength(std::span{altered}.first(3)));
            Bytes sentinel{42}; bool ignored{false};
            if (n == boundary) {
                BOOST_CHECK(!corrupted.Decrypt(std::span{altered}.subspan(3), sentinel, ignored));
                BOOST_CHECK(sentinel == Bytes{42}); BOOST_CHECK_EQUAL(corrupted.Packets(), n);
                BOOST_CHECK(!corrupted.DecryptLength(std::span{wire}.first(3)));
            } else BOOST_REQUIRE(corrupted.Decrypt(std::span{wire}.subspan(3), sentinel, ignored));
        }
        // A replay into advanced state must never release a previous packet.
        Bytes sentinel{42}; bool decoy{false}; auto len = dec.DecryptLength(std::span{first}.first(3));
        if (len) BOOST_CHECK(!dec.Decrypt(std::span{first}.subspan(3), sentinel, decoy));
        BOOST_CHECK(sentinel == Bytes{42}); BOOST_CHECK(!dec.DecryptLength(std::span{wire}.first(3)));
        // A fresh reconnect's packet state cannot decode an advanced epoch.
        if (boundary >= 224) {
            PacketCipher fresh(length, payload, keys.h5, role);
            len = fresh.DecryptLength(std::span{wire}.first(3));
            if (len) BOOST_CHECK(!fresh.Decrypt(std::span{wire}.subspan(3), sentinel, decoy));
            BOOST_CHECK(sentinel == Bytes{42});
        }
    }
}
BOOST_AUTO_TEST_CASE(packet_call_order_errors_are_terminal)
{
    std::array<uint8_t,32> key{}; Digest hash{};
    PacketCipher enc(key,key,hash,0); Bytes wire; BOOST_REQUIRE(enc.Encrypt(Bytes{18,42},0,wire));
    for (unsigned kind=0;kind<4;++kind) {
        PacketCipher dec(key,key,hash,0); Bytes sentinel{42}; bool ignored{false};
        if (kind == 0) BOOST_CHECK(!dec.Decrypt(std::span{wire}.subspan(3),sentinel,ignored));
        else if (kind == 1) BOOST_CHECK(!dec.DecryptLength(std::span{wire}.first(2)));
        else {
            BOOST_REQUIRE(dec.DecryptLength(std::span{wire}.first(3)));
            if (kind == 2) BOOST_CHECK(!dec.DecryptLength(std::span{wire}.first(3)));
            else BOOST_CHECK(!dec.Decrypt(std::span{wire}.subspan(3,wire.size()-4),sentinel,ignored));
        }
        BOOST_CHECK(sentinel == Bytes{42}); BOOST_CHECK_EQUAL(dec.Packets(),0U);
        BOOST_CHECK(!dec.DecryptLength(std::span{wire}.first(3)));
        BOOST_CHECK(!dec.Decrypt(std::span{wire}.subspan(3),sentinel,ignored));
    }
}
BOOST_AUTO_TEST_CASE(packet_limit_last_permitted_packet_and_terminal_retries)
{
    // Inject ONLY the outer guard counter. The underlying FS cipher state is
    // fresh: this tests the 2^32 guard, not billions of packets/rekeys or their
    // actual final nonce. Real 224-packet transitions have separate frozen tests.
    using test::PacketCipherAccess;
    constexpr uint64_t limit = uint64_t{1} << 32;
    Pair p; auto m5 = p.At(5); Bytes out; BOOST_REQUIRE(p.r->Advance(m5,out));
    auto keys = p.i->TakeKeys();
    for (uint8_t role : {0,1}) for (bool discard : {false,true}) {
        BOOST_TEST_CONTEXT("role=" << unsigned(role) << " decoy=" << discard) {
            const auto& length = role ? keys.r_length.bytes : keys.i_length.bytes;
            const auto& payload = role ? keys.r_payload.bytes : keys.i_payload.bytes;
            PacketCipher enc(length,payload,keys.h5,role), dec(length,payload,keys.h5,role);
            PacketCipherAccess::NearLimit(enc,limit - 1); PacketCipherAccess::NearLimit(dec,limit - 1);
            const Bytes contents{18,42}; Bytes wire;
            BOOST_REQUIRE(enc.Encrypt(contents,discard ? 128 : 0,wire));
            BOOST_CHECK_EQUAL(enc.Packets(),limit); BOOST_CHECK_EQUAL(enc.Epoch(),limit / 224);
            auto size = dec.DecryptLength(std::span{wire}.first(3));
            BOOST_REQUIRE(size); BOOST_CHECK_EQUAL(*size,contents.size());
            BOOST_CHECK_EQUAL(dec.Packets(),limit - 1);
            Bytes plain{99}; bool decoy{!discard};
            BOOST_REQUIRE(dec.Decrypt(std::span{wire}.subspan(3),plain,decoy));
            BOOST_CHECK_EQUAL(decoy,discard); BOOST_CHECK(plain == (discard ? Bytes{99} : contents));
            BOOST_CHECK_EQUAL(dec.Packets(),limit); BOOST_CHECK_EQUAL(dec.Epoch(),limit / 224);
            for (unsigned retry = 0; retry < 3; ++retry) {
                Bytes rejected{77};
                BOOST_CHECK(!enc.Encrypt(contents,0,rejected)); BOOST_CHECK(rejected == Bytes{77});
                BOOST_CHECK(!dec.DecryptLength(std::span{wire}.first(3)));
                plain = {99}; decoy = true;
                BOOST_CHECK(!dec.Decrypt(std::span{wire}.subspan(3),plain,decoy));
                BOOST_CHECK(plain == Bytes{99}); BOOST_CHECK(decoy);
                // A failed instance cannot be revived by changing call direction.
                BOOST_CHECK(!dec.Encrypt(contents,0,rejected)); BOOST_CHECK(rejected == Bytes{77});
                BOOST_CHECK(!enc.DecryptLength(std::span{wire}.first(3)));
                BOOST_CHECK(!enc.Decrypt(std::span{wire}.subspan(3),plain,decoy)); BOOST_CHECK(plain == Bytes{99});
                BOOST_CHECK(PacketCipherAccess::Failed(enc) && PacketCipherAccess::Failed(dec));
                BOOST_CHECK_EQUAL(enc.Packets(),limit); BOOST_CHECK_EQUAL(dec.Packets(),limit);
            }
            PacketCipher control(length,payload,keys.h5,role); Bytes control_wire;
            BOOST_REQUIRE(control.Encrypt(contents,discard ? 128 : 0,control_wire)); BOOST_CHECK(control_wire == wire);
            const auto expected = PacketCipherAccess::Probe(control);
            BOOST_CHECK(PacketCipherAccess::Probe(enc) == expected);
            BOOST_CHECK(PacketCipherAccess::Probe(dec) == expected);
            // Even after the test-only probe, production entry points stay dead.
            Bytes rejected{77};
            BOOST_CHECK(!enc.Encrypt(contents,0,rejected)); BOOST_CHECK(rejected == Bytes{77});
            BOOST_CHECK(!dec.DecryptLength(std::span{wire}.first(3)));
            BOOST_CHECK(PacketCipherAccess::Failed(enc) && PacketCipherAccess::Failed(dec));
            BOOST_CHECK_EQUAL(enc.Packets(),limit); BOOST_CHECK_EQUAL(dec.Packets(),limit);
        }
    }
    // Independent AKE with fresh session coins and traffic keys remains usable.
    Pair fresh(1); m5 = fresh.At(5); BOOST_REQUIRE(fresh.r->Advance(m5,out));
    auto new_keys = fresh.i->TakeKeys(); BOOST_CHECK(new_keys.id != keys.id);
    for (uint8_t role : {0,1}) {
        const auto& length = role ? new_keys.r_length.bytes : new_keys.i_length.bytes;
        const auto& payload = role ? new_keys.r_payload.bytes : new_keys.i_payload.bytes;
        BOOST_CHECK(!Equal(length,role ? keys.r_length.bytes : keys.i_length.bytes));
        BOOST_CHECK(!Equal(payload,role ? keys.r_payload.bytes : keys.i_payload.bytes));
        PacketCipher enc(length,payload,new_keys.h5,role), dec(length,payload,new_keys.h5,role);
        Bytes wire, plain; bool decoy{true};
        BOOST_REQUIRE(enc.Encrypt(Bytes{18,42},0,wire));
        BOOST_REQUIRE(dec.DecryptLength(std::span{wire}.first(3)));
        BOOST_REQUIRE(dec.Decrypt(std::span{wire}.subspan(3),plain,decoy));
        BOOST_CHECK(plain == (Bytes{18,42})); BOOST_CHECK(!decoy);
        BOOST_CHECK_EQUAL(enc.Packets(),1U); BOOST_CHECK_EQUAL(dec.Packets(),1U);
    }
}
BOOST_AUTO_TEST_CASE(packet_limit_rejects_at_and_above_limit_without_cipher_state_change)
{
    using test::PacketCipherAccess;
    constexpr uint64_t limit = uint64_t{1} << 32;
    std::array<uint8_t,32> length{}, payload{}; payload[0] = 1; Digest h5{};
    for (uint8_t role : {0,1}) for (uint64_t count : {limit,limit + 1,std::numeric_limits<uint64_t>::max()}) {
        BOOST_TEST_CONTEXT("role=" << unsigned(role) << " injected_count=" << count) {
            PacketCipher enc(length,payload,h5,role), dec(length,payload,h5,role), source(length,payload,h5,role);
            PacketCipherAccess::NearLimit(enc,count); PacketCipherAccess::NearLimit(dec,count);
            Bytes wire; BOOST_REQUIRE(source.Encrypt(Bytes{18,42},0,wire));
            for (unsigned retry = 0; retry < 3; ++retry) {
                Bytes rejected{77}, plain{99}; bool decoy{true};
                BOOST_CHECK(!enc.Encrypt(Bytes{18,42},0,rejected)); BOOST_CHECK(rejected == Bytes{77});
                BOOST_CHECK(!dec.DecryptLength(std::span{wire}.first(3)));
                BOOST_CHECK(!dec.Decrypt(std::span{wire}.subspan(3),plain,decoy));
                BOOST_CHECK(plain == Bytes{99}); BOOST_CHECK(decoy);
                BOOST_CHECK(PacketCipherAccess::Failed(enc) && PacketCipherAccess::Failed(dec));
                BOOST_CHECK_EQUAL(enc.Packets(),count); BOOST_CHECK_EQUAL(dec.Packets(),count);
            }
            PacketCipher control(length,payload,h5,role);
            const auto expected = PacketCipherAccess::Probe(control);
            BOOST_CHECK(PacketCipherAccess::Probe(enc) == expected);
            BOOST_CHECK(PacketCipherAccess::Probe(dec) == expected);
            BOOST_CHECK(PacketCipherAccess::Failed(enc) && PacketCipherAccess::Failed(dec));
            BOOST_CHECK_EQUAL(enc.Packets(),count); BOOST_CHECK_EQUAL(dec.Packets(),count);
        }
    }
}
BOOST_AUTO_TEST_SUITE_END()
