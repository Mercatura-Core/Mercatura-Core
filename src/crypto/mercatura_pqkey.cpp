#include <crypto/mercatura_pqkey.h>

#include <crypto/mercatura_mldsa.h>
#include <crypto/sha256.h>

#include <array>
#include <string_view>

bool ComputeMercaturaPQKeyCommitmentV1(
    MercaturaPQKeyCommitment& commitment_out,
    std::span<const unsigned char> public_key)
{
    if (public_key.size() != MERCATURA_MLDSA65_PUBLIC_KEY_SIZE) {
        return false;
    }

    static constexpr std::string_view TAG{
        "Mercatura/PQKeyCommitment/v1"
    };

    std::array<unsigned char, CSHA256::OUTPUT_SIZE> tag_hash{};

    CSHA256()
        .Write(
            reinterpret_cast<const unsigned char*>(TAG.data()),
            TAG.size())
        .Finalize(tag_hash.data());

    CSHA256()
        .Write(tag_hash.data(), tag_hash.size())
        .Write(tag_hash.data(), tag_hash.size())
        .Write(public_key.data(), public_key.size())
        .Finalize(commitment_out.data());

    return true;
}