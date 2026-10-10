// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_PROTOCOL_H
#define BITCOIN_POOL_PROTOCOL_H
#include <univalue.h>

#include <stdexcept>
#include <string>

namespace pool {
enum ErrorCode { MALFORMED = 100,
                 VERSION = 101,
                 IDENTITY = 102,
                 PAYOUT = 103,
                 SESSION = 104,
                 CAPACITY = 105,
                 EMPTY = 106,
                 STALE = 107,
                 DUPLICATE = 108,
                 LOW_WORK = 109,
                 OVERLOAD = 110,
                 RATE = 111,
                 INTERNAL = 112,
                 CANCELLED = 113 };
class ProtocolError : public std::runtime_error
{
public:
    int code;
    ProtocolError(int c, const std::string& message) : std::runtime_error{message}, code{c} {}
};
struct Request {
    int64_t id;
    std::string method;
    UniValue params;
};
Request ParseRequest(const std::string& raw, size_t max_bytes);
uint32_t Uint32(const UniValue& value);
void Fields(const UniValue& object, const std::vector<std::string>& required, const std::vector<std::string>& optional = {});
UniValue Success(int64_t id, UniValue result);
UniValue Failure(int64_t id, int code, const std::string& message);
UniValue Notify(const std::string& method, UniValue params);
} // namespace pool
#endif // BITCOIN_POOL_PROTOCOL_H
