// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/hmac_sha3_384.h>
#include <support/cleanse.h>

#include <algorithm>
#include <array>
#include <cassert>

CHMAC_SHA3_384::CHMAC_SHA3_384(const unsigned char* key, size_t keylen)
{
    std::array<unsigned char, BLOCK_SIZE> key_block{};

    if (keylen > BLOCK_SIZE) {
        std::array<unsigned char, OUTPUT_SIZE> key_hash{};
        SHA3_384 key_hasher;
        key_hasher.Write(std::span<const unsigned char>{key, keylen}).Finalize(key_hash);
        memory_cleanse(&key_hasher, sizeof(key_hasher));

        std::copy(key_hash.begin(), key_hash.end(), key_block.begin());
        memory_cleanse(key_hash.data(), key_hash.size());
    } else if (keylen != 0) {
        std::copy(key, key + keylen, key_block.begin());
    }

    std::array<unsigned char, BLOCK_SIZE> inner_pad{};
    std::array<unsigned char, BLOCK_SIZE> outer_pad{};

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        inner_pad[i] = key_block[i] ^ 0x36;
        outer_pad[i] = key_block[i] ^ 0x5c;
    }

    m_inner.Write(inner_pad);
    m_outer.Write(outer_pad);
    memory_cleanse(key_block.data(), key_block.size());
    memory_cleanse(inner_pad.data(), inner_pad.size());
    memory_cleanse(outer_pad.data(), outer_pad.size());
}

CHMAC_SHA3_384::~CHMAC_SHA3_384() { memory_cleanse(this, sizeof(*this)); }

void CHMAC_SHA3_384::Finalize(std::span<unsigned char> hash)
{
    assert(hash.size() == OUTPUT_SIZE);

    std::array<unsigned char, OUTPUT_SIZE> inner_hash{};
    m_inner.Finalize(inner_hash);

    m_outer.Write(inner_hash);
    m_outer.Finalize(hash);
    memory_cleanse(inner_hash.data(), inner_hash.size());
}
