// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_MERCATURA_PQWALLET_H
#define BITCOIN_WALLET_MERCATURA_PQWALLET_H

#include <crypto/mercatura_pqkey.h>

#include <serialize.h>

#include <array>
#include <cstdint>
#include <vector>

namespace wallet {

struct MercaturaPQWalletState
{
    static constexpr uint32_t SCHEME_VERSION = 1;
    static constexpr uint32_t DERIVATION_VERSION = 1;

    uint32_t scheme_version{SCHEME_VERSION};
    uint32_t derivation_version{DERIVATION_VERSION};
    uint32_t account{0};
    uint32_t next_external_index{0};
    uint32_t next_internal_index{0};

    SERIALIZE_METHODS(MercaturaPQWalletState, obj)
    {
        READWRITE(
            obj.scheme_version,
            obj.derivation_version,
            obj.account,
            obj.next_external_index,
            obj.next_internal_index);
    }

    bool IsSupported() const
    {
        return scheme_version == SCHEME_VERSION &&
               derivation_version == DERIVATION_VERSION;
    }

    friend bool operator==(
        const MercaturaPQWalletState& a,
        const MercaturaPQWalletState& b)
    {
        return a.scheme_version == b.scheme_version &&
               a.derivation_version == b.derivation_version &&
               a.account == b.account &&
               a.next_external_index == b.next_external_index &&
               a.next_internal_index == b.next_internal_index;
    }
};

static constexpr size_t MERCATURA_PQ_WALLET_MASTER_SEED_SIZE = 32;

/**
 * Public derivation metadata for one Mercatura PQ destination.
 *
 * No secret material is stored here. The authoritative secret remains
 * the wallet's 32-byte PQ master seed.
 *
 * branch:
 *   0 = external / receive
 *   1 = internal / change
 */
struct MercaturaPQKeyLocator
{
    uint32_t account{0};
    uint8_t branch{0};
    uint32_t index{0};

    SERIALIZE_METHODS(MercaturaPQKeyLocator, obj)
    {
        READWRITE(
            obj.account,
            obj.branch,
            obj.index);
    }

    bool IsStructurallyValid() const
    {
        return branch <= 1;
    }

    bool operator==(
        const MercaturaPQKeyLocator&) const = default;
};

struct MercaturaPQCryptedSeed
{
    std::vector<unsigned char> ciphertext;
    std::array<unsigned char, 32> seed_check{};

    SERIALIZE_METHODS(MercaturaPQCryptedSeed, obj)
    {
        READWRITE(obj.ciphertext, obj.seed_check);
    }

    bool IsStructurallyValid() const
    {
        return !ciphertext.empty();
    }
};

} // namespace wallet

#endif // BITCOIN_WALLET_MERCATURA_PQWALLET_H