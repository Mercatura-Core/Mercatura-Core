// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/system.h>
#include <compat/compat.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <util/sock.h>
#include <util/threadinterrupt.h>

#include <boost/test/unit_test.hpp>

#include <cassert>
#include <thread>

using namespace std::chrono_literals;

BOOST_FIXTURE_TEST_SUITE(sock_tests, BasicTestingSetup)

static bool SocketIsClosed(const SOCKET& s)
{
    // Notice that if another thread is running and creates its own socket after `s` has been
    // closed, it may be assigned the same file descriptor number. In this case, our test will
    // wrongly pretend that the socket is not closed.
    int type;
    socklen_t len = sizeof(type);
    return getsockopt(s, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &len) == SOCKET_ERROR;
}

static SOCKET CreateSocket()
{
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOST_REQUIRE(s != static_cast<SOCKET>(SOCKET_ERROR));
    return s;
}

BOOST_AUTO_TEST_CASE(constructor_and_destructor)
{
    const SOCKET s = CreateSocket();
    Sock* sock = new Sock(s);
    BOOST_CHECK(*sock == s);
    BOOST_CHECK(!SocketIsClosed(s));
    delete sock;
    BOOST_CHECK(SocketIsClosed(s));
}

BOOST_AUTO_TEST_CASE(move_constructor)
{
    const SOCKET s = CreateSocket();
    Sock* sock1 = new Sock(s);
    Sock* sock2 = new Sock(std::move(*sock1));
    delete sock1;
    BOOST_CHECK(!SocketIsClosed(s));
    BOOST_CHECK(*sock2 == s);
    delete sock2;
    BOOST_CHECK(SocketIsClosed(s));
}

BOOST_AUTO_TEST_CASE(move_assignment)
{
    const SOCKET s1 = CreateSocket();
    const SOCKET s2 = CreateSocket();
    Sock* sock1 = new Sock(s1);
    Sock* sock2 = new Sock(s2);

    BOOST_CHECK(!SocketIsClosed(s1));
    BOOST_CHECK(!SocketIsClosed(s2));

    *sock2 = std::move(*sock1);
    BOOST_CHECK(!SocketIsClosed(s1));
    BOOST_CHECK(SocketIsClosed(s2));
    BOOST_CHECK(*sock2 == s1);

    delete sock1;
    BOOST_CHECK(!SocketIsClosed(s1));
    BOOST_CHECK(SocketIsClosed(s2));
    BOOST_CHECK(*sock2 == s1);

    delete sock2;
    BOOST_CHECK(SocketIsClosed(s1));
    BOOST_CHECK(SocketIsClosed(s2));
}

#ifndef WIN32 // Windows does not have socketpair(2).

static void CreateSocketPair(int s[2])
{
    BOOST_REQUIRE_EQUAL(socketpair(AF_UNIX, SOCK_STREAM, 0, s), 0);
}

static void SendAndRecvMessage(const Sock& sender, const Sock& receiver)
{
    const char* msg = "abcd";
    constexpr ssize_t msg_len = 4;
    char recv_buf[10];

    BOOST_CHECK_EQUAL(sender.Send(msg, msg_len, 0), msg_len);
    BOOST_CHECK_EQUAL(receiver.Recv(recv_buf, sizeof(recv_buf), 0), msg_len);
    BOOST_CHECK_EQUAL(strncmp(msg, recv_buf, msg_len), 0);
}

BOOST_AUTO_TEST_CASE(send_and_receive)
{
    int s[2];
    CreateSocketPair(s);

    Sock* sock0 = new Sock(s[0]);
    Sock* sock1 = new Sock(s[1]);

    SendAndRecvMessage(*sock0, *sock1);

    Sock* sock0moved = new Sock(std::move(*sock0));
    Sock* sock1moved = new Sock(INVALID_SOCKET);
    *sock1moved = std::move(*sock1);

    delete sock0;
    delete sock1;

    SendAndRecvMessage(*sock1moved, *sock0moved);

    delete sock0moved;
    delete sock1moved;

    BOOST_CHECK(SocketIsClosed(s[0]));
    BOOST_CHECK(SocketIsClosed(s[1]));
}

BOOST_AUTO_TEST_CASE(wait)
{
    int s[2];
    CreateSocketPair(s);

    Sock sock0(s[0]);
    Sock sock1(s[1]);

    std::thread waiter([&sock0]() { (void)sock0.Wait(24h, Sock::RECV); });

    BOOST_REQUIRE_EQUAL(sock1.Send("a", 1, 0), 1);

    waiter.join();
}

BOOST_AUTO_TEST_CASE(recv_until_terminator_limit)
{
    constexpr auto timeout = 1min; // High enough so that it is never hit.
    CThreadInterrupt interrupt;
    int s[2];
    CreateSocketPair(s);

    Sock sock_send(s[0]);
    Sock sock_recv(s[1]);

    std::thread receiver([&sock_recv, &timeout, &interrupt]() {
        constexpr size_t max_data{10};
        bool threw_as_expected{false};
        // BOOST_CHECK_EXCEPTION() writes to some variables shared with the main thread which
        // creates a data race. So mimic it manually.
        try {
            (void)sock_recv.RecvUntilTerminator('\n', timeout, interrupt, max_data);
        } catch (const std::runtime_error& e) {
            threw_as_expected = HasReason("too many bytes without a terminator")(e);
        }
        assert(threw_as_expected);
    });

    BOOST_REQUIRE_NO_THROW(sock_send.SendComplete("1234567", timeout, interrupt));
    BOOST_REQUIRE_NO_THROW(sock_send.SendComplete("89a\n", timeout, interrupt));

    receiver.join();
}

#endif /* WIN32 */

// Real TCP sockets are required for urgent data; socketpair() is insufficient
// on several platforms. All traffic is bounded and stays on loopback.
static std::pair<std::unique_ptr<Sock>, std::unique_ptr<Sock>> CreateTCPPair()
{
    Sock listener{CreateSocket()};
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    BOOST_REQUIRE_EQUAL(listener.Bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    BOOST_REQUIRE_EQUAL(listener.Listen(1), 0);
    socklen_t addr_len{sizeof(addr)};
    BOOST_REQUIRE_EQUAL(listener.GetSockName(reinterpret_cast<sockaddr*>(&addr), &addr_len), 0);
    auto client{std::make_unique<Sock>(CreateSocket())};
    BOOST_REQUIRE_EQUAL(client->Connect(reinterpret_cast<sockaddr*>(&addr), addr_len), 0);
    auto server{listener.Accept(nullptr, nullptr)};
    BOOST_REQUIRE(server);
    BOOST_REQUIRE(client->SetNonBlocking());
    BOOST_REQUIRE(server->SetNonBlocking());
    return {std::move(client), std::move(server)};
}

BOOST_AUTO_TEST_CASE(wait_backpressure_urgent_data)
{
    auto [sender, receiver]{CreateTCPPair()};
    BOOST_REQUIRE_EQUAL(sender->Send("!", 1, MSG_OOB), 1);

    Sock::Event occurred{};
    BOOST_REQUIRE(receiver->Wait(1s, Sock::RECV, &occurred));
#if !defined(USE_POLL) || defined(MERCATURA_TEST_SELECT_BACKEND)
    // select must retain exception monitoring when receiving is enabled.
    BOOST_CHECK(occurred & Sock::ERR);
#endif
    // Pending urgent data must never enable reads when receive is paused.
    // Repeated masks model bounded adversarial readiness/backpressure changes.
    const std::shared_ptr<const Sock> receiver_ref{std::shared_ptr<const Sock>{}, receiver.get()};
    const std::shared_ptr<const Sock> sender_ref{std::shared_ptr<const Sock>{}, sender.get()};
    for (unsigned i{0}; i < 128; ++i) {
        const Sock::Event requested{static_cast<Sock::Event>((i & 1) ? Sock::SEND : 0)};
        // Windows select requires at least one socket in a nonempty set.
        Sock::EventsPerSock events{{receiver_ref, Sock::Events{requested}}, {sender_ref, Sock::Events{Sock::SEND}}};
        BOOST_REQUIRE(receiver->WaitMany(0ms, events));
        occurred = events.at(receiver_ref).occurred;
        BOOST_CHECK_EQUAL(occurred & (Sock::RECV | Sock::ERR), 0);
        if (requested & Sock::SEND) BOOST_CHECK(occurred & Sock::SEND);
    }
    char urgent{};
    BOOST_REQUIRE_EQUAL(receiver->Recv(&urgent, 1, MSG_OOB), 1);
    BOOST_CHECK_EQUAL(urgent, '!');
    BOOST_REQUIRE_EQUAL(sender->Send("a", 1, 0), 1);
    BOOST_REQUIRE(receiver->Wait(1s, Sock::RECV, &occurred));
    BOOST_CHECK(occurred & Sock::RECV);
    char byte{};
    BOOST_REQUIRE_EQUAL(receiver->Recv(&byte, 1, 0), 1);
    BOOST_CHECK_EQUAL(byte, 'a');
}

BOOST_AUTO_TEST_CASE(wait_connection_error)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Sock unused{CreateSocket()};
    BOOST_REQUIRE_EQUAL(unused.Bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    socklen_t addr_len{sizeof(addr)};
    BOOST_REQUIRE_EQUAL(unused.GetSockName(reinterpret_cast<sockaddr*>(&addr), &addr_len), 0);
    // Keep the port reserved but unlistened throughout the connection attempt.
    Sock client{CreateSocket()};
    BOOST_REQUIRE(client.SetNonBlocking());
    BOOST_CHECK_EQUAL(client.Connect(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SOCKET_ERROR);
    Sock::Event occurred{};
    BOOST_REQUIRE(client.Wait(1s, Sock::RECV | Sock::SEND, &occurred));
    BOOST_CHECK(occurred & (Sock::RECV | Sock::SEND | Sock::ERR));
    int error{};
    socklen_t len{sizeof(error)};
    BOOST_REQUIRE_EQUAL(client.GetSockOpt(SOL_SOCKET, SO_ERROR, &error, &len), 0);
    BOOST_CHECK_NE(error, 0);
}

BOOST_AUTO_TEST_SUITE_END()
