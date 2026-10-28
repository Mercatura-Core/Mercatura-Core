#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Required policy, real connection origins, diagnostics and pins on owned nodes.

--public-testnet uses the unchanged public-testnet identity on loopback only.
It never mines public-testnet blocks or contacts a public endpoint.
"""
import json
import os
from pathlib import Path
import socket
import threading
import time

from feature_pq_transport import PQTransportTest
from test_framework.pq_lab_proxy import run_owned_test
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, p2p_port


class ClassicalEndpoint:
    """Bounded local legacy-only endpoint also detects an unwanted retry."""
    def __init__(self):
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(2)
        self.listener.settimeout(0.1)
        self.address = f'127.0.0.1:{self.listener.getsockname()[1]}'
        self.prefaces = []
        self.error = None
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run)

    def run(self):
        try:
            while not self.stop.is_set():
                try:
                    peer, _ = self.listener.accept()
                except socket.timeout:
                    continue
                with peer:
                    peer.settimeout(5)
                    preface = b''
                    while len(preface) < 16:
                        part = peer.recv(16-len(preface))
                        if not part:
                            break
                        preface += part
                    self.prefaces.append(preface)
                    peer.sendall(b'\xd7\xdb\xee\xa4version\x00\x00\x00\x00\x00')
                    try:
                        while peer.recv(4096):
                            pass
                    except ConnectionResetError:
                        pass
        except Exception as error:
            self.error = error

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join(6)
        self.listener.close()
        assert not self.thread.is_alive()
        if self.error:
            raise self.error


class PQRequiredTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument('--public-testnet', action='store_true')

    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.chain = 'testnet' if self.options.public_testnet else 'regtest'
        self.common = ['-dnsseed=0', '-fixedseeds=0', '-discover=0', '-natpmp=0', '-maxconnections=16']
        self.common += ['-pqp2p=required']
        self.extra_args = [self.common + ['-connect=0']] * self.num_nodes

    def setup_network(self):
        self.setup_nodes()

    def status(self, n=1):
        return self.nodes[n].getnetworkinfo()['pqp2p']

    def ready(self, n=1):
        return [p for p in self.nodes[n].getpeerinfo() if p['pq_encrypted']]

    def clear(self):
        for peer in self.nodes[1].getpeerinfo():
            self.nodes[1].disconnectnode(nodeid=peer['id'])
        self.wait_until(lambda: not self.nodes[1].getpeerinfo())
        time.sleep(1.1)  # Preserve the original admission token policy.

    def assert_ready(self, pinned=False):
        self.wait_until(lambda: bool(self.ready()))
        peer = self.ready()[0]
        assert_equal(peer['transport_protocol_type'], 'pq-v1')
        assert peer['pq_authenticated']
        assert_equal(peer['pq_identity_verified'], pinned)
        assert_equal(len(peer['session_id']), 64)
        return peer['session_id']

    def run_test(self):
        events = []
        for n in range(self.num_nodes):
            info = self.nodes[n].getnetworkinfo()
            assert self.status(n)['required'] and self.status(n)['listening']
            assert 'P2P_PQ_EXPERIMENTAL' in info['localservicesnames']
            assert 'P2P_V2' not in info['localservicesnames']
        self.connect_nodes(2, 0)
        healthy = self.ready(2)[0]['session_id']
        self.connect_nodes(1, 0)
        first = self.assert_ready()
        self.clear()
        self.connect_nodes(1, 0)
        second = self.assert_ready()
        assert second != first
        self.nodes[1].setnetworkactive(False)
        self.wait_until(lambda: not self.nodes[1].getpeerinfo())
        assert self.status()['required'] and not self.status()['listening']
        self.nodes[1].setnetworkactive(True)
        self.connect_nodes(1,0)
        assert self.assert_ready() != second
        assert_raises_rpc_error(-8, 'Required PQ forbids', self.nodes[1].addnode,
                                f'127.0.0.1:{p2p_port(0)}', 'onetry', True)
        before = self.status(0)['failures']['framing']
        for preface in (b'\xd7\xdb\xee\xa4version\x00', bytes(32)):
            with socket.create_connection(('127.0.0.1', p2p_port(0)), timeout=5) as sock:
                sock.settimeout(5)
                sock.sendall(preface)
                assert_equal(sock.recv(1), b'')
            time.sleep(1.1)
        self.wait_until(lambda: self.status(0)['failures']['framing'] >= before + 2)
        self.clear()
        with ClassicalEndpoint() as legacy:
            before = self.status()['failures']['framing']
            self.nodes[1].addnode(legacy.address, 'onetry', False)
            self.wait_until(lambda: self.status()['failures']['framing'] > before)
            self.wait_until(lambda: not self.nodes[1].getpeerinfo())
            time.sleep(2)
        assert_equal(legacy.prefaces, [b'Mercatura/P2P/PQ'])
        events.append({'outbound_classical_rejected': True, 'tcp_attempts': len(legacy.prefaces)})

        # Configuration-based origins go through the same central selector.
        for option in ('connect', 'addnode'):
            opts = self.common + [f'-{option}=127.0.0.1:{p2p_port(0)}']
            if option == 'addnode':
                opts += ['-connect=0']
            self.restart_node(1, opts)
            events.append({'origin': option, 'session_id': self.assert_ready()})
        self.restart_node(1, self.extra_args[1])

        # Pin bindings use the selected network datadir and discriminator.
        pin = None
        if os.name != 'nt':
            self.restart_node(0, self.extra_args[0] + ['-pqp2pidentity=operator.dat', '-pqp2pidentitycreate=1'])
            fingerprint = PQTransportTest.public_fingerprint(self, self.nodes[0], 'operator.dat')
            self.connect_nodes(2, 0)
            healthy = self.ready(2)[0]['session_id']
            destination = f'127.0.0.1:{p2p_port(0)}'
            bad = '-pqp2ppin=' + destination + '=mcapq1:' + '00' * 48
            pin = '-pqp2ppin=' + destination + '=' + fingerprint
            self.restart_node(1, self.extra_args[1] + [bad])
            origins = ['manual'] if self.options.public_testnet else ['manual', 'outbound-full-relay', 'block-relay-only', 'addr-fetch', 'feeler']
            established = self.status()['established_total']
            for origin in origins + ['manual']:
                before = self.status()['failures']['pin']
                if origin == 'manual':
                    self.nodes[1].addnode(destination, 'onetry', False)
                else:
                    self.nodes[1].addconnection(destination, origin, False)
                self.wait_until(lambda: self.status()['failures']['pin'] > before)
                self.wait_until(lambda: not self.nodes[1].getpeerinfo())
                assert_equal(self.status()['established_total'], established)
                time.sleep(1.1)
            assert_equal(self.ready(2)[0]['session_id'], healthy)
            self.restart_node(1, self.extra_args[1] + [pin])
            self.connect_nodes(1, 0)
            pinned_session = self.assert_ready(pinned=True)
            self.clear()
            # The same socket endpoint with a different destination spelling
            # remains permissionless; it must not inherit the exact pin.
            alternate = f'127.0.0.1:0{p2p_port(0)}'
            self.nodes[1].addnode(alternate, 'onetry', False)
            unpinned_session = self.assert_ready(pinned=False)
            assert unpinned_session != pinned_session
            self.clear()
            self.connect_nodes(1, 0)
            assert self.assert_ready(pinned=True) not in (pinned_session, unpinned_session)
            events.append({'exact_pin_reconnect': True, 'alternate_destination_unverified': True,
                           'mismatch_retried_without_replacement': True})
            self.clear()
            self.restart_node(0, self.extra_args[0] + ['-pqp2pidentity=operator.dat'])
            assert_equal(PQTransportTest.public_fingerprint(self, self.nodes[0], 'operator.dat'), fingerprint)
            self.connect_nodes(2, 0)

        if not self.options.public_testnet:
            for origin in ('outbound-full-relay', 'block-relay-only', 'addr-fetch', 'feeler'):
                before = self.status()['established_total']
                self.nodes[1].addconnection(f'127.0.0.1:{p2p_port(0)}', origin, False)
                self.wait_until(lambda: self.status()['established_total'] > before)
                for peer in self.ready():
                    assert_equal(peer['transport_protocol_type'], 'pq-v1')
                    assert_equal(peer['pq_identity_verified'], pin is not None)
                events.append({'origin': origin, 'completed_pq_gate': True})
                self.clear()
        self.stop_node(1)
        for args, error in [
            (self.extra_args[1] + ['-v2transport=1'], '-v2transport=1 contradicts required PQ; classical P2P transports are disabled.'),
            (['-regtest=0', '-testnet=0', '-chain=main', '-listen=0', '-connect=0', '-pqp2p=off'], 'PQ P2P is compulsory on Mercatura mainnet; -pqp2p=off is forbidden.'),
        ]:
            self.nodes[1].assert_start_raises_init_error(extra_args=args, expected_msg='Error: ' + error)
        if self.options.public_testnet:
            default_args = [a for a in self.extra_args[1] if not a.startswith('-pqp2p=')]
            enforced = self.status(0)['testnet_enforced_build']
            self.start_node(1, default_args)
            assert_equal(self.status()['required'], enforced)
            assert_equal(self.status()['testnet_activation'], 'pending')
            self.stop_node(1)
            if enforced:
                self.nodes[1].assert_start_raises_init_error(extra_args=default_args + ['-pqp2p=off'],
                    expected_msg='Error: This qualification build requires public-testnet PQ; -pqp2p=off is forbidden. Public activation remains pending.')
            else:
                self.start_node(1, default_args + ['-pqp2p=off'])
                assert not self.status()['required']
                self.stop_node(1)
            events.append({'default_profile_required': enforced, 'activation': 'pending'})
        self.start_node(1, self.extra_args[1])
        assert_equal(self.nodes[0].getblockcount(), 0)
        (Path(self.options.tmpdir) / 'pq-required-evidence.json').write_text(json.dumps({
            'chain': self.chain, 'events': events, 'status': [self.status(n) for n in range(self.num_nodes)],
            'persistent_pin_tests': os.name != 'nt', 'public_network_contact': False,
            'genesis': self.nodes[0].getblockhash(0),
        }, indent=2) + '\n')


if __name__ == '__main__':
    run_owned_test(PQRequiredTest(__file__))
