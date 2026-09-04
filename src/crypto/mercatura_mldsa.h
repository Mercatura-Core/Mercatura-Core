#ifndef MERCATURA_MLDSA_H
#define MERCATURA_MLDSA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MERCATURA_MLDSA65_SEED_SIZE       32
#define MERCATURA_MLDSA65_RANDOM_SIZE     32
#define MERCATURA_MLDSA65_PUBLIC_KEY_SIZE 1952
#define MERCATURA_MLDSA65_SECRET_KEY_SIZE 4032
#define MERCATURA_MLDSA65_SIGNATURE_SIZE  3309

int mercatura_mldsa65_keypair_from_seed(
    uint8_t *public_key,
    size_t public_key_len,
    uint8_t *secret_key,
    size_t secret_key_len,
    const uint8_t *seed,
    size_t seed_len);

int mercatura_mldsa65_sign(
    uint8_t *signature,
    size_t signature_len,
    const uint8_t *message,
    size_t message_len,
    const uint8_t *context,
    size_t context_len,
    const uint8_t *signing_randomness,
    size_t signing_randomness_len,
    const uint8_t *secret_key,
    size_t secret_key_len);

int mercatura_mldsa65_verify(
    const uint8_t *signature,
    size_t signature_len,
    const uint8_t *message,
    size_t message_len,
    const uint8_t *context,
    size_t context_len,
    const uint8_t *public_key,
    size_t public_key_len);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
