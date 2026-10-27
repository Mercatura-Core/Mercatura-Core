#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Bounded loopback PQ authentication failures preserve an unrelated live peer.

Reuse the transparent record relay: it knows no endpoint secrets. Three fresh
regtest nodes, one relay, finite cases, explicit ports and owned thread cleanup.
"""
import time

from feature_pq_transport_negative import OPTIONS, Relay, flip
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, p2p_port


class PQB3AssuranceTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [OPTIONS + ['-maxconnections=16']] * 3

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(2, 0)

    def healthy(self, session):
        outbound = self.nodes[2].getpeerinfo()
        inbound = [peer for peer in self.nodes[0].getpeerinfo() if peer.get('session_id') == session]
        return (len(outbound) == len(inbound) == 1 and
                all(peer['pq_encrypted'] and peer['pq_authenticated'] and
                    not peer['pq_identity_verified'] and peer['session_id'] == session
                    for peer in outbound + inbound))

    def no_test_peer(self):
        return not self.nodes[1].getpeerinfo() and len(self.nodes[0].getpeerinfo()) == 1

    def run_test(self):
        healthy_session = self.nodes[2].getpeerinfo()[0]['session_id']
        assert self.healthy(healthy_session)
        sessions = []
        for _ in range(2):
            with Relay(p2p_port(3), p2p_port(0)) as relay:
                self.nodes[1].addnode(f'127.0.0.1:{p2p_port(3)}', 'onetry')
                self.wait_until(lambda: len(self.nodes[1].getpeerinfo()) == 1 and
                                self.nodes[1].getpeerinfo()[0]['bytesrecv_per_msg'].get('pong', 0) >= 29)
                peer = self.nodes[1].getpeerinfo()[0]
                assert peer['pq_authenticated'] and peer['pq_encrypted']
                assert not peer['pq_identity_verified']
                sessions.append(peer['session_id'])
                assert_equal(sum(map(len, relay.frames.values())), 15488)
                captured = dict(relay.frames)
                assert self.healthy(healthy_session)
            self.wait_until(self.no_test_peer)
            time.sleep(1.05)  # Honor the unchanged per-source token bucket.
        assert sessions[0] != sessions[1]

        cases = [
            ('invalid initiator signature', flip(3, 12 + 1088), None, False),
            ('invalid responder confirmation', flip(4, 12 + 3309), None, False),
            ('invalid final confirmation', flip(5, 12), None, False),
            ('old session final confirmation', lambda step, wire: captured[5] if step == 5 else wire, None, False),
            ('invalid application packet', None, None, True),
            ('disconnect after initiator authentication', None, 3, False),
        ]
        for name, change, close_after, packet_tamper in cases:
            with self.nodes[0].assert_debug_log([], unexpected_msgs=['received: version', 'retrying with v1 transport protocol']):
                with Relay(p2p_port(3), p2p_port(0), change, close_after, packet_tamper) as relay:
                    self.nodes[1].addnode(f'127.0.0.1:{p2p_port(3)}', 'onetry')
                    self.wait_until(relay.done.is_set, timeout=10)
                    assert not relay.timed_out, name
                    assert not change or relay.mutated, name
                    assert not close_after or relay.reached_close, name
                    assert not packet_tamper or relay.packet_modified, name
                    self.wait_until(self.no_test_peer)
            assert self.healthy(healthy_session), name
            before = self.nodes[2].getpeerinfo()[0]['bytesrecv_per_msg'].get('pong', 0)
            self.nodes[2].ping()
            self.wait_until(lambda: self.nodes[2].getpeerinfo()[0]['bytesrecv_per_msg'].get('pong', 0) > before)
            self.log.info('PASS B3 containment: %s; sibling session and pong delivery preserved', name)
            time.sleep(1.05)
        self.disconnect_nodes(2, 0)
        assert_equal(self.nodes[2].getpeerinfo(), [])
        assert_equal(self.nodes[0].getpeerinfo(), [])
        self.log.info('PASS B3: 2 valid sessions, fresh reconnect, 6 contained failures, clean peer removal')


if __name__ == '__main__':
    PQB3AssuranceTest(__file__).main()
