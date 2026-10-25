// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <net_pq_protocol.h>
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
