// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/hkdf_sha3_384.h>

#include <array>
#include <algorithm>
#include <cassert>

CHKDF_HMAC_SHA3_384_L32::CHKDF_HMAC_SHA3_384_L32(
    std::span<const unsigned char> ikm,
    std::span<const unsigned char> salt)
{
    CHMAC_SHA3_384(salt.data(), salt.size())
        .Write(ikm)
        .Finalize(m_prk);
}

void CHKDF_HMAC_SHA3_384_L32::Expand32(
    std::span<const unsigned char> info,
    std::span<unsigned char> output) const
{
    assert(output.size() == OUTPUT_SIZE);

    constexpr std::array<unsigned char, 1> counter{0x01};

    std::array<unsigned char, CHMAC_SHA3_384::OUTPUT_SIZE> t1{};

    CHMAC_SHA3_384(m_prk.data(), m_prk.size())
        .Write(info)
        .Write(counter)
        .Finalize(t1);

    std::copy_n(t1.begin(), OUTPUT_SIZE, output.begin());
}