// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_CRYPTO_MERCATURA_P2P_CRYPTO_H
#define BITCOIN_CRYPTO_MERCATURA_P2P_CRYPTO_H

#include <support/cleanse.h>
#include <array>
#include <functional>
#include <span>
#include <cstdint>
#include <algorithm>

namespace pq {
template <size_t N> struct Secret {
    std::array<uint8_t, N> bytes{};
    Secret() = default;
    Secret(const Secret&) = delete;
    Secret& operator=(const Secret&) = delete;
    Secret(Secret&& other) noexcept : bytes(other.bytes) { other.Clear(); }
    Secret& operator=(Secret&& other) noexcept { if (this != &other) { bytes = other.bytes; other.Clear(); } return *this; }
    ~Secret() { Clear(); }
    void Clear() { memory_cleanse(bytes.data(), bytes.size()); }
};
using Random = std::function<bool(std::span<uint8_t>)>;
using Digest = std::array<uint8_t, 48>;
using KEMPublic = std::array<uint8_t, 1184>;
using KEMCiphertext = std::array<uint8_t, 1088>;
using IdentityPublic = std::array<uint8_t, 1952>;
using Signature = std::array<uint8_t, 3309>;
struct KEMKey {
    KEMPublic pub{};
    Secret<2400> priv;
    bool Generate(const Random& rng);
    bool Decapsulate(std::span<const uint8_t> ct, Secret<32>& ss) const;
};
bool Encapsulate(std::span<const uint8_t> pub, KEMCiphertext& ct, Secret<32>& ss, const Random& rng);
struct Identity {
    IdentityPublic pub{};
    Secret<4032> priv;
    bool FromSeed(std::span<const uint8_t> seed);
    bool Generate(const Random& rng);
    bool Sign(std::span<const uint8_t> msg, Signature& sig, const Random& rng) const;
};
bool Verify(std::span<const uint8_t> pub, std::span<const uint8_t> msg, std::span<const uint8_t> sig);
void Extract(std::span<const uint8_t> salt, std::span<const uint8_t> ikm, std::span<uint8_t,48> prk);
bool Expand(std::span<const uint8_t> prk, std::span<const uint8_t> info, std::span<uint8_t> out);
Digest MAC(std::span<const uint8_t> key, std::span<const uint8_t> msg);
bool Equal(std::span<const uint8_t> a, std::span<const uint8_t> b);
} // namespace pq
#endif
