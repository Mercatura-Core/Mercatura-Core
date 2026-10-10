// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_RPC_H
#define BITCOIN_POOL_RPC_H
#include <pool/config.h>
#include <univalue.h>

#include <stdexcept>

namespace pool {
class RpcError : public std::runtime_error
{
public:
    int code;
    explicit RpcError(int c, const std::string& msg) : std::runtime_error{msg}, code{c} {}
};
class Rpc
{
    Config m_cfg;

public:
    explicit Rpc(const Config& cfg);
    UniValue Call(const std::string& method, const UniValue& params = UniValue{UniValue::VARR});
    void CheckIdentity();
    UniValue Template();
    std::string ValidatePayout(const std::string& address);
};
} // namespace pool
#endif // BITCOIN_POOL_RPC_H
