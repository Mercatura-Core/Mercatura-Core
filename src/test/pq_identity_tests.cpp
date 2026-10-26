// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <net_pq_identity_format.h>
#include <boost/test/unit_test.hpp>
#include <new>
#include <stdexcept>

using namespace pq;
BOOST_AUTO_TEST_SUITE(pq_identity_tests)
BOOST_AUTO_TEST_CASE(identity_format_corruption_network_and_key_binding)
{
    Network network{}; network[0]=3;
    Secret<32> seed; seed.bytes[0]=42;
    Identity id; BOOST_REQUIRE(id.FromSeed(seed.bytes));
    IdentityFile file; EncodeIdentityFile(network,seed.bytes,id.pub,file);
    BOOST_CHECK_EQUAL(file.bytes.size(),2127U);
    BOOST_CHECK_EQUAL(std::string(file.bytes.begin(),file.bytes.begin()+8),"MCAPQID1");
    BOOST_CHECK(file.bytes[8]==1 && file.bytes[9]==0);
    auto parsed=ParseIdentityFile(file.bytes,network);
    BOOST_REQUIRE(parsed); BOOST_CHECK(parsed->pub==id.pub);
    // Every single-byte corruption is rejected. None of these public synthetic
    // seed values are used by a daemon; assertions never dump seed contents.
    for (size_t n=0;n<file.bytes.size();++n) {
        file.bytes[n]^=1;
        BOOST_CHECK_MESSAGE(!ParseIdentityFile(file.bytes,network),"accepted corruption at offset "<<n);
        file.bytes[n]^=1;
    }
    BOOST_CHECK(!ParseIdentityFile(std::span{file.bytes}.first(2126),network));
    Bytes oversized(file.bytes.begin(),file.bytes.end()); oversized.push_back(0);
    BOOST_CHECK(!ParseIdentityFile(oversized,network));
    auto wrong=network;
    for (size_t n:{size_t{0},size_t{1},size_t{5},size_t{37}}) {
        wrong[n]^=1; BOOST_CHECK(!ParseIdentityFile(file.bytes,wrong));
        BOOST_CHECK(!Equal(Fingerprint(wrong,id.pub),Fingerprint(network,id.pub)));
        wrong[n]^=1;
    }
    // A correctly recomputed checksum is not authority to change the key. The
    // seed must reconstruct the stored public key, and the selected N must match.
    IdentityPublic unrelated=id.pub; unrelated[17]^=1;
    EncodeIdentityFile(network,seed.bytes,unrelated,file);
    BOOST_CHECK(!ParseIdentityFile(file.bytes,network));
    seed.bytes[0]^=1; EncodeIdentityFile(network,seed.bytes,id.pub,file);
    BOOST_CHECK(!ParseIdentityFile(file.bytes,network));
    wrong[5]^=1; EncodeIdentityFile(wrong,seed.bytes,id.pub,file);
    BOOST_CHECK(!ParseIdentityFile(file.bytes,network));
    BOOST_CHECK(!id.FromSeed(std::span{seed.bytes}.first(31)));
    BOOST_CHECK(std::ranges::all_of(id.priv.bytes,[](auto b) { return b==0; }));
}
BOOST_AUTO_TEST_CASE(canonical_fingerprint_and_explicit_destination_bindings)
{
    const std::string fingerprint="mcapq1:"+std::string(96,'a');
    auto value=ParseFingerprint(fingerprint); BOOST_REQUIRE(value);
    BOOST_CHECK(std::ranges::all_of(*value,[](auto b) { return b==0xaa; }));
    for (auto invalid:{fingerprint+"\n",fingerprint.substr(0,102),"MCAPQ1:"+std::string(96,'a'),"mcapq1:"+std::string(96,'A'),"mcapq1:"+std::string(96,'g')}) {
        BOOST_CHECK(!ParseFingerprint(invalid));
    }
    for (const std::string& destination:std::vector<std::string>{"127.0.0.1:18444","localhost:18444","[2001:db8::1]:18444",std::string(56,'a')+".onion:18444"}) {
        auto parsed=ParsePinBinding(destination+"="+fingerprint);
        BOOST_REQUIRE(parsed); BOOST_CHECK_EQUAL(parsed->destination,destination);
        BOOST_CHECK(Equal(parsed->fingerprint,*value));
    }
    for (const std::string destination:{"localhost","localhost:","localhost:0","localhost:65536","localhost:-1","localhost:abc","2001:db8::1:18444","[::1:18444","host name:18444","host\n:18444",":18444"}) {
        BOOST_CHECK_MESSAGE(!ParsePinBinding(destination+"="+fingerprint),"accepted invalid binding "<<destination);
    }
    BOOST_CHECK(!ParsePinBinding("="+fingerprint));
    BOOST_CHECK(!ParsePinBinding("localhost:18444="+fingerprint+"="+fingerprint));
    Network network{}; Identity id; BOOST_REQUIRE(id.FromSeed(std::array<uint8_t,32>{}));
    auto a=Fingerprint(network,id.pub); network[0]=3;
    BOOST_CHECK(!Equal(a,Fingerprint(network,id.pub)));
}
BOOST_AUTO_TEST_CASE(secret_move_failure_and_destruction_cleanup)
{
    Secret<32> source; source.bytes.fill(42);
    Secret<32> moved(std::move(source));
    BOOST_CHECK(std::ranges::all_of(source.bytes,[](auto b) { return b==0; }));
    BOOST_CHECK(std::ranges::all_of(moved.bytes,[](auto b) { return b==42; }));
    source=std::move(moved);
    BOOST_CHECK(std::ranges::all_of(moved.bytes,[](auto b) { return b==0; }));
    alignas(Secret<32>) std::array<unsigned char,sizeof(Secret<32>)> storage{};
    auto* secret=new (storage.data()) Secret<32>;
    secret->bytes.fill(99); secret->~Secret();
    BOOST_CHECK(std::ranges::all_of(storage,[](auto b) { return b==0; }));
    auto failing=[](std::span<uint8_t> out) { std::fill(out.begin(),out.end(),42); return false; };
    Identity id; BOOST_REQUIRE(id.FromSeed(std::array<uint8_t,32>{}));
    BOOST_CHECK(!id.Generate(failing));
    BOOST_CHECK(std::ranges::all_of(id.priv.bytes,[](auto b) { return b==0; }));
    BOOST_CHECK(std::ranges::all_of(id.pub,[](auto b) { return b==0; }));
}
BOOST_AUTO_TEST_CASE(throwing_rng_clears_replaced_key_outputs)
{
    auto good = [](std::span<uint8_t> out) { std::fill(out.begin(), out.end(), 42); return true; };
    auto throwing = [](std::span<uint8_t> out) -> bool {
        std::fill(out.begin(), out.end(), 99);
        throw std::runtime_error("synthetic RNG failure");
    };
    Identity identity;
    BOOST_REQUIRE(identity.FromSeed(std::array<uint8_t,32>{}));
    BOOST_CHECK_THROW(identity.Generate(throwing), std::runtime_error);
    BOOST_CHECK(std::ranges::all_of(identity.priv.bytes, [](auto b) { return b == 0; }));
    BOOST_CHECK(std::ranges::all_of(identity.pub, [](auto b) { return b == 0; }));
    KEMKey kem;
    BOOST_REQUIRE(kem.Generate(good));
    BOOST_CHECK_THROW(kem.Generate(throwing), std::runtime_error);
    BOOST_CHECK(std::ranges::all_of(kem.priv.bytes, [](auto b) { return b == 0; }));
    BOOST_CHECK(std::ranges::all_of(kem.pub, [](auto b) { return b == 0; }));
    BOOST_REQUIRE(identity.FromSeed(std::array<uint8_t,32>{}));
    Signature signature; signature.fill(42);
    BOOST_CHECK_THROW(identity.Sign({}, signature, throwing), std::runtime_error);
    BOOST_CHECK(std::ranges::all_of(signature, [](auto b) { return b == 0; }));
    KEMCiphertext ciphertext; ciphertext.fill(42);
    Secret<32> shared; shared.bytes.fill(42);
    BOOST_CHECK_THROW(Encapsulate(kem.pub, ciphertext, shared, throwing), std::runtime_error);
    BOOST_CHECK(std::ranges::all_of(ciphertext, [](auto b) { return b == 0; }));
    BOOST_CHECK(std::ranges::all_of(shared.bytes, [](auto b) { return b == 0; }));
}
BOOST_AUTO_TEST_SUITE_END()
