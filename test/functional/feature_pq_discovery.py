#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Four-process PQ bootstrap/recovery rehearsal with strictly mapped lab sockets.

Virtual routable addresses exercise the real addrman/netgroup algorithms; the
SOCKS fixture forwards exclusively to these four owned loopback processes.
This is not evidence of independent public operators, real NAT or Tor behavior.
"""
from decimal import Decimal
import json
import os
from pathlib import Path
import socket
import time

from feature_pq_required import ClassicalEndpoint
from feature_pq_transport import PQTransportTest
from test_framework.pq_lab_proxy import PQLabProxy, run_owned_test
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, p2p_port


class PQDiscoveryTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument('--public-testnet', action='store_true')
        parser.add_argument('--ipv6', action='store_true')

    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.disable_autoconnect = False
        self.chain = 'testnet' if self.options.public_testnet else 'regtest'
        self.uses_wallet = not self.options.public_testnet
        self.rpc_timeout = 120
        self.extra_args = [[] for _ in range(4)]

    def skip_test_if_missing_module(self):
        if self.uses_wallet:
            self.skip_if_no_wallet()

    def setup_network(self):
        self.add_nodes(self.num_nodes)  # Start only inside the owned proxy lifetime.
        for node in self.nodes:
            # This fixture supplies explicit IPv4/IPv6 binds per process. The
            # framework's default bind must not be duplicated in merged config.
            node.replace_in_config([('bind=127.0.0.1', '')])

    def live(self, n):
        return [peer for peer in self.nodes[n].getpeerinfo() if peer['pq_encrypted'] and peer['version']]

    def all_pq(self, n):
        peers = self.nodes[n].getpeerinfo()
        assert all(peer['transport_protocol_type'] == 'pq-v1' for peer in peers)
        for peer in self.live(n):
            assert peer['pq_authenticated'] and not peer['pq_identity_verified']
        return peers

    @staticmethod
    def process_sample(pid):
        proc = Path('/proc') / str(pid)
        if not (proc / 'status').is_file():
            return {'available': False, 'reason': 'Linux /proc metrics unavailable'}
        sample = {'available': True, 'pid': pid}
        for line in (proc / 'status').read_text().splitlines():
            key, _, value = line.partition(':')
            if key in ('VmRSS','VmHWM','Threads'):
                sample[key] = int(value.split()[0])
        sample['descriptors_sampled'] = len(list((proc / 'fd').iterdir()))
        fields = (proc / 'stat').read_text().rsplit(')',1)[1].split()
        sample['cpu_seconds'] = (int(fields[11])+int(fields[12])) / os.sysconf('SC_CLK_TCK')
        return sample

    def run_test(self):
        ipv6 = self.options.ipv6
        loopback = '::1' if ipv6 else '127.0.0.1'
        if ipv6:
            with socket.socket(socket.AF_INET6) as probe:
                probe.bind((loopback, 0))  # Report unavailable IPv6 honestly as a test failure.
        host = lambda node, alias=1: f'2001:{0x1234 + node * 0x1111:x}::{alias:x}' if ipv6 else f'{11+node}.{1+node}.0.{alias}'
        endpoint = lambda node, alias=1: f'[{host(node, alias)}]:27778' if ipv6 else f'{host(node, alias)}:27778'
        destinations = {(host(n, alias), 27778): (loopback, p2p_port(n)) for n in range(4) for alias in range(1, 33)}
        observed = {'chain': self.chain, 'lab_only': True, 'ipv6': ipv6,
                    'proxy_latency_seconds': [0.01, 0.02], 'proxy_bandwidth_bytes_per_second': 2 * 1024 * 1024,
                    'independent_public_operators_qualified': False, 'phases': []}
        started = time.monotonic()
        with ClassicalEndpoint() as incompatible, PQLabProxy(
                destinations | {('19.9.0.1',27778): ('127.0.0.1',incompatible.listener.getsockname()[1])},
                latency=(0.01, 0.02), bytes_per_second=2 * 1024 * 1024) as proxy:
            common = ['-dns=0', '-dnsseed=0', '-fixedseeds=0', '-discover=0', '-natpmp=0', '-maxconnections=16',
                      f'-proxy={proxy.address}', '-proxyrandomize=0', '-onlynet=' + ('ipv6' if ipv6 else 'ipv4')]
            if not self.options.public_testnet:
                common += ['-pqp2p=required']
                common += ['-mercahashv2height=0']
            for n in range(4):
                bind = f'[{loopback}]:{p2p_port(n)}' if ipv6 else f'{loopback}:{p2p_port(n)}'
                self.extra_args[n] = common + [f'-bind={bind}']
            for n in (0, 1):
                self.extra_args[n] += ['-connect=0']
                if os.name != 'nt':
                    self.extra_args[n] += ['-pqp2pidentity=server.dat', '-pqp2pidentitycreate=1']
                self.start_node(n, self.extra_args[n])
                if os.name != 'nt':
                    self.extra_args[n].remove('-pqp2pidentitycreate=1')
            fingerprint = PQTransportTest.public_fingerprint(self,self.nodes[0],'server.dat') if os.name != 'nt' else None
            # Two simulated independent address sources. Each serves many
            # aliases in one netgroup of the OTHER owned process. The ordinary
            # 23% getaddr response then always provides useful addresses, and
            # the second peer teaches the joiner the first group in turn.
            for n in (0, 1):
                for alias in range(1, 33):
                    self.nodes[n].addpeeraddress(host(1-n, alias), 27778)
                assert self.nodes[n].getnetworkinfo()['pqp2p']['known_addresses'] >= 8
                assert all(not (a['services'] & (1 << 24)) for a in self.nodes[n].getnodeaddresses(0))
            for n, seed in ((2, 0), (3, 1)):
                assert not (self.nodes[n].chain_path / 'peers.dat').exists()
                self.extra_args[n] += [f'-seednode={endpoint(seed)}']
                begin = time.monotonic()
                self.start_node(n, self.extra_args[n])
                self.wait_until(lambda: len([p for p in self.live(n) if p['connection_type'] == 'outbound-full-relay']) >= 2, timeout=90)
                peers = self.all_pq(n)
                groups = {p['addr'].split(']')[0].split('::')[0] if ipv6 else '.'.join(p['addr'].split('.')[:2])
                          for p in peers if p['connection_type'] == 'outbound-full-relay' and p['pq_encrypted']}
                assert len(groups) >= 2
                observed['phases'].append({'joiner': n, 'bootstrap_source': seed, 'seconds': time.monotonic()-begin,
                                          'discovered_addresses': self.nodes[n].getnetworkinfo()['pqp2p']['known_addresses'],
                                          'outbound_groups': sorted(groups)})

            if self.uses_wallet:
                self.nodes[2].createwallet('miner', load_on_startup=True)
                self.nodes[3].createwallet('receiver', load_on_startup=True)
                miner = self.nodes[2].get_wallet_rpc('miner')
                receiver = self.nodes[3].get_wallet_rpc('receiver')
                reward = miner.getnewaddress()
                destination = receiver.getnewaddress()
                self.generatetoaddress(self.nodes[2], 101, reward)
                self.sync_blocks(timeout=120)
                begin = time.monotonic()
                txid = miner.sendtoaddress(destination, Decimal('1.00'))
                self.sync_mempools(timeout=60)
                observed['transaction_relay_seconds'] = time.monotonic()-begin
                observed['txid'] = txid
                begin = time.monotonic()
                observed['confirmation_block'] = self.generatetoaddress(self.nodes[2], 1, reward)[0]
                self.sync_blocks(timeout=120)
                observed['block_relay_seconds'] = time.monotonic()-begin
                assert_equal(receiver.getbalance(), Decimal('1.00'))
                observed['confirmed_receiver_balance'] = '1.00'

            if not ipv6:
                before = self.nodes[2].getnetworkinfo()['pqp2p']['failures']['framing']
                assert self.nodes[2].addpeeraddress('19.9.0.1',27778)['success']
                self.wait_until(lambda: self.nodes[2].getnetworkinfo()['pqp2p']['failures']['framing'] > before, timeout=60)
                attempts = len(incompatible.prefaces)
                time.sleep(3)
                assert_equal(len(incompatible.prefaces),attempts)
                assert all(preface == b'Mercatura/P2P/PQ' for preface in incompatible.prefaces)
                observed['incompatible_candidate_attempts'] = attempts
                self.all_pq(2)
            # A lost bootstrap must leave the learned B path functioning.
            self.stop_node(0)
            for n in (2, 3):
                self.wait_until(lambda: any(host(1, alias) in p['addr'] for alias in range(1, 33) for p in self.live(n)))
                self.nodes[n].ping()
                self.all_pq(n)
            old_sessions = {p['session_id'] for p in self.live(3)}
            self.restart_node(3, self.extra_args[3])
            self.wait_until(lambda: bool(self.live(3)), timeout=60)
            assert old_sessions.isdisjoint({p['session_id'] for p in self.live(3)})
            assert self.nodes[3].getnetworkinfo()['pqp2p']['known_addresses'] > 0
            self.all_pq(3)
            # Restart retained addrman state while the initial A source is down.
            if self.uses_wallet:
                receiver = self.nodes[3].get_wallet_rpc('receiver')
                assert_equal(receiver.getbalance(), Decimal('1.00'))
                self.nodes[3].setnetworkactive(False)
                self.wait_until(lambda: not self.nodes[3].getpeerinfo())
                self.generatetoaddress(self.nodes[2], 2, reward, sync_fun=lambda: None)
                self.nodes[3].setnetworkactive(True)
                self.wait_until(lambda: bool(self.live(3)), timeout=60)
                self.sync_blocks(self.nodes[1:], timeout=120)
            self.start_node(0, self.extra_args[0])
            if fingerprint:
                assert_equal(PQTransportTest.public_fingerprint(self,self.nodes[0],'server.dat'),fingerprint)
                observed['persistent_server_fingerprint'] = fingerprint
            if self.uses_wallet:
                # A restarts at the preserved older tip and downloads the gap.
                self.nodes[2].addnode(endpoint(0), 'onetry', False)
                self.wait_until(lambda: self.nodes[0].getbestblockhash() == self.nodes[2].getbestblockhash(), timeout=120)
            for n in range(4):
                self.all_pq(n)
                assert self.nodes[n].verifychain(4, 0)
            tips = [self.nodes[n].getbestblockhash() for n in range(4)]
            assert_equal(len(set(tips)), 1)
            observed['tips'] = tips
            observed['heights'] = [node.getblockcount() for node in self.nodes]
            observed['statuses'] = [node.getnetworkinfo()['pqp2p'] for node in self.nodes]
            for status in observed['statuses']:
                assert status['pending'] <= 64 and status['queued'] <= 32 and status['active'] <= 2
            observed['resource_samples'] = [self.process_sample(node.process.pid) for node in self.nodes]
            for n in range(4):
                self.stop_node(n)
        observed['proxy_events'] = proxy.events
        observed['all_children_joined'] = True
        observed['seconds'] = time.monotonic()-started
        (Path(self.options.tmpdir) / 'pq-discovery-evidence.json').write_text(json.dumps(observed, indent=2) + '\n')


if __name__ == '__main__':
    run_owned_test(PQDiscoveryTest(__file__))
