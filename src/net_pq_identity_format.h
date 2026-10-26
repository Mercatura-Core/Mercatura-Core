// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_NET_PQ_IDENTITY_FORMAT_H
#define BITCOIN_NET_PQ_IDENTITY_FORMAT_H

#include <net_pq_protocol.h>
#include <string>

namespace pq {
inline constexpr size_t IDENTITY_FILE_SIZE{2127};
using IdentityFile = Secret<IDENTITY_FILE_SIZE>;

/** Byte-exact experimental unencrypted seed format. No filesystem access.
 * The caller owns and cleanses the input/encoded seed buffer. A checksum is
 * corruption detection only; trusted ownership/permissions are checked by the
 * descriptor-based loader before this parser is called.
 */
void EncodeIdentityFile(const Network& network, std::span<const uint8_t,32> seed,
                        const IdentityPublic& pub, IdentityFile& contents);
std::shared_ptr<const Identity> ParseIdentityFile(std::span<const uint8_t> contents, const Network& network);

struct PinBinding {
    std::string destination;
    Digest fingerprint;
};
/** Exact destination spelling, explicit numeric port, full canonical fingerprint.
 * No name resolution, case folding, implicit default port, TOFU or alias trust.
 */
std::optional<Digest> ParseFingerprint(std::string_view text);
std::optional<PinBinding> ParsePinBinding(std::string_view text);
} // namespace pq
#endif // BITCOIN_NET_PQ_IDENTITY_FORMAT_H
