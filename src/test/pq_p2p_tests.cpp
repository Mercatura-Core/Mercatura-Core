// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <net_pq_protocol.h>
#include <test/data/pq_p2p_candidate1.json.h>
#include <test/data/pq_p2p_packets.json.h>
#include <univalue.h>
#include <boost/test/unit_test.hpp>
#include <string>

namespace {
using namespace pq;
Bytes Hex(const std::string& s)
{
    Bytes out;
    auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i < s.size(); i += 2) out.push_back(16 * nibble(s[i]) + nibble(s[i+1]));
    return out;
}
Bytes V(const UniValue& v) { return Hex(v.get_str()); }
Bytes Join(Bytes a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
template<size_t N> std::array<uint8_t, N> Array(const UniValue& v) { auto b = V(v); BOOST_REQUIRE_EQUAL(b.size(), N); std::array<uint8_t, N> a; std::copy(b.begin(), b.end(), a.begin()); return a; }
UniValue Candidate() { UniValue v; BOOST_REQUIRE(v.read(json_tests::pq_p2p_candidate1)); return v; }
Random Draws(std::vector<Bytes> draws)
{
    return [draws = std::move(draws), i = size_t{0}](std::span<uint8_t> out) mutable {
        if (i >= draws.size() || draws[i].size() != out.size()) return false;
        std::copy(draws[i].begin(), draws[i].end(), out.begin()); ++i; return true;
    };
}
struct Pair {
    UniValue v{Candidate()};
    std::shared_ptr<Identity> idi{std::make_shared<Identity>()}, idr{std::make_shared<Identity>()};
    std::unique_ptr<Handshake> i, r;
    Pair(bool pins = true, uint8_t session = 0) {
        BOOST_REQUIRE(idi->FromSeed(V(v["inputs"]["identity_seed_I"])));
        BOOST_REQUIRE(idr->FromSeed(V(v["inputs"]["identity_seed_R"])));
        auto n = Array<85>(v["N"]);
        auto nonce_i = V(v["inputs"]["nonce_I"]), nonce_r = V(v["inputs"]["nonce_R"]);
        nonce_i.back() ^= session;
        nonce_r.back() ^= session;
        i = std::make_unique<Handshake>(true, n, pins ? std::optional{Fingerprint(n, idr->pub)} : std::nullopt, idi,
            Draws({V(v["inputs"]["kem_coins_I"]), nonce_i, V(v["inputs"]["enc_coins_I"]), V(v["inputs"]["sign_randomness_I"])}));
        r = std::make_unique<Handshake>(false, n, pins ? std::optional{Fingerprint(n, idi->pub)} : std::nullopt, idr,
            Draws({V(v["inputs"]["kem_coins_R"]), V(v["inputs"]["enc_coins_R"]), nonce_r, V(v["inputs"]["sign_randomness_R"])}));
    }
    Bytes Record(int type) { return V(v["messages"][type-1]["wire"]); }
};
}

BOOST_AUTO_TEST_SUITE(pq_p2p_tests)
BOOST_AUTO_TEST_CASE(production_crypto_wrapper)
{
    auto v = Candidate();
    KEMKey i, r;
    BOOST_REQUIRE(i.Generate(Draws({V(v["inputs"]["kem_coins_I"])})));
    BOOST_REQUIRE(r.Generate(Draws({V(v["inputs"]["kem_coins_R"])})));
    BOOST_CHECK(std::ranges::equal(i.pub, V(v["public_keys"]["ek_I"])));
    KEMCiphertext ct; Secret<32> ss, received;
    BOOST_REQUIRE(Encapsulate(i.pub, ct, ss, Draws({V(v["inputs"]["enc_coins_R"])})));
    BOOST_CHECK(std::ranges::equal(ct, V(v["kem"]["ct_R"])));
    BOOST_CHECK(std::ranges::equal(ss.bytes, V(v["kem"]["ss_R"])));
    BOOST_REQUIRE(i.Decapsulate(ct, received)); BOOST_CHECK(Equal(ss.bytes, received.bytes));
    BOOST_REQUIRE(r.Decapsulate(ct, received)); BOOST_CHECK(!Equal(ss.bytes, received.bytes));
    ct[0] ^= 1; BOOST_REQUIRE(i.Decapsulate(ct, received)); BOOST_CHECK(!Equal(ss.bytes, received.bytes));
    BOOST_CHECK(!i.Decapsulate(std::span{ct}.first(1087), received));
    BOOST_CHECK(std::ranges::all_of(received.bytes, [](auto b) { return b == 0; }));
    auto fail_rng = [](std::span<uint8_t> out) { std::fill(out.begin(), out.end(), 42); return false; };
    BOOST_CHECK(!i.Generate(fail_rng)); BOOST_CHECK(std::ranges::all_of(i.priv.bytes, [](auto b) { return b == 0; }));
    BOOST_CHECK(!Encapsulate(r.pub, ct, ss, fail_rng));
    auto bad_pub = r.pub; bad_pub.fill(255);
    BOOST_CHECK(!Encapsulate(bad_pub, ct, ss, Draws({Bytes(32)})));
    Identity identity; Signature signature;
    BOOST_REQUIRE(identity.FromSeed(V(v["inputs"]["identity_seed_I"])));
    auto msg = V(v["signature_inputs"][0]["message"]);
    BOOST_REQUIRE(identity.Sign(msg, signature, Draws({V(v["inputs"]["sign_randomness_I"])})));
    BOOST_CHECK(std::ranges::equal(signature, V(v["signature_inputs"][0]["signature"])));
    BOOST_CHECK(Verify(identity.pub, msg, signature)); msg.back() ^= 1; BOOST_CHECK(!Verify(identity.pub, msg, signature));
    BOOST_CHECK(!identity.Sign(msg, signature, fail_rng)); BOOST_CHECK(!identity.Generate(fail_rng));
    for (const auto& row : v["expand"].getValues()) {
        Bytes out(row["length"].getInt<int>());
        BOOST_REQUIRE(Expand(V(v["extract"]["PRK"]), V(row["info"]), out));
        BOOST_CHECK(out == V(row["output"]));
    }
    Secret<48> prk; Extract(V(v["extract"]["salt"]), V(v["extract"]["IKM"]), prk.bytes);
    BOOST_CHECK(Equal(prk.bytes, V(v["extract"]["PRK"])));
    Bytes too_large(12241); BOOST_CHECK(!Expand(V(v["extract"]["PRK"]), {}, too_large));
    BOOST_CHECK(!Expand(Bytes(47), {}, ss.bytes));
    Bytes multi(96);
    BOOST_REQUIRE(Expand(Bytes(48,17), Bytes{'a',0,'b'}, multi));
    // Independently calculated with Python hmac.digest(SHA3-384), two RFC5869 blocks.
    BOOST_CHECK(multi == Hex("4312fb333b713a29d702da38a630a2990c91e81ea48357dca899f72990ec2528bcb5e8320cc12f1f94895393626af3eb222ad3db9334db760a41e509667af445d33974ad550b4970bb806911ce397e985c593ffc405b14735c333bd70609355f"));
}
BOOST_AUTO_TEST_CASE(exact_five_message_fixture)
{
    for (bool pinned : {false, true}) {
        Pair p(pinned); Bytes wire, next;
        BOOST_REQUIRE(p.i->Start(wire)); BOOST_CHECK(wire == Join(V(p.v["preface"]), p.Record(1)));
        BOOST_REQUIRE(p.r->Advance(std::span{wire}.subspan(16), next)); BOOST_CHECK(next == Join(V(p.v["preface"]), p.Record(2)));
        BOOST_REQUIRE(p.i->Advance(std::span{next}.subspan(16), wire)); BOOST_CHECK(wire == p.Record(3));
        BOOST_REQUIRE(p.r->Advance(wire, next)); BOOST_CHECK(next == p.Record(4));
        BOOST_CHECK(!p.r->Ready());
        BOOST_REQUIRE(p.i->Advance(next, wire)); BOOST_CHECK(wire == p.Record(5));
        BOOST_REQUIRE(p.r->Advance(wire, next)); BOOST_CHECK(next.empty());
        BOOST_REQUIRE(p.i->Ready() && p.r->Ready());
        auto ki = p.i->TakeKeys(), kr = p.r->TakeKeys();
        BOOST_CHECK(Equal(ki.id, kr.id)); BOOST_CHECK(Equal(ki.i_length.bytes, kr.i_length.bytes));
        BOOST_CHECK(Equal(ki.h5, V(p.v["transcripts"]["h5"])));
        BOOST_CHECK(Equal(ki.id, V(p.v["expand"][6]["output"])));
        BOOST_CHECK(Equal(ki.i_payload.bytes, V(p.v["expand"][3]["output"])));
    }
}
BOOST_AUTO_TEST_CASE(handshake_mutations_fail_closed)
{
    {
        Pair p; Bytes a,b; BOOST_REQUIRE(p.i->Start(a)); BOOST_REQUIRE(p.r->Advance(p.Record(1),b));
        auto m3=p.Record(3); m3[12+1088]^=1;
        BOOST_CHECK(!p.r->Advance(m3,b)); BOOST_CHECK_EQUAL(p.r->Decapsulations(),0U);
    }
    // Independently corrupt every framing header field, network, KEM and identity/signature/confirmation region.
    for (int type = 1; type <= 5; ++type) {
        std::vector<size_t> offsets{0, 1, 2, 4, 6, 8, 11, 12, RECORD_SIZES[type]-1};
        if (type <= 2) { offsets.push_back(12+85); offsets.push_back(12+117); offsets.push_back(12+1301); }
        if (type == 3) offsets.push_back(12+1088);
        for (size_t offset : offsets) {
            Pair p; Bytes a, b; BOOST_REQUIRE(p.i->Start(a));
            bool failed = false;
            for (int t = 1; t <= 5; ++t) {
                auto record = p.Record(t); if (t == type) record[offset] ^= 1;
                // Mutated hello nonces/KEM keys may be accepted initially, but must fail authentication later.
                auto& receiver = t % 2 ? p.r : p.i;
                bool ok = receiver->Advance(record, b);
                if (!ok) { failed = true; break; }
            }
            BOOST_CHECK_MESSAGE(failed, "mutation accepted at type " << type << " offset " << offset);
        }
    }
    Pair p; Bytes out; BOOST_CHECK(!p.r->Advance(p.Record(3), out));
    Pair q; auto m = q.Record(1); m.push_back(0); BOOST_CHECK(!q.r->Advance(m, out));
    Pair s; m = s.Record(1); m.pop_back(); BOOST_CHECK(!s.r->Advance(m, out));
}
BOOST_AUTO_TEST_CASE(invalid_kem_public_keys_fail_before_avoidable_work)
{
    Pair p; auto network=Array<85>(p.v["N"]);
    unsigned draws{0};
    auto rng=[&](std::span<uint8_t> bytes) { ++draws; std::fill(bytes.begin(),bytes.end(),42); return true; };
    Handshake responder(false,network,std::nullopt,p.idr,rng);
    auto m1=p.Record(1); std::fill(m1.begin()+129,m1.begin()+1313,255);
    Bytes response;
    BOOST_CHECK(!responder.Advance(m1,response));
    BOOST_CHECK_EQUAL(draws,0U);
    BOOST_REQUIRE(p.i->Start(response));
    auto m2=p.Record(2); std::fill(m2.begin()+129,m2.begin()+1313,255);
    BOOST_CHECK(!p.i->Advance(m2,response));
    BOOST_CHECK_EQUAL(p.i->Decapsulations(),0U);
}
BOOST_AUTO_TEST_CASE(cross_session_signature_and_confirmation_substitution)
{
    // Both sessions use the same valid identities, pins and KEM coins, but
    // different nonces. Valid messages from one must not authenticate the other.
    for (int substituted : {3, 4, 5}) {
        Pair a, b(true, 1);
        Bytes wire_a, wire_b, next_a, next_b;
        BOOST_REQUIRE(a.i->Start(wire_a));
        BOOST_REQUIRE(b.i->Start(wire_b));
        wire_a.erase(wire_a.begin(), wire_a.begin()+16);
        wire_b.erase(wire_b.begin(), wire_b.begin()+16);
        for (int type = 1; type < substituted; ++type) {
            auto& receiver_a = type % 2 ? a.r : a.i;
            auto& receiver_b = type % 2 ? b.r : b.i;
            BOOST_REQUIRE(receiver_a->Advance(wire_a, next_a));
            BOOST_REQUIRE(receiver_b->Advance(wire_b, next_b));
            if (type == 1) {
                next_a.erase(next_a.begin(), next_a.begin()+16);
                next_b.erase(next_b.begin(), next_b.begin()+16);
            }
            wire_a = std::move(next_a);
            wire_b = std::move(next_b);
        }
        BOOST_REQUIRE(wire_a != wire_b);
        auto& receiver = substituted % 2 ? b.r : b.i;
        BOOST_CHECK(!receiver->Advance(wire_a, next_b));
        BOOST_CHECK(!receiver->Ready());
        if (substituted == 3) BOOST_CHECK_EQUAL(receiver->Decapsulations(), 0U);
    }
}
BOOST_AUTO_TEST_CASE(wrong_network_rejected_before_randomness)
{
    Pair p;
    auto network = Array<85>(p.v["N"]);
    for (size_t offset = 0; offset < network.size(); ++offset) {
        unsigned draws{0};
        auto rng = [&](std::span<uint8_t>) { ++draws; return false; };
        Handshake responder(false, network, std::nullopt, p.idr, rng);
        auto record = p.Record(1);
        record[12+offset] ^= 1;
        Bytes response;
        BOOST_CHECK(!responder.Advance(record, response));
        BOOST_CHECK_EQUAL(draws, 0U);
        BOOST_CHECK_EQUAL(responder.Decapsulations(), 0U);
        BOOST_CHECK(!responder.Ready());
    }
}
BOOST_AUTO_TEST_CASE(packet_reference_and_multiple_rekeys)
{
    UniValue fixture; BOOST_REQUIRE(fixture.read(json_tests::pq_p2p_packets));
    auto h5 = Array<48>(fixture["h5"]);
    for (const auto& direction : fixture["directions"].getValues()) {
        auto len = V(direction["length_key"]), key = V(direction["payload_key"]); int role = direction["role"].getInt<int>();
        PacketCipher enc(len, key, h5, role), dec(len, key, h5, role);
        SHA3_384 all; unsigned selected{0};
        for (uint64_t i = 0; i < 674; ++i) {
            Bytes contents(9), wire, decoded{99}; contents[0] = 18; for (int j = 0; j < 8; ++j) contents[j+1] = i >> (8*j);
            bool ignore = i % 7 == 0;
            BOOST_REQUIRE(enc.Encrypt(contents, ignore ? 128 : 0, wire)); all.Write(wire);
            auto length = dec.DecryptLength(std::span{wire}.first(3)); BOOST_REQUIRE(length); BOOST_CHECK_EQUAL(*length, contents.size());
            bool decoy{false}; BOOST_REQUIRE(dec.Decrypt(std::span{wire}.subspan(3), decoded, decoy));
            BOOST_CHECK_EQUAL(decoy, ignore); BOOST_CHECK(decoded == (ignore ? Bytes{99} : contents));
            BOOST_CHECK_EQUAL(enc.Packets(), i+1); BOOST_CHECK_EQUAL(enc.Epoch(), (i+1)/224);
            const auto& row = direction["packets"][selected];
            if (i == row["index"].getInt<uint64_t>()) { BOOST_CHECK(wire == V(row["wire"])); if (selected+1 < direction["packets"].size()) ++selected; }
        }
        Digest digest; all.Finalize(digest); BOOST_CHECK(Equal(digest, V(direction["all_packets_sha3_384"])));
        for (const auto& invalid : direction["invalid_packets"].getValues()) {
            PacketCipher decoder(len, key, h5, role); auto wire = V(invalid["wire"]); Bytes sentinel{42}; bool ignore{false};
            BOOST_REQUIRE(decoder.DecryptLength(std::span{wire}.first(3)));
            BOOST_CHECK(!decoder.Decrypt(std::span{wire}.subspan(3), sentinel, ignore)); BOOST_CHECK(sentinel == Bytes{42});
        }
        PacketCipher oversized(len, key, h5, role); BOOST_CHECK(!oversized.DecryptLength(V(direction["oversized_wire_length"])));
        for (int error = 0; error < 7; ++error) {
            auto wire = V(direction["packets"][1]["wire"]); // Use packet zero below, so counters match.
            wire = V(direction["packets"][error == 6 ? 1 : 0]["wire"]);
            if (error < 3) wire[error == 0 ? 0 : error == 1 ? 3 : wire.size()-1] ^= 1;
            auto wrong_hash = h5; if (error == 3) wrong_hash[0] ^= 1;
            auto wrong_key=key; if (error==5) wrong_key[0]^=1;
            PacketCipher bad(len, wrong_key, wrong_hash, error == 4 ? role ^ 1 : role);
            Bytes sentinel{42}; bool ignore{false}; auto l = bad.DecryptLength(std::span{wire}.first(3));
            if (l) BOOST_CHECK(!bad.Decrypt(std::span{wire}.subspan(3), sentinel, ignore));
            BOOST_CHECK(sentinel == Bytes{42}); BOOST_CHECK(!bad.DecryptLength(std::span{wire}.first(3)));
        }
    }
}
BOOST_AUTO_TEST_SUITE_END()
