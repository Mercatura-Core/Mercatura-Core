#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Isolated Stage A: actual PQ node relay, pins, storage, reconnects and no fallback.

All datadirs/ports belong to the functional harness. No public infrastructure.
The 101-block fixture is solely coinbase-maturity setup, not a cadence benchmark.
"""
import hashlib
import json
from pathlib import Path
import socket
import time
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, p2p_port

PQ = ['-pqp2p=required', '-dnsseed=0', '-fixedseeds=0', '-discover=0', '-natpmp=0', '-connect=0']


class PQTransportTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument('--mercahash-v2-height', type=int, choices=(-1, 0, 20), default=-1,
                            help='Isolated regtest V1, V2 from genesis, or height-20 transition')

    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.pq_args = PQ + [f'-mercahashv2height={self.options.mercahash_v2_height}']
        self.extra_args = [self.pq_args, self.pq_args, ['-connect=0', '-dnsseed=0', '-discover=0', '-natpmp=0']]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        self.setup_nodes()

    def peers(self):
        return self.nodes[0].getpeerinfo(), self.nodes[1].getpeerinfo()

    def check_session(self, pinned_outbound=False):
        peers0, peers1 = self.peers()
        assert_equal(len(peers0), 1)
        assert_equal(len(peers1), 1)
        for peer in peers0 + peers1:
            assert_equal(peer['transport_protocol_type'], 'pq-v1')
            assert peer['pq_encrypted'] and peer['pq_authenticated']
            assert_equal(peer['pq_kem'], 'ML-KEM-768')
            assert_equal(peer['pq_auth_algorithm'], 'ML-DSA-65')
            assert peer['pq_identity'].startswith('mcapq1:')
            assert_equal(peer['pq_identity_verified'], pinned_outbound and not peer['inbound'])
        # Ordinary node peers send no decoy packets. Quiescent receive accounting
        # must leave exactly the directional handshake budget after subtracting
        # authenticated application-message wire sizes.
        def budgets_match():
            current = self.nodes[0].getpeerinfo() + self.nodes[1].getpeerinfo()
            return len(current) == 2 and all(peer['bytesrecv'] - sum(peer['bytesrecv_per_msg'].values()) == (7750 if peer['inbound'] else 7738) for peer in current)
        self.wait_until(budgets_match)
        assert_equal(peers0[0]['session_id'], peers1[0]['session_id'])
        assert_equal(len(peers0[0]['session_id']), 64)
        return peers0[0]['session_id'], peers0[0]['pq_identity'], peers1[0]['pq_identity']

    def public_fingerprint(self, node, filename='pq_identity.dat'):
        # Read only the network and public-key fields, never the stored seed.
        with (node.chain_path / filename).open('rb') as stream:
            stream.seek(10)
            network = stream.read(85)
            stream.seek(127)
            public = stream.read(1952)
        label = b'Mercatura/P2PIdentity/v1'
        value = len(label).to_bytes(2, 'little') + label + b'\x01\x00' + network + (1952).to_bytes(2, 'little') + public
        return 'mcapq1:' + hashlib.sha3_384(value).hexdigest()

    def run_test(self):
        start = time.monotonic()
        self.connect_nodes(1, 0)
        initial_connection_ms = (time.monotonic()-start)*1000
        first = self.check_session()
        self.log.info('Real isolated PQ connection established; equal session IDs, unpinned possession only')
        self.nodes[0].createwallet('sender')
        self.nodes[1].createwallet('receiver')
        sender = self.nodes[0].get_wallet_rpc('sender')
        receiver = self.nodes[1].get_wallet_rpc('receiver')
        address, destination = sender.getnewaddress(), receiver.getnewaddress()
        assert address.startswith('mcrt1z') and destination.startswith('mcrt1z')
        self.log.info('Preparing a bounded coinbase-maturity fixture and checking block relay')
        for offset in range(0, 101, 5):
            self.generatetoaddress(self.nodes[0], min(5, 101-offset), address, sync_fun=self.no_op)
        self.sync_blocks(self.nodes[:2])
        txid = sender.sendtoaddress(destination, Decimal('1.00'))
        self.wait_until(lambda: txid in self.nodes[1].getrawmempool())
        self.nodes[1].syncwithvalidationinterfacequeue()
        assert_equal(receiver.getbalances()['mine']['untrusted_pending'], Decimal('1.00'))
        block = self.generatetoaddress(self.nodes[1], 1, destination, sync_fun=self.no_op)[0]
        self.sync_blocks(self.nodes[:2])
        assert_equal(self.nodes[0].getbestblockhash(), block)
        assert txid not in self.nodes[0].getrawmempool() and txid not in self.nodes[1].getrawmempool()
        self.nodes[1].syncwithvalidationinterfacequeue()
        assert_equal(receiver.getbalance(), Decimal('1.00'))
        self.log.info('Native PQ transaction %s and block %s relayed over pq-v1', txid, block)

        self.disconnect_nodes(1, 0)
        reconnect_start = time.monotonic()
        self.connect_nodes(1, 0)
        reconnect_ms = (time.monotonic()-reconnect_start)*1000
        second = self.check_session()
        assert all(a != b for a, b in zip(first, second))
        self.log.info('Reconnect changed session ID and both default ephemeral identities')

        self.log.info('Rejecting ordinary classical peers without downgrade')
        with self.nodes[0].assert_debug_log(['Experimental PQ transport rejected'], timeout=5):
            self.nodes[2].addnode(f'127.0.0.1:{p2p_port(0)}', 'onetry')
        self.wait_until(lambda: self.nodes[2].getpeerinfo() == [])
        self.check_session()
        fixture = json.loads((Path(__file__).resolve().parents[2] / 'src/test/data/pq_p2p_candidate1.json').read_text())
        for malformed in [b'not-a-pq-preface!', bytes.fromhex(fixture['preface']) + bytes.fromhex('0100010001000100b50c0000')]:
            with socket.create_connection(('127.0.0.1', p2p_port(0)), timeout=5) as wire:
                wire.sendall(malformed)
                try:
                    assert_equal(wire.recv(1), b'')
                except ConnectionResetError:
                    pass
        self.check_session()

        self.log.info('Creating explicit persistent identities in temporary regtest datadirs')
        self.stop_node(1)
        self.stop_node(0)
        persistent = self.pq_args + ['-pqp2pidentity=pq_identity.dat']
        self.start_node(0, persistent + ['-pqp2pidentitycreate=1'])
        self.start_node(1, persistent + ['-pqp2pidentitycreate=1'])
        fp0, fp1 = self.public_fingerprint(self.nodes[0]), self.public_fingerprint(self.nodes[1])
        self.stop_node(1)
        pin = f'-pqp2ppin=127.0.0.1:{p2p_port(0)}={fp0}'
        self.start_node(1, persistent + [pin])
        self.connect_nodes(1, 0)
        pinned = self.check_session(pinned_outbound=True)
        assert_equal(pinned[1], fp1)
        assert_equal(pinned[2], fp0)
        self.disconnect_nodes(1, 0)
        self.connect_nodes(1, 0)
        pinned_again = self.check_session(pinned_outbound=True)
        assert pinned_again[0] != pinned[0]
        assert_equal(pinned_again[1:], pinned[1:])
        self.stop_node(1)
        bad_fp = fp0[:-1] + ('0' if fp0[-1] != '0' else '1')
        self.start_node(1, persistent + [f'-pqp2ppin=127.0.0.1:{p2p_port(0)}={bad_fp}'])
        with self.nodes[1].assert_debug_log(['expected identity mismatch'], timeout=5):
            self.nodes[1].addnode(f'127.0.0.1:{p2p_port(0)}', 'onetry')
        self.wait_until(lambda: not self.nodes[1].getpeerinfo() and not self.nodes[0].getpeerinfo())
        self.restart_node(1, persistent + [pin])
        self.connect_nodes(1, 0)
        self.check_session(pinned_outbound=True)
        self.log.info('Wrong pin disconnected without fallback; corrected explicit pin reconnected')

        self.log.info('Explicit identity rotation requires an explicit trusted pin update')
        self.restart_node(0, self.pq_args + ['-pqp2pidentity=rotated.dat', '-pqp2pidentitycreate=1'])
        self.wait_until(lambda: not self.nodes[1].getpeerinfo() and not self.nodes[0].getpeerinfo())
        rotated_fp = self.public_fingerprint(self.nodes[0], 'rotated.dat')
        assert rotated_fp != fp0
        assert_equal(self.public_fingerprint(self.nodes[0]), fp0)
        with self.nodes[1].assert_debug_log(['expected identity mismatch'], timeout=5):
            self.nodes[1].addnode(f'127.0.0.1:{p2p_port(0)}', 'onetry')
        self.wait_until(lambda: not self.nodes[1].getpeerinfo() and not self.nodes[0].getpeerinfo())
        self.restart_node(1, persistent + [f'-pqp2ppin=127.0.0.1:{p2p_port(0)}={rotated_fp}'])
        self.connect_nodes(1, 0)
        self.check_session(pinned_outbound=True)
        # Restarted wallets and nodes retain the confirmed transaction and chain.
        if 'receiver' not in self.nodes[1].listwallets():
            self.nodes[1].loadwallet('receiver')
        assert_equal(self.nodes[1].get_wallet_rpc('receiver').getbalance(), Decimal('1.00'))
        tip = self.generatetoaddress(self.nodes[0], 1, address, sync_fun=self.no_op)[0]
        self.sync_blocks(self.nodes[:2])
        for node in self.nodes[:2]:
            assert_equal(node.getbestblockhash(), tip)
            assert_equal(node.getblockchaininfo()['headers'], node.getblockcount())
            assert node.verifychain(4, 0)
        public_fields = ['transport_protocol_type', 'session_id', 'inbound', 'pq_encrypted', 'pq_authenticated', 'pq_identity_verified', 'pq_identity', 'pq_kem', 'pq_auth_algorithm', 'bytesrecv', 'bytesrecv_per_msg']
        evidence = {'network': 'isolated loopback regtest', 'txid': txid, 'relayed_block': block, 'height': self.nodes[0].getblockcount(), 'handshake_bytes': 15488, 'ephemeral_reconnect_changed_identities': True, 'wrong_pin_rejected_without_fallback': True,
                    'mercahash_v2_height': self.options.mercahash_v2_height, 'explicit_rotation_and_pin_update': True,
                    'post_restart_tip': tip, 'confirmed_receiver_balance': '1.00',
                    'connection_including_version_pong_ms': initial_connection_ms, 'reconnection_including_version_pong_ms': reconnect_ms,
                    'peers': [{field: peer[field] for field in public_fields} for peer in self.nodes[0].getpeerinfo() + self.nodes[1].getpeerinfo()]}
        (Path(self.options.tmpdir) / 'pq-stagea-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
        self.stop_node(2)
        for chain in ['-testnet=1', '-signet=1', '-chain=main']:
            self.nodes[2].assert_start_raises_init_error(extra_args=['-regtest=0', chain, '-pqp2p=required'], expected_msg='Error: Experimental PQ P2P is restricted to isolated regtest; public networks are not approved.')
        self.start_node(2)
        self.log.info('Stage A end-to-end qualification passed in %.3f seconds', time.monotonic()-start)


if __name__ == '__main__':
    PQTransportTest(__file__).main()
