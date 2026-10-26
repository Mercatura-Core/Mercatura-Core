// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <net_pq_protocol.h>
#include <net_pq_identity_format.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <cassert>
#include <cstdlib>

FUZZ_TARGET(pq_packet)
{
    FuzzedDataProvider data(buffer.data(), buffer.size());
    std::array<uint8_t,32> key{};
    pq::Digest h5{};
    pq::PacketCipher decoder(key,key,h5,data.ConsumeBool());
    pq::Bytes wire;
    if (data.ConsumeBool()) {
        pq::PacketCipher encoder(key,key,h5,data.ConsumeBool());
        const bool decoy=data.ConsumeBool();
        const bool mutate=data.ConsumeBool();
        const auto offset=data.ConsumeIntegral<size_t>();
        const auto mask=data.ConsumeIntegral<uint8_t>();
        auto contents=data.ConsumeBytes<uint8_t>(8192);
        if (!encoder.Encrypt(contents,decoy ? 128 : 0,wire)) return;
        if (!wire.empty() && mutate) wire[offset%wire.size()]^=mask;
    } else { wire = data.ConsumeRemainingBytes<uint8_t>(); }
    if (wire.size() < 3) return;
    auto length = decoder.DecryptLength(std::span{wire}.first(3));
    if (!length) return;
    pq::Bytes sentinel{42}; bool decoy{false};
    if (!decoder.Decrypt(std::span{wire}.subspan(3), sentinel, decoy) && sentinel != pq::Bytes{42}) std::abort();
}

FUZZ_TARGET(pq_handshake)
{
    FuzzedDataProvider data(buffer.data(), buffer.size());
    const auto step = data.ConsumeIntegralInRange<uint8_t>(1,5);
    // Public deterministic fuzz-only keys/coins. Never configurable in a node.
    static const auto identities = [] {
        std::array<std::shared_ptr<pq::Identity>,2> results;
        for (unsigned i=0;i<2;++i) {
            results[i]=std::make_shared<pq::Identity>(); std::array<uint8_t,32> seed{}; seed[0]=i;
            if (!results[i]->FromSeed(seed)) std::abort();
        }
        return results;
    }();
    auto rng = [](std::span<uint8_t> out) { std::fill(out.begin(),out.end(),42); return true; };
    pq::Network network{}; network[0]=3;
    pq::Handshake i(true,network,{},identities[0],rng), r(false,network,{},identities[1],rng);
    pq::Bytes wire, next; if (!i.Start(wire)) std::abort(); wire.erase(wire.begin(),wire.begin()+16);
    for (uint8_t s=1;s<=step;++s) {
        auto& receiver = s%2 ? r : i;
        if (s==step) {
            if (data.ConsumeBool() && !wire.empty()) {
                const auto offset=data.ConsumeIntegralInRange<size_t>(0,wire.size()-1);
                wire[offset]^=data.ConsumeIntegral<uint8_t>();
            } else { wire = data.ConsumeBytes<uint8_t>(4409); }
            receiver.Advance(wire,next); return;
        }
        if (!receiver.Advance(wire,next)) std::abort();
        wire=std::move(next); if (s==1) wire.erase(wire.begin(),wire.begin()+16);
    }
}

FUZZ_TARGET(pq_identity)
{
    FuzzedDataProvider data(buffer.data(),buffer.size());
    pq::Network network{}; network[0]=3;
    const bool structured=data.ConsumeBool();
    if (structured) {
        // Reach the checksum/key-consistency parser with public synthetic files,
        // rather than requiring a fuzzer to guess a SHA3 checksum from scratch.
        static const auto identity=[] {
            auto value=std::make_shared<pq::Identity>();
            if (!value->FromSeed(std::array<uint8_t,32>{})) std::abort();
            return value;
        }();
        pq::IdentityFile file;
        pq::EncodeIdentityFile(network,std::array<uint8_t,32>{},identity->pub,file);
        const auto edits=data.ConsumeIntegralInRange<unsigned>(0,8);
        for (unsigned n=0;n<edits;++n) {
            const auto offset=data.ConsumeIntegralInRange<size_t>(0,file.bytes.size()-1);
            file.bytes[offset]^=data.ConsumeIntegral<uint8_t>();
        }
        auto result=pq::ParseIdentityFile(file.bytes,network);
        if (result && result->pub!=identity->pub) std::abort();
    } else {
        const auto bytes=data.ConsumeRemainingBytes<uint8_t>();
        pq::ParseIdentityFile(bytes,network);
    }
    const std::string text(buffer.begin(),buffer.end());
    pq::ParseFingerprint(text);
    auto binding=pq::ParsePinBinding(text);
    if (binding && !pq::Equal(*pq::ParseFingerprint(text.substr(text.find('=')+1)),binding->fingerprint)) std::abort();
}

FUZZ_TARGET(pq_headers)
{
    FuzzedDataProvider data(buffer.data(),buffer.size());
    const auto expected=data.ConsumeIntegral<uint8_t>();
    auto header=data.ConsumeRemainingBytes<uint8_t>();
    const bool valid=pq::ValidHeader(header,expected);
    if (valid && (expected<1 || expected>5 || header!=pq::Header(expected))) std::abort();
}

FUZZ_TARGET(pq_command)
{
    auto contents=buffer;
    const auto before=contents.size();
    const auto command=pq::DecodeCommand(contents);
    if (command && (command->empty() || command->size()>12 || contents.size()>=before ||
                    !std::ranges::all_of(*command,[](unsigned char c) { return c>=32 && c<=127; }))) std::abort();
}

FUZZ_TARGET(pq_packet_stream)
{
    FuzzedDataProvider data(buffer.data(),buffer.size());
    std::array<uint8_t,32> key{}; pq::Digest h5{};
    const uint8_t role=data.ConsumeBool();
    pq::PacketCipher encoder(key,key,h5,role),decoder(key,key,h5,role);
    static constexpr std::array<unsigned,8> counts{1,2,223,224,225,448,672,674};
    const auto count=data.PickValueInArray(counts);
    auto contents=data.ConsumeBytes<uint8_t>(256);
    if (contents.empty()) contents.push_back(18);
    const auto mutate_at=data.ConsumeIntegralInRange<unsigned>(0,count);
    const auto mutation=data.ConsumeIntegral<uint8_t>();
    for (unsigned n=0;n<count;++n) {
        pq::Bytes wire,sentinel{42}; bool decoy{false};
        if (!encoder.Encrypt(contents,n%7==0 ? 128 : 0,wire)) std::abort();
        if (n==mutate_at && mutation) wire.back()^=mutation;
        auto length=decoder.DecryptLength(std::span{wire}.first(3));
        if (!length) std::abort();
        bool ok=decoder.Decrypt(std::span{wire}.subspan(3),sentinel,decoy);
        if (n==mutate_at && mutation) {
            if (ok || sentinel!=pq::Bytes{42} || decoder.DecryptLength(std::span{wire}.first(3))) std::abort();
            return;
        }
        if (!ok || decoy!=(n%7==0) || sentinel!=(decoy ? pq::Bytes{42} : contents) || decoder.Packets()!=n+1) std::abort();
    }
}
