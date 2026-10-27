// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Test-only loopback bridge: Python's message injector -> authenticated PQ peer.
// Never installed or selected by a production node. No fallback path exists.
#include <chainparams.h>
#include <common/system.h>
#include <net_pq.h>
#include <random.h>
#include <util/chaintype.h>
#include <util/sock.h>
#include <util/translation.h>

#include <array>
#include <algorithm>
#include <charconv>
#include <csignal>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

const TranslateFn G_TRANSLATION_FUN{};

namespace {
volatile std::sig_atomic_t stopped{0};
void Stop(int) { stopped = 1; }
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
bool Retry()
{
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINTR || error == WSAEINPROGRESS;
}
std::unique_ptr<Sock> Socket()
{
    const SOCKET descriptor = static_cast<SOCKET>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    Require(descriptor != INVALID_SOCKET, "test socket creation failed");
    return std::make_unique<Sock>(descriptor);
}
// Exactly one message per direction can wait for the opposite transport.
struct Direction {
    Transport& source;
    Transport& target;
    Sock& socket;
    std::vector<uint8_t> input;
    std::optional<CSerializedNetMsg> pending;
    bool Read()
    {
        if (pending && target.SetMessageToSend(*pending)) pending.reset();
        if (source.ReceivedMessageComplete() && !pending) {
            bool rejected{false};
            auto message = source.GetReceivedMessage({}, rejected);
            Require(!rejected, "test injector sent an invalid envelope");
            pending.emplace();
            pending->m_type = std::move(message.m_type);
            pending->data.resize(message.m_recv.size());
            if (!pending->data.empty()) std::memcpy(pending->data.data(), message.m_recv.data(), pending->data.size());
        }
        if (pending || source.ReceivedMessageComplete()) return true;
        if (!input.empty()) {
            auto bytes = std::span<const uint8_t>{input};
            Require(source.ReceivedBytes(bytes), "test transport rejected input");
            const size_t consumed = input.size() - bytes.size();
            input.erase(input.begin(), input.begin() + consumed);
        }
        if (input.size() == 65536) return true;
        Sock::Event events{0};
        Require(socket.Wait(0ms, Sock::RECV, &events), "test read readiness failed");
        if (events & Sock::RECV) {
            std::array<uint8_t, 65536> buffer;
            const auto count = socket.Recv(buffer.data(), buffer.size() - input.size(), 0);
            if (count == 0 || (count < 0 && !Retry())) return false;
            if (count > 0) input.insert(input.end(), buffer.begin(), buffer.begin() + count);
        }
        return true;
    }
};
bool Write(Transport& transport, Sock& socket)
{
    const auto [bytes, more, type] = transport.GetBytesToSend(false);
    if (bytes.empty()) return true;
    const auto count = socket.Send(bytes.data(), std::min(bytes.size(), size_t{65536}), MSG_NOSIGNAL);
    if (count == 0 || (count < 0 && !Retry())) return false;
    if (count > 0) transport.MarkBytesSent(count);
    return true;
}
}

int main(int argc, char** argv)
{
    try {
        Require(argc == 3, "usage: pq-test-peer main|test|regtest OWNED_LOOPBACK_PORT");
        const std::string_view chain{argv[1]}, port_text{argv[2]};
        unsigned port{0};
        const auto parsed = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
        Require(parsed.ec == std::errc{} && parsed.ptr == port_text.data() + port_text.size() && port > 0 && port <= 65535, "invalid owned port");
        Require(chain == "main" || chain == "test" || chain == "regtest", "unsupported test chain");
        Require(SetupNetworking(), "test networking initialization failed");
        RandomInit();
        SelectParams(chain == "main" ? ChainType::MAIN : chain == "test" ? ChainType::TESTNET : ChainType::REGTEST);
        std::signal(SIGTERM, Stop);
#ifndef WIN32
        std::signal(SIGPIPE, SIG_IGN);
#endif
        auto listener = Socket();
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Require(listener->Bind(reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0, "test bind failed");
        Require(listener->Listen(1) == 0, "test listen failed");
        socklen_t size{sizeof(local)};
        Require(listener->GetSockName(reinterpret_cast<sockaddr*>(&local), &size) == 0, "test listener address failed");
        Require(listener->SetNonBlocking(), "test listener mode failed");
        std::cout << ntohs(local.sin_port) << std::endl;
        Sock::Event ready{0};
        Require(listener->Wait(30s, Sock::RECV, &ready) && (ready & Sock::RECV), "test injector did not connect");
        auto plain = listener->Accept(nullptr, nullptr);
        Require(bool(plain) && plain->SetNonBlocking(), "test accept failed");
        auto remote = Socket();
        local.sin_port = htons(static_cast<uint16_t>(port));
        Require(remote->Connect(reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0, "owned PQ node unavailable");
        Require(remote->SetNonBlocking(), "test peer mode failed");
        auto context = std::make_shared<pq::Context>(pq::SelectedNetwork());
        const CService destination{CNetAddr{in_addr{htonl(INADDR_LOOPBACK)}}, uint16_t(port)};
        auto encrypted = pq::MakePQTransport(context, true, destination, "", false);
        V1Transport envelope{0};
        Direction to_pq{envelope, *encrypted, *plain, {}, {}}, to_python{*encrypted, envelope, *remote, {}, {}};
        const auto deadline = pq::Clock::now() + 300s;
        while (!stopped && pq::Clock::now() < deadline) {
            if (!encrypted->Poll() || !Write(*encrypted, *remote) || !Write(envelope, *plain) || !to_pq.Read() || !to_python.Read()) break;
            std::this_thread::sleep_for(1ms);
        }
        encrypted->Disconnect();
        context->Stop();
        Require(stopped || pq::Clock::now() < deadline, "test peer deadline reached");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
