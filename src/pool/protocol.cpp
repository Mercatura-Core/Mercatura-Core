// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/protocol.h>

#include <algorithm>
#include <set>

namespace pool {
void Fields(const UniValue& object, const std::vector<std::string>& required, const std::vector<std::string>& optional)
{
    if (!object.isObject()) throw ProtocolError{MALFORMED, "object required"};
    std::set<std::string> seen;
    for (const auto& key : object.getKeys()) {
        if (!seen.insert(key).second || (std::find(required.begin(), required.end(), key) == required.end() && std::find(optional.begin(), optional.end(), key) == optional.end())) throw ProtocolError{MALFORMED, "unknown or duplicate field"};
    }
    for (const auto& key : required)
        if (!seen.contains(key)) throw ProtocolError{MALFORMED, "missing required field"};
}
Request ParseRequest(const std::string& raw, size_t max_bytes)
{
    if (raw.empty() || raw.size() > max_bytes) throw ProtocolError{MALFORMED, "message size invalid"};
    int depth = 0;
    bool quoted = false, escaped = false;
    for (unsigned char c : raw) {
        if (c == 0) throw ProtocolError{MALFORMED, "NUL in message"};
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 8) throw ProtocolError{MALFORMED, "excessive JSON depth"};
        } else if (c == '}' || c == ']') {
            if (--depth < 0) throw ProtocolError{MALFORMED, "invalid JSON"};
        }
    }
    UniValue json;
    if (depth != 0 || quoted || !json.read(raw)) throw ProtocolError{MALFORMED, "invalid JSON"};
    Fields(json, {"id", "method", "params"});
    Request request{json["id"].getInt<int64_t>(), json["method"].get_str(), json["params"]};
    if (request.id <= 0 || request.id > INT32_MAX || request.method.size() > 32 || !request.params.isObject()) throw ProtocolError{MALFORMED, "request fields invalid"};
    return request;
}
uint32_t Uint32(const UniValue& value)
{
    try {
        return value.getInt<uint32_t>();
    } catch (const std::exception&) {
        throw ProtocolError{MALFORMED, "unsigned 32-bit integer required"};
    }
}
UniValue Success(int64_t id, UniValue result)
{
    UniValue o{UniValue::VOBJ};
    o.pushKV("id", id);
    o.pushKV("result", std::move(result));
    o.pushKV("error", UniValue{});
    return o;
}
UniValue Failure(int64_t id, int code, const std::string& message)
{
    UniValue e{UniValue::VOBJ};
    e.pushKV("code", code);
    e.pushKV("message", message);
    UniValue o{UniValue::VOBJ};
    o.pushKV("id", id);
    o.pushKV("result", UniValue{});
    o.pushKV("error", std::move(e));
    return o;
}
UniValue Notify(const std::string& method, UniValue params)
{
    UniValue o{UniValue::VOBJ};
    o.pushKV("method", method);
    o.pushKV("params", std::move(params));
    return o;
}
} // namespace pool
