#include "mercatura_mldsa.h"

#include <string.h>

#include <mldsa_native.h>

#if MLD_CONFIG_PARAMETER_SET != 65
#error "Mercatura ML-DSA wrapper requires ML-DSA-65"
#endif

#if MLDSA_SEEDBYTES != MERCATURA_MLDSA65_SEED_SIZE
#error "Unexpected ML-DSA seed size"
#endif

#if MLDSA_RNDBYTES != MERCATURA_MLDSA65_RANDOM_SIZE
#error "Unexpected ML-DSA randomness size"
#endif

#if MLDSA65_PUBLICKEYBYTES != MERCATURA_MLDSA65_PUBLIC_KEY_SIZE
#error "Unexpected ML-DSA-65 public key size"
#endif

#if MLDSA65_SECRETKEYBYTES != MERCATURA_MLDSA65_SECRET_KEY_SIZE
#error "Unexpected ML-DSA-65 secret key size"
#endif

#if MLDSA65_BYTES != MERCATURA_MLDSA65_SIGNATURE_SIZE
#error "Unexpected ML-DSA-65 signature size"
#endif

static int valid_buffer(const uint8_t *ptr, size_t len)
{
    return ptr != NULL || len == 0;
}

static int build_prefix(
    uint8_t prefix[257],
    size_t *prefix_len,
    const uint8_t *context,
    size_t context_len)
{
    if (prefix == NULL || prefix_len == NULL) {
        return 0;
    }

    if (!valid_buffer(context, context_len)) {
        return 0;
    }

    if (context_len > 255) {
        return 0;
    }

    /*
     * FIPS 204 pure ML-DSA message prefix:
     *
     *   0x00 || ctxlen || ctx
     *
     * This is only the FIPS 204 formatting used by this standalone wrapper.
     * Mercatura transaction domain separation is NOT defined here.
     */
    prefix[0] = 0x00;
    prefix[1] = (uint8_t)context_len;

    if (context_len != 0) {
        memcpy(prefix + 2, context, context_len);
    }

    *prefix_len = context_len + 2;
    return 1;
}

int mercatura_mldsa65_keypair_from_seed(
    uint8_t *public_key,
    size_t public_key_len,
    uint8_t *secret_key,
    size_t secret_key_len,
    const uint8_t *seed,
    size_t seed_len)
{
    if (public_key == NULL ||
        secret_key == NULL ||
        seed == NULL) {
        return 0;
    }

    if (public_key_len != MERCATURA_MLDSA65_PUBLIC_KEY_SIZE ||
        secret_key_len != MERCATURA_MLDSA65_SECRET_KEY_SIZE ||
        seed_len != MERCATURA_MLDSA65_SEED_SIZE) {
        return 0;
    }

    return PQCP_MLDSA_NATIVE_MLDSA65_keypair_internal(
        public_key,
        secret_key,
        seed) == 0;
}

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
    size_t secret_key_len)
{
    uint8_t prefix[257];
    size_t prefix_len;

    if (signature == NULL ||
        signing_randomness == NULL ||
        secret_key == NULL ||
        !valid_buffer(message, message_len)) {
        return 0;
    }

    if (signature_len != MERCATURA_MLDSA65_SIGNATURE_SIZE ||
        signing_randomness_len != MERCATURA_MLDSA65_RANDOM_SIZE ||
        secret_key_len != MERCATURA_MLDSA65_SECRET_KEY_SIZE) {
        return 0;
    }

    if (!build_prefix(prefix, &prefix_len, context, context_len)) {
        return 0;
    }

    return PQCP_MLDSA_NATIVE_MLDSA65_signature_internal(
        signature,
        message,
        message_len,
        prefix,
        prefix_len,
        signing_randomness,
        secret_key,
        0) == 0;
}

int mercatura_mldsa65_verify(
    const uint8_t *signature,
    size_t signature_len,
    const uint8_t *message,
    size_t message_len,
    const uint8_t *context,
    size_t context_len,
    const uint8_t *public_key,
    size_t public_key_len)
{
    uint8_t prefix[257];
    size_t prefix_len;

    if (signature == NULL ||
        public_key == NULL ||
        !valid_buffer(message, message_len)) {
        return 0;
    }

    if (signature_len != MERCATURA_MLDSA65_SIGNATURE_SIZE ||
        public_key_len != MERCATURA_MLDSA65_PUBLIC_KEY_SIZE) {
        return 0;
    }

    if (!build_prefix(prefix, &prefix_len, context, context_len)) {
        return 0;
    }

    return PQCP_MLDSA_NATIVE_MLDSA65_verify_internal(
        signature,
        message,
        message_len,
        prefix,
        prefix_len,
        public_key,
        0) == 0;
}
