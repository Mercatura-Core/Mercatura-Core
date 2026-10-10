// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/rpc.h>

#include <curl/curl.h>
#include <pool/job.h>

#include <fstream>
#include <memory>
#include <stdexcept>

namespace pool {
namespace {
struct Reply {
    std::string bytes;
    bool oversized{false};
};
size_t Receive(char* bytes, size_t size, size_t count, void* context)
{
    auto& reply = *static_cast<Reply*>(context);
    if (size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    if (n > 16 * 1024 * 1024 - reply.bytes.size()) {
        reply.oversized = true;
        return 0;
    }
    try {
        reply.bytes.append(bytes, n);
        return n;
    } catch (...) {
        return 0;
    }
}
std::string Cookie(const std::string& path)
{
    std::ifstream file{path};
    std::string s;
    char c;
    if (!file) throw std::runtime_error("RPC cookie unavailable");
    while (file.get(c) && c != '\n') {
        if (s.size() >= 4096) throw std::runtime_error("RPC cookie invalid");
        s += c;
    }
    if (s.empty() || s.find(':') == std::string::npos) throw std::runtime_error("RPC cookie invalid");
    return s;
}
} // namespace
Rpc::Rpc(const Config& cfg) : m_cfg{cfg}
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("curl initialization failed");
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url{curl_url(), curl_url_cleanup};
    if (!url || curl_url_set(url.get(), CURLUPART_URL, cfg.rpc_url.c_str(), 0) != CURLUE_OK) throw std::invalid_argument("invalid RPC URL");
    auto part = [&](CURLUPart p) { char* s=nullptr; auto result=curl_url_get(url.get(),p,&s,0); std::string value=result==CURLUE_OK?s:""; curl_free(s); return value; };
    const auto scheme = part(CURLUPART_SCHEME), host = part(CURLUPART_HOST);
    if (!part(CURLUPART_USER).empty() || !part(CURLUPART_PASSWORD).empty()) throw std::invalid_argument("RPC credentials belong in authentication settings");
    if (scheme != "https" && !(scheme == "http" && (host == "127.0.0.1" || host == "localhost" || host == "[::1]"))) throw std::invalid_argument("remote RPC requires authenticated TLS");
}
UniValue Rpc::Call(const std::string& method, const UniValue& params)
{
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl{curl_easy_init(), curl_easy_cleanup};
    if (!curl) throw std::runtime_error("curl allocation failed");
    UniValue request{UniValue::VOBJ};
    request.pushKV("jsonrpc", "2.0");
    request.pushKV("id", 1);
    request.pushKV("method", method);
    request.pushKV("params", params);
    const auto body = request.write();
    auto auth = m_cfg.rpc_cookie.empty() ? m_cfg.rpc_user + ":" + m_cfg.rpc_password : Cookie(m_cfg.rpc_cookie);
    curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!headers) throw std::bad_alloc{};
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> guard{headers, curl_slist_free_all};
    Reply reply;
    curl_easy_setopt(curl.get(), CURLOPT_URL, m_cfg.rpc_url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl.get(), CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl.get(), CURLOPT_USERPWD, auth.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, Receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, static_cast<long>(m_cfg.rpc_connect_timeout_seconds));
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, static_cast<long>(m_cfg.rpc_timeout_seconds));
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    // Never follow redirects. Keep certificate verification and inherited
    // network/proxy policy enabled.
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    if (!m_cfg.rpc_ca.empty()) curl_easy_setopt(curl.get(), CURLOPT_CAINFO, m_cfg.rpc_ca.c_str());
    const auto result = curl_easy_perform(curl.get());
    if (result != CURLE_OK) throw std::runtime_error(reply.oversized ? "Core RPC response exceeds limit" : "Core RPC transport failed");
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    UniValue value;
    if (!value.read(reply.bytes) || !value.isObject()) throw std::runtime_error("invalid Core RPC response");
    if (!value["error"].isNull()) throw RpcError{value["error"]["code"].getInt<int>(), "Core RPC " + method + " failed"};
    if (status != 200 || value["id"].getInt<int>() != 1) throw std::runtime_error("Core RPC authentication/response failed");
    return value["result"];
}
void Rpc::CheckIdentity()
{
    auto info = Call("getblockchaininfo");
    UniValue p{UniValue::VARR};
    p.push_back(0);
    if (info["chain"].get_str() != m_cfg.network || Call("getblockhash", p).get_str() != m_cfg.genesis) throw std::runtime_error("Core network/genesis mismatch");
    if (m_cfg.network != "regtest" && info["initialblockdownload"].get_bool()) throw std::runtime_error("Core is in initial block download");
}
UniValue Rpc::Template()
{
    UniValue options{UniValue::VOBJ}, rules{UniValue::VARR}, p{UniValue::VARR};
    rules.push_back("segwit");
    options.pushKV("rules", rules);
    p.push_back(options);
    return Call("getblocktemplate", p);
}
std::string Rpc::ValidatePayout(const std::string& address)
{
    if (address.empty() || address.size() > 90) throw std::invalid_argument("invalid PQ address length");
    UniValue p{UniValue::VARR};
    p.push_back(address);
    auto validation = Call("validateaddress", p);
    if (!validation["isvalid"].get_bool()) throw std::invalid_argument("invalid PQ payout address for network");
    auto script = validation["scriptPubKey"].get_str();
    PqScript(script);
    return script;
}
} // namespace pool
