// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_HKDF_SHA3_384_H
#define BITCOIN_CRYPTO_HKDF_SHA3_384_H

#include <crypto/hmac_sha3_384.h>

#include <array>
#include <cstddef>
#include <span>

/**
 * RFC 5869 HKDF using HMAC-SHA3-384.
 *
 * Mercatura PQ wallet derivation uses:
 *   PRK size = 48 bytes
 *   OKM size = 32 bytes
 */
class CHKDF_HMAC_SHA3_384_L32
{
private:
    std::array<unsigned char, CHMAC_SHA3_384::OUTPUT_SIZE> m_prk{};

public:
    static constexpr size_t PRK_SIZE = CHMAC_SHA3_384::OUTPUT_SIZE;
    static constexpr size_t OUTPUT_SIZE = 32;

    CHKDF_HMAC_SHA3_384_L32(
        std::span<const unsigned char> ikm,
        std::span<const unsigned char> salt);

    void Expand32(
        std::span<const unsigned char> info,
        std::span<unsigned char> output) const;

    const std::array<unsigned char, PRK_SIZE>& GetPRK() const
    {
        return m_prk;
    }
};

#endif // BITCOIN_CRYPTO_HKDF_SHA3_384_H