#ifndef BITCOIN_CRYPTO_MERCATURA_PQKEY_H
#define BITCOIN_CRYPTO_MERCATURA_PQKEY_H

#include <array>
#include <cstddef>
#include <span>

static constexpr size_t MERCATURA_PQ_KEY_COMMITMENT_SIZE = 32;

using MercaturaPQKeyCommitment =
    std::array<unsigned char, MERCATURA_PQ_KEY_COMMITMENT_SIZE>;

bool ComputeMercaturaPQKeyCommitmentV1(
    MercaturaPQKeyCommitment& commitment_out,
    std::span<const unsigned char> public_key);

#endif // BITCOIN_CRYPTO_MERCATURA_PQKEY_H