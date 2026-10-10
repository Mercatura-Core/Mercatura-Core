// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_JOB_H
#define BITCOIN_POOL_JOB_H

#include <arith_uint256.h>
#include <pool/accounting.h>
#include <primitives/block.h>
#include <univalue.h>

#include <optional>
#include <string>

namespace pool {
template <typename T>
std::string SerializeHex(const T& object);
std::string BlockHex(const CBlock& block);
CBlock ParseBlock(const std::string& hex);
std::string HeaderHex(const CBlockHeader& header);
CScript PqScript(const std::string& hex);
arith_uint256 ParseTarget(const std::string& target);
arith_uint256 TemplateTarget(const UniValue& block_template);
CScript CoinbaseScript(int height, uint64_t session, uint64_t job);
bool HasPoolMarker(const CScript& script, int height);
struct Job {
    std::string id;
    std::string snapshot;
    int64_t serial{0};
    int64_t session{0};
    int64_t issued{0};
    int64_t expires{0};
    CBlock block;
    arith_uint256 share_target;
    arith_uint256 network_target;
    UniValue manifest;
    UniValue snapshot_manifest;
    UniValue Message() const;
};
size_t RequiredBlockBytes(const UniValue& block_template, size_t payout_count);
Job BuildJob(const UniValue& block_template, const Window& window, int64_t cutoff, const Number& window_limit,
             const std::string& network, const std::string& genesis, int64_t session, int64_t serial,
             const std::string& target, int64_t now, int64_t lifetime, bool warmup = false);
} // namespace pool
#endif // BITCOIN_POOL_JOB_H
