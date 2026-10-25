// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <crypto/mercatura_p2p_crypto.h>
#include <crypto/mercatura_mldsa.h>
#include <crypto/hmac_sha3_384.h>
#include <mlkem_native.h>
#include <string_view>

static_assert(MLKEM768_PUBLICKEYBYTES == 1184 && MLKEM768_SECRETKEYBYTES == 2400);
static_assert(MLKEM768_CIPHERTEXTBYTES == 1088 && MLKEM_BYTES == 32);
namespace pq {
namespace {
constexpr std::string_view CONTEXT{"Mercatura/P2PAuth/v1"};
}
bool KEMKey::Generate(const Random& rng)
{
    Secret<64> coins;
    if (rng(coins.bytes) && PQCP_MLKEM_NATIVE_MLKEM768_keypair_derand(pub.data(), priv.bytes.data(), coins.bytes.data()) == 0) return true;
    priv.Clear(); pub.fill(0); return false;
}
bool KEMKey::Decapsulate(std::span<const uint8_t> ct, Secret<32>& ss) const
{
    ss.Clear();
    if (ct.size() != 1088) return false;
    // Correct-length invalid ciphertexts intentionally use upstream implicit rejection.
    if (PQCP_MLKEM_NATIVE_MLKEM768_dec(ss.bytes.data(), ct.data(), priv.bytes.data()) == 0) return true;
    ss.Clear(); return false;
}
bool Encapsulate(std::span<const uint8_t> pub, KEMCiphertext& ct, Secret<32>& ss, const Random& rng)
{
    Secret<32> coins;
    ss.Clear(); ct.fill(0);
    if (pub.size() == 1184 && rng(coins.bytes) && PQCP_MLKEM_NATIVE_MLKEM768_enc_derand(ct.data(), ss.bytes.data(), pub.data(), coins.bytes.data()) == 0) return true;
    ss.Clear(); ct.fill(0); return false;
}
bool Identity::FromSeed(std::span<const uint8_t> seed)
{
    if (mercatura_mldsa65_keypair_from_seed(pub.data(), pub.size(), priv.bytes.data(), priv.bytes.size(), seed.data(), seed.size())) return true;
    priv.Clear(); pub.fill(0); return false;
}
bool Identity::Generate(const Random& rng)
{
    Secret<32> seed;
    if (rng(seed.bytes)) return FromSeed(seed.bytes);
    priv.Clear(); pub.fill(0); return false;
}
bool Identity::Sign(std::span<const uint8_t> msg, Signature& sig, const Random& rng) const
{
    Secret<32> rnd;
    if (rng(rnd.bytes) && mercatura_mldsa65_sign(sig.data(), sig.size(), msg.data(), msg.size(), reinterpret_cast<const uint8_t*>(CONTEXT.data()), CONTEXT.size(), rnd.bytes.data(), rnd.bytes.size(), priv.bytes.data(), priv.bytes.size())) return true;
    sig.fill(0); return false;
}
bool Verify(std::span<const uint8_t> pub, std::span<const uint8_t> msg, std::span<const uint8_t> sig)
{
    return mercatura_mldsa65_verify(sig.data(), sig.size(), msg.data(), msg.size(), reinterpret_cast<const uint8_t*>(CONTEXT.data()), CONTEXT.size(), pub.data(), pub.size());
}
Digest MAC(std::span<const uint8_t> key, std::span<const uint8_t> msg)
{
    Digest out;
    CHMAC_SHA3_384 state(key.data(), key.size());
    state.Write(msg).Finalize(out);
    memory_cleanse(&state, sizeof(state));
    return out;
}
Digest Extract(std::span<const uint8_t> salt, std::span<const uint8_t> ikm) { return MAC(salt, ikm); }
bool Expand(std::span<const uint8_t> prk, std::span<const uint8_t> info, std::span<uint8_t> out)
{
    if (prk.size() != 48 || out.size() > 255 * 48) return false;
    Secret<48> t;
    size_t pos{0};
    for (unsigned counter = 1; pos < out.size(); ++counter) {
        CHMAC_SHA3_384 state(prk.data(), prk.size());
        if (pos) state.Write(t.bytes);
        const uint8_t c = counter;
        state.Write(info).Write({&c, 1}).Finalize(t.bytes);
        memory_cleanse(&state, sizeof(state));
        const auto count = std::min(size_t{48}, out.size() - pos);
        std::copy_n(t.bytes.begin(), count, out.begin() + pos);
        pos += count;
    }
    return true;
}
bool Equal(std::span<const uint8_t> a, std::span<const uint8_t> b)
{
    if (a.size() != b.size()) return false;
    volatile uint8_t diff{0};
    for (size_t i = 0; i < a.size(); ++i) diff = diff | (a[i] ^ b[i]);
    return diff == 0;
}
} // namespace pq
