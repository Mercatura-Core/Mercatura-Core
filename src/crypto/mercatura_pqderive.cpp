// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/mercatura_pqderive.h>

#include <crypto/common.h>
#include <crypto/hkdf_sha3_384.h>

#include <array>
#include <string_view>
#include <vector>

std::optional<std::array<unsigned char, MERCATURA_PQ_CHILD_SEED_SIZE>>
DeriveMercaturaPQChildSeed(
    std::span<const unsigned char, MERCATURA_PQ_MASTER_SEED_SIZE> master_seed,
    const uint256& genesis_hash,
    uint32_t account,
    uint8_t branch,
    uint32_t index)
{
    if (branch > 1) {
        return std::nullopt;
    }

    static constexpr std::string_view SALT{
        "Mercatura/PQWalletMaster/v1"
    };

    static constexpr std::string_view INFO_TAG{
        "Mercatura/MLDSA65Key/v1"
    };

    const std::span<const unsigned char> salt{
        reinterpret_cast<const unsigned char*>(SALT.data()),
        SALT.size()
    };

    CHKDF_HMAC_SHA3_384_L32 hkdf{
        master_seed,
        salt
    };

    std::vector<unsigned char> info;
    info.reserve(
        INFO_TAG.size() +
        1 +
        uint256::size() +
        4 +
        1 +
        4);

    info.insert(
        info.end(),
        reinterpret_cast<const unsigned char*>(INFO_TAG.data()),
        reinterpret_cast<const unsigned char*>(INFO_TAG.data()) +
            INFO_TAG.size());

    // Mandatory domain separator.
    info.push_back(0x00);

    // Raw canonical uint256 serialization bytes.
    info.insert(
        info.end(),
        genesis_hash.begin(),
        genesis_hash.end());

    std::array<unsigned char, 4> account_le{};
    WriteLE32(account_le.data(), account);
    info.insert(
        info.end(),
        account_le.begin(),
        account_le.end());

    info.push_back(branch);

    std::array<unsigned char, 4> index_le{};
    WriteLE32(index_le.data(), index);
    info.insert(
        info.end(),
        index_le.begin(),
        index_le.end());

    std::array<unsigned char, MERCATURA_PQ_CHILD_SEED_SIZE> child_seed{};
    hkdf.Expand32(info, child_seed);

    return child_seed;
}