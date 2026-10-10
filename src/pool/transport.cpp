// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <pool/transport.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace pool {
namespace {
using Clock = std::chrono::steady_clock;
struct Fd {
    int value{-1};
    ~Fd()
    {
        if (value >= 0) close(value);
    }
};
struct Connection {
    Fd socket;
    std::unique_ptr<SSL, decltype(&SSL_free)> ssl{nullptr, SSL_free};
    std::string ip, input, output;
    size_t offset{0};
    size_t tls_write_size{0};
    bool handshake{false}, dead{false};
    bool handshake_wants_write{false};
    bool read_wants_write{false}, write_wants_read{false};
    int64_t session{0};
    Clock::time_point created{Clock::now()}, last{created};
    int64_t second{-1};
    size_t messages{0};
    std::set<int64_t> outstanding;
};
struct IpRate {
    int64_t second{-1};
    size_t messages{0};
    size_t connections{0};
};
int64_t Second() { return std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count(); }
bool Rate(int64_t second, int64_t& stored, size_t& count, size_t limit)
{
    if (second != stored) {
        stored = second;
        count = 0;
    }
    return ++count <= limit;
}
void Nonblock(int fd)
{
    auto flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) throw std::runtime_error("cannot set nonblocking socket");
}
void Queue(Connection& c, const UniValue& response, size_t limit)
{
    std::string raw = response.write() + "\n";
    if (raw.size() > limit || c.output.size() - c.offset > limit - raw.size()) {
        c.dead = true;
        return;
    }
    if (c.offset) {
        c.output.erase(0, c.offset);
        c.offset = 0;
    }
    c.output += raw;
    if (response["id"].isNum()) c.outstanding.erase(response["id"].getInt<int64_t>());
}
} // namespace
size_t TlsWriteLength(size_t& pending, size_t available)
{
    if (!pending) pending = std::min<size_t>(16384, available);
    return pending;
}
void RunServer(Service& service, const std::atomic<bool>& stop)
{
    const Config& cfg = service.Settings();
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> tls{nullptr, SSL_CTX_free};
    if (!cfg.plaintext_regtest) {
        tls.reset(SSL_CTX_new(TLS_server_method()));
        if (!tls || SSL_CTX_set_min_proto_version(tls.get(), TLS1_2_VERSION) != 1) throw std::runtime_error("TLS context unavailable");
        if (SSL_CTX_use_certificate_chain_file(tls.get(), cfg.tls_certificate.c_str()) != 1 || SSL_CTX_use_PrivateKey_file(tls.get(), cfg.tls_key.c_str(), SSL_FILETYPE_PEM) != 1 || SSL_CTX_check_private_key(tls.get()) != 1) throw std::runtime_error("TLS certificate/key invalid");
        SSL_CTX_set_options(tls.get(), SSL_OP_NO_COMPRESSION);
        SSL_CTX_set_session_cache_mode(tls.get(), SSL_SESS_CACHE_OFF);
    }
    Fd listener{socket(AF_INET, SOCK_STREAM, 0)};
    if (listener.value < 0) throw std::runtime_error("listener socket unavailable");
    Nonblock(listener.value);
    int one = 1;
    setsockopt(listener.value, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(cfg.port);
    if (inet_pton(AF_INET, cfg.listen.c_str(), &address.sin_addr) != 1 || bind(listener.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(listener.value, 64) < 0) throw std::runtime_error("cannot bind pool IPv4 listener");
    std::map<int, std::unique_ptr<Connection>> clients;
    std::map<std::string, IpRate> ips;
    int64_t last_stats = Now();
    auto cleanup = [&] { for (auto it=clients.begin();it!=clients.end();) {
        if (it->second->dead) { service.Disconnect(it->second->session); auto ip=ips.find(it->second->ip); if (ip!=ips.end() && ip->second.connections) --ip->second.connections; it=clients.erase(it); } else ++it;
    } };
    try {
        while (!stop.load()) {
            if (cfg.stats_seconds && Now() - last_stats >= cfg.stats_seconds) {
                std::cout << service.Stats().write() << '\n';
                last_stats = Now();
            }
            for (auto& response : service.Tick())
                for (auto& [fd, c] : clients)
                    if (c->session == response.session) {
                        Queue(*c, response.message, cfg.max_output_bytes);
                        break;
                    }
            cleanup();
            std::vector<pollfd> pollfds{{listener.value, POLLIN, 0}};
            for (const auto& [fd, c] : clients)
                pollfds.push_back({fd, static_cast<short>(POLLIN | ((c->handshake_wants_write || c->offset < c->output.size() || c->read_wants_write) ? POLLOUT : 0)), 0});
            int result = poll(pollfds.data(), pollfds.size(), 50);
            if (result < 0 && errno != EINTR) throw std::runtime_error("pool poll failed");
            if (pollfds[0].revents & POLLIN) {
                // Bound accept work per reactor iteration.
                for (unsigned i = 0; i < 16; ++i) {
                    sockaddr_in peer{};
                    socklen_t length = sizeof(peer);
                    int fd = accept(listener.value, reinterpret_cast<sockaddr*>(&peer), &length);
                    if (fd < 0) break;
                    Fd owned{fd};
                    char text[INET_ADDRSTRLEN]{};
                    if (!inet_ntop(AF_INET, &peer.sin_addr, text, sizeof(text))) continue;
                    std::string ip{text};
                    // Recently closed IPs retain rate state briefly. Time-based
                    // pruning alone does not bound a distributed connection flood.
                    if (!ips.contains(ip) && ips.size() >= cfg.max_sessions * 4) continue;
                    auto& rate = ips[ip];
                    if (clients.size() >= cfg.max_sessions || rate.connections >= cfg.max_sessions_per_ip || !Rate(Second(), rate.second, rate.messages, cfg.messages_per_ip)) continue;
                    Nonblock(fd);
                    auto c = std::make_unique<Connection>();
                    c->socket.value = fd;
                    owned.value = -1;
                    c->ip = ip;
                    c->handshake = cfg.plaintext_regtest;
                    if (tls) {
                        c->ssl.reset(SSL_new(tls.get()));
                        if (!c->ssl || SSL_set_fd(c->ssl.get(), fd) != 1) throw std::runtime_error("TLS connection allocation failed");
                        SSL_set_accept_state(c->ssl.get());
                        SSL_set_mode(c->ssl.get(), SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
                    }
                    ++rate.connections;
                    clients.emplace(fd, std::move(c));
                }
            }
            for (size_t i = 1; i < pollfds.size(); ++i) {
                auto found = clients.find(pollfds[i].fd);
                if (found == clients.end()) continue;
                auto& c = *found->second;
                const auto events = pollfds[i].revents;
                if (events & (POLLERR | POLLHUP | POLLNVAL)) {
                    c.dead = true;
                    continue;
                }
                if (Clock::now() - c.last > std::chrono::seconds{cfg.heartbeat_seconds} || (!c.session && Clock::now() - c.created > std::chrono::seconds{cfg.handshake_seconds})) {
                    c.dead = true;
                    continue;
                }
                if (!events && !(c.ssl && SSL_pending(c.ssl.get())) && c.input.find('\n') == std::string::npos) continue;
                if (!c.handshake) {
                    int r = SSL_accept(c.ssl.get());
                    if (r == 1) {
                        c.handshake = true;
                        c.handshake_wants_write = false;
                    } else {
                        int e = SSL_get_error(c.ssl.get(), r);
                        c.handshake_wants_write = e == SSL_ERROR_WANT_WRITE;
                        if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) c.dead = true;
                        continue;
                    }
                }
                if (events & POLLIN || c.read_wants_write || (c.ssl && SSL_pending(c.ssl.get()))) {
                    std::array<char, 4096> bytes{};
                    int n = c.ssl ? SSL_read(c.ssl.get(), bytes.data(), bytes.size()) : static_cast<int>(recv(c.socket.value, bytes.data(), bytes.size(), 0));
                    c.read_wants_write = false;
                    if (n > 0) {
                        c.last = Clock::now();
                        c.input.append(bytes.data(), n);
                    } else if (c.ssl) {
                        int e = SSL_get_error(c.ssl.get(), n);
                        if (e == SSL_ERROR_WANT_WRITE)
                            c.read_wants_write = true;
                        else if (e != SSL_ERROR_WANT_READ)
                            c.dead = true;
                    } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                        c.dead = true;
                }
                if (!c.dead && c.handshake) {
                    // Each input buffer is at most max_message_bytes plus one
                    // fixed read buffer. Frames are validated before service work.
                    size_t processed = 0;
                    while (!c.dead && processed++ < 16) {
                        auto lf = c.input.find('\n');
                        if (lf == std::string::npos) break;
                        std::string raw = c.input.substr(0, lf);
                        c.input.erase(0, lf + 1);
                        int64_t id = 0;
                        try {
                            auto& rate = ips.at(c.ip);
                            const auto second = Second();
                            if (!Rate(second, c.second, c.messages, cfg.messages_per_session) || !Rate(second, rate.second, rate.messages, cfg.messages_per_ip)) {
                                Queue(c, Failure(0, RATE, "request rate exceeded"), cfg.max_output_bytes);
                                c.dead = true;
                                break;
                            }
                            auto request = ParseRequest(raw, cfg.max_message_bytes);
                            id = request.id;
                            if (c.outstanding.contains(id)) {
                                // A response with this ID would ambiguously
                                // consume the original pending request.
                                c.dead = true;
                                break;
                            }
                            auto response = service.Handle(c.session, c.ip, request);
                            if (response)
                                Queue(c, *response, cfg.max_output_bytes);
                            else
                                c.outstanding.insert(id);
                        } catch (const ProtocolError& e) {
                            Queue(c, Failure(id, e.code, e.what()), cfg.max_output_bytes);
                        } catch (const std::exception&) {
                            Queue(c, Failure(id, MALFORMED, "invalid message fields"), cfg.max_output_bytes);
                        }
                    }
                    if (c.input.size() > cfg.max_message_bytes) c.dead = true;
                }
                if (!c.dead && c.offset < c.output.size() && ((events & POLLOUT) || c.write_wants_read)) {
                    // OpenSSL requires identical bytes and length on retry,
                    // even if new responses append to the output meanwhile.
                    const auto length = c.ssl ? TlsWriteLength(c.tls_write_size, c.output.size() - c.offset) : std::min<size_t>(16384, c.output.size() - c.offset);
                    int n = c.ssl ? SSL_write(c.ssl.get(), c.output.data() + c.offset, static_cast<int>(length)) : static_cast<int>(send(c.socket.value, c.output.data() + c.offset, length, MSG_NOSIGNAL));
                    c.write_wants_read = false;
                    if (n > 0) {
                        c.tls_write_size = 0;
                        c.offset += n;
                        if (c.offset == c.output.size()) {
                            c.output.clear();
                            c.offset = 0;
                        }
                    } else if (c.ssl) {
                        int e = SSL_get_error(c.ssl.get(), n);
                        if (e == SSL_ERROR_WANT_READ)
                            c.write_wants_read = true;
                        else if (e != SSL_ERROR_WANT_WRITE)
                            c.dead = true;
                    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                        c.dead = true;
                }
            }
            for (auto it = ips.begin(); it != ips.end();) {
                if (!it->second.connections && Second() - it->second.second > 2)
                    it = ips.erase(it);
                else
                    ++it;
            }
            cleanup();
        }
    } catch (...) {
        for (const auto& [fd, c] : clients)
            service.Disconnect(c->session);
        service.Shutdown();
        throw;
    }
    for (const auto& [fd, c] : clients)
        service.Disconnect(c->session);
    service.Shutdown();
}
} // namespace pool
