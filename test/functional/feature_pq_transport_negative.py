#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Isolated real-node PQ tampering/replay, application gates and absolute deadlines.

The bounded transparent relay records only public handshake bytes; it has no
session or identity secrets and does not implement cryptographic endpoints.
"""
import select
import socket
import threading
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, p2p_port

PREFACE = b'Mercatura/P2P/PQ'
SIZES = [0, 3265, 4353, 4409, 3369, 60]
OPTIONS = ['-pqp2p=required', '-connect=0', '-dnsseed=0', '-fixedseeds=0', '-discover=0', '-natpmp=0']


class Relay:
    def __init__(self, port, target, change=None, close_after=None, packet_tamper=False):
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(('127.0.0.1', port))
        self.listener.listen(1)
        self.listener.settimeout(8)
        self.target = target
        self.change = change
        self.close_after = close_after
        self.packet_tamper = packet_tamper
        self.frames = {}
        self.sockets = []
        self.stop = threading.Event()
        self.done = threading.Event()
        self.ready = threading.Event()
        self.error = None
        self.timed_out = False
        self.reached_close = False
        self.mutated = False
        self.packet_modified = False
        self.thread = threading.Thread(target=self.run)

    @staticmethod
    def read(sock, size):
        data = bytearray()
        while len(data) < size:
            part = sock.recv(size-len(data))
            if not part:
                raise EOFError('peer closed')
            data.extend(part)
        return bytes(data)

    def run(self):
        try:
            client, _ = self.listener.accept()
            server = socket.create_connection(('127.0.0.1', self.target), timeout=8)
            self.sockets = [client, server]
            for sock in self.sockets:
                sock.settimeout(8)
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            for step in range(1, 6):
                sender, receiver = (client, server) if step % 2 else (server, client)
                frame = self.read(sender, SIZES[step] + (16 if step <= 2 else 0))
                self.frames[step] = frame
                forwarded = self.change(step, frame) if self.change else frame
                self.mutated |= forwarded != frame
                # Deliberate transport fragmentation; buffers remain fixed and bounded.
                for offset in range(0, len(forwarded), 7):
                    receiver.sendall(forwarded[offset:offset+7])
                if self.close_after == step:
                    self.reached_close = True
                    return
            self.ready.set()
            if self.packet_tamper:
                # The first 3 bytes are the encrypted length. Changing ciphertext
                # byte 0 after it preserves framing and must fail AEAD verification.
                packet = bytearray(self.read(client, 4))
                packet[3] ^= 1
                self.packet_modified = True
                server.sendall(packet)
            while not self.stop.is_set():
                readable, _, _ = select.select(self.sockets, [], [], 0.1)
                for sender in readable:
                    data = sender.recv(65536)
                    if not data:
                        return
                    receiver = server if sender is client else client
                    receiver.sendall(data)
        except socket.timeout:
            if not self.stop.is_set():
                self.timed_out = True
        except (EOFError, ConnectionError, OSError):
            pass  # Active peer rejection or explicit test shutdown.
        except BaseException as error:
            self.error = error
        finally:
            for sock in self.sockets + [self.listener]:
                sock.close()
            self.done.set()

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *args):
        self.stop.set()
        for sock in self.sockets:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self.listener.close()
        self.thread.join(10)
        assert not self.thread.is_alive(), 'bounded relay did not shut down'
        assert self.error is None, repr(self.error)


def flip(step, offset):
    def change(current, frame):
        if current != step:
            return frame
        data = bytearray(frame)
        data[offset] ^= 1
        return bytes(data)
    return change


class PQTransportNegativeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # Twelve total automatic slots leave one inbound slot (8 full relay,
        # 2 block relay and 1 feeler are reserved by the existing connman).
        self.extra_args = [OPTIONS + ['-maxconnections=12'], OPTIONS]

    def setup_network(self):
        self.setup_nodes()

    def no_peers(self):
        return not self.nodes[0].getpeerinfo() and not self.nodes[1].getpeerinfo()

    def ready_peers(self):
        peers = self.nodes[0].getpeerinfo() + self.nodes[1].getpeerinfo()
        return len(peers) == 2 and all(peer['pq_encrypted'] and peer['pq_authenticated'] and peer['bytesrecv_per_msg'].get('pong', 0) >= 29 for peer in peers)

    def connect_relay(self):
        self.nodes[1].addnode(f'127.0.0.1:{p2p_port(2)}', 'onetry')

    def run_test(self):
        # An explicit classical-v2 request is rejected by RPC validation when
        # the required-only listener does not advertise classical acceptance.
        assert_raises_rpc_error(-8, 'v2transport requested but not enabled', self.nodes[1].addnode, f'127.0.0.1:{p2p_port(2)}', 'onetry', True)
        with Relay(p2p_port(2), p2p_port(0)) as relay:
            self.connect_relay()
            self.wait_until(self.ready_peers)
            captured = dict(relay.frames)
            assert_equal(len(captured), 5)
            assert_equal(sum(map(len, captured.values())), 15488)
            established = self.nodes[0].getpeerinfo()[0]['session_id']
            with self.nodes[0].assert_debug_log(['inbound capacity reached; incomplete PQ handshake cannot evict existing peers'], timeout=5):
                with socket.create_connection(('127.0.0.1', p2p_port(0)), timeout=5) as extra:
                    assert_equal(extra.recv(1), b'')
            assert_equal(self.nodes[0].getpeerinfo()[0]['session_id'], established)
            assert self.ready_peers()
            self.log.info('PASS full inbound capacity: established PQ session preserved')
        self.wait_until(self.no_peers)

        cases = [(name, flip(step, offset), None, False) for name, step, offset in [
            ('preface', 1, 0), ('role', 1, 17), ('version', 1, 18),
            ('suite', 1, 20), ('flags', 1, 22), ('length', 1, 24),
            ('network', 1, 28), ('M2 network', 2, 28),
            ('KEM ciphertext R', 2, 16+12+1301),
            ('KEM ciphertext I', 3, 12), ('signature I', 3, 12+1088),
            ('signature R', 4, 12), ('confirmation R', 4, 12+3309),
            ('confirmation I', 5, 12),
        ]]
        cases += [
            ('recorded M3 replay', lambda step, frame: captured[3] if step == 3 else frame, None, False),
            ('role reflection', lambda step, frame: captured[2] if step == 1 else frame, None, False),
            ('duplicate M1 instead of M3', lambda step, frame: captured[1][16:] if step == 3 else frame, None, False),
            ('identity substitution', lambda step, frame: frame[:-1952] + captured[1][-1952:] if step == 1 else frame, None, False),
            ('application AEAD ciphertext', None, None, True),
        ]
        cases += [(f'disconnect after M{step}', None, step, False) for step in range(1,6)]
        for name, change, close_after, packet_tamper in cases:
            time.sleep(1.05)  # Keep authentication tests inside the unchanged source bucket policy.
            with self.nodes[0].assert_debug_log([], unexpected_msgs=['received: version', 'retrying with v1 transport protocol']):
                with Relay(p2p_port(2), p2p_port(0), change, close_after, packet_tamper) as relay:
                    self.connect_relay()
                    self.wait_until(relay.done.is_set, timeout=10)
                    assert not relay.timed_out, f'{name}: proxy timeout must not masquerade as rejection'
                    if change:
                        assert relay.mutated, f'{name}: intended mutation was not reached'
                    if packet_tamper:
                        assert relay.packet_modified, 'application ciphertext tampering was not reached'
                    if close_after:
                        assert relay.reached_close
                    self.wait_until(self.no_peers)
            self.log.info('PASS wire scenario: %s', name)

        for name, initial, delay, fragment, minimum, maximum in [
            ('absolute preface deadline', PREFACE[:1], 2.5, PREFACE[1:2], 4.5, 9),
            ('absolute record deadline', PREFACE, 25, b'\x01', 29, 36),
        ]:
            start = time.monotonic()
            with socket.create_connection(('127.0.0.1', p2p_port(0)), timeout=40) as peer:
                peer.sendall(initial)
                time.sleep(delay)
                peer.sendall(fragment)
                assert_equal(peer.recv(1), b'')
            elapsed = time.monotonic() - start
            assert minimum <= elapsed <= maximum, (name, elapsed)
            self.wait_until(self.no_peers)
            self.log.info('PASS %s: %.3f seconds despite a later fragment', name, elapsed)
        self.log.info('PASS: %d wire scenarios, 2 absolute deadlines; no unauthenticated version delivery or classical retry', len(cases))


if __name__ == '__main__':
    PQTransportNegativeTest(__file__).main()
