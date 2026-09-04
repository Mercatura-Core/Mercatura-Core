// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_HMAC_SHA3_384_H
#define BITCOIN_CRYPTO_HMAC_SHA3_384_H

#include <crypto/sha3.h>

#include <cstddef>
#include <span>

/** HMAC using SHA3-384. */
class CHMAC_SHA3_384
{
private:
    SHA3_384 m_outer;
    SHA3_384 m_inner;

public:
    static constexpr size_t OUTPUT_SIZE = SHA3_384::OUTPUT_SIZE;
    static constexpr size_t BLOCK_SIZE = 104;

    CHMAC_SHA3_384(const unsigned char* key, size_t keylen);

    CHMAC_SHA3_384& Write(std::span<const unsigned char> data)
    {
        m_inner.Write(data);
        return *this;
    }

    void Finalize(std::span<unsigned char> hash);
};

#endif // BITCOIN_CRYPTO_HMAC_SHA3_384_H