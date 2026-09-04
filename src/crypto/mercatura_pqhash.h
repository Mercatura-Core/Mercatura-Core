#ifndef BITCOIN_CRYPTO_MERCATURA_PQHASH_H
#define BITCOIN_CRYPTO_MERCATURA_PQHASH_H

#include <crypto/sha3.h>
#include <serialize.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

static constexpr size_t MERCATURA_PQHASH384_SIZE = 48;

using MercaturaPQHash384 =
    std::array<unsigned char, MERCATURA_PQHASH384_SIZE>;

class MercaturaPQHashWriter
{
private:
    SHA3_384 m_hasher;

public:
    explicit MercaturaPQHashWriter(std::string_view tag);

    void write(std::span<const std::byte> src);

    template <typename T>
    MercaturaPQHashWriter& operator<<(const T& obj)
    {
        ::Serialize(*this, obj);
        return *this;
    }

    MercaturaPQHash384 GetHash();
};

MercaturaPQHash384 PQH384(
    std::string_view tag,
    std::span<const unsigned char> data);

#endif // BITCOIN_CRYPTO_MERCATURA_PQHASH_H
