#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Owned four-node upgrade and coordinated rollback against a prior binary.

Run with --configfile=<new-build>/test/config.ini --previous-bindir=<verified-bin>.
Default regtest mines controlled V1 history; --public-testnet rehearses the actual
unchanged public-testnet identity at genesis only. Never use operator datadirs.
No public endpoints, deployment, consensus override or datadir deletion is used.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
from decimal import Decimal

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/functional'))
from test_framework.pq_lab_proxy import run_owned_test  # noqa: E402
from test_framework.test_framework import BitcoinTestFramework  # noqa: E402
from test_framework.util import assert_equal, p2p_port  # noqa: E402


class PQUpgradeRehearsal(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument('--previous-bindir', required=True, type=Path)
        parser.add_argument('--public-testnet', action='store_true')

    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.rpc_timeout = 180
        self.chain = 'testnet' if self.options.public_testnet else 'regtest'
        self.legacy_args = ['-connect=0', '-dnsseed=0', '-fixedseeds=0', '-natpmp=0', '-discover=0',
                            '-listenonion=0', '-maxconnections=16', '-v2transport=0']
        self.required_args = self.legacy_args + ['-pqp2p=required']
        self.extra_args = [self.legacy_args] * self.num_nodes

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        if self.options.usecli or self.options.valgrind:
            raise RuntimeError('Use direct native RPC mode for this binary-switch rehearsal')

    def setup_network(self):
        self.add_nodes(self.num_nodes, self.extra_args)
        self.choose(previous=True)
        self.start_nodes()
        self.join()

    def join(self):
        for n in (1, 2, 3):
            self.connect_nodes(n, 0)

    def snapshot(self):
        rows = []
        for node in self.nodes:
            info = node.getblockchaininfo()
            rows.append({'height': info['blocks'], 'tip': info['bestblockhash'], 'chainwork': info['chainwork'],
                         'genesis': node.getblockhash(0), 'utxo': node.gettxoutsetinfo()['hash_serialized_3']})
        assert all(row == rows[0] for row in rows)
        return rows

    def choose(self, previous, nodes=None):
        binaries = self.get_binaries(str(self.options.previous_bindir) if previous else None)
        argv = binaries.node_argv()
        assert_equal(len(argv), 1)
        for node in self.nodes if nodes is None else nodes:
            node.args[0] = argv[0]
            node.binaries = binaries

    def switch(self, previous):
        self.stop_nodes()
        self.choose(previous)
        self.start_nodes([self.legacy_args if previous else self.required_args] * self.num_nodes)
        self.join()

    def initial_upgrade(self, report):
        # First upgrade the two backbone processes while C/D still run the
        # published legacy release. Their classical attempts must be rejected
        # without disturbing the healthy new PQ backbone session.
        for n in (0,1):
            self.stop_node(n)
        self.choose(previous=False,nodes=self.nodes[:2])
        for n in (0,1):
            self.start_node(n,self.required_args)
        self.connect_nodes(1,0)
        healthy=self.nodes[1].getpeerinfo()[0]['session_id']
        for n in (2,3):
            self.wait_until(lambda: not self.nodes[n].getpeerinfo())
            before=self.nodes[0].getnetworkinfo()['pqp2p']['failures']['framing']
            self.nodes[n].addnode(f'127.0.0.1:{p2p_port(0)}','onetry',False)
            self.wait_until(lambda: self.nodes[0].getnetworkinfo()['pqp2p']['failures']['framing']>before)
            self.wait_until(lambda: not self.nodes[n].getpeerinfo())
            assert_equal(self.nodes[1].getpeerinfo()[0]['session_id'],healthy)
            time.sleep(1.1) # Retain the original per-source admission bucket.
        for n in (2,3):
            self.stop_node(n)
            self.choose(previous=False,nodes=[self.nodes[n]])
            self.start_node(n,self.required_args)
            self.connect_nodes(n,0)
            time.sleep(1.1)
        report['partial_upgrade_classical_clients_rejected']=True
        report['healthy_new_backbone_session_preserved']=True

    def assert_transport(self, required):
        for node in self.nodes:
            peers = node.getpeerinfo()
            assert peers
            for peer in peers:
                assert_equal(peer['transport_protocol_type'], 'pq-v1' if required else 'v1')
                if required:
                    assert peer['pq_encrypted'] and peer['pq_authenticated']
                    assert not peer['pq_identity_verified']
            if required:
                assert node.getnetworkinfo()['pqp2p']['required']

    def run_test(self):
        started = time.monotonic()
        old = Path(self.get_binaries(str(self.options.previous_bindir)).node_argv()[0])
        new = Path(self.get_binaries().node_argv()[0])
        report = {'network': self.chain, 'history_scope': 'genesis only' if self.options.public_testnet else 'controlled V1 regtest history',
                  'real_public_history_qualified': False, 'public_network_contact': False,
                  'old_binary_sha256': hashlib.sha256(old.read_bytes()).hexdigest(),
                  'new_binary_sha256': hashlib.sha256(new.read_bytes()).hexdigest(),
                  'old_version': subprocess.check_output([str(old), '-version'], text=True, timeout=20).splitlines()[0],
                  'new_version': subprocess.check_output([str(new), '-version'], text=True, timeout=20).splitlines()[0]}
        self.assert_transport(False)
        self.nodes[2].createwallet('upgrade-miner', load_on_startup=True)
        self.nodes[3].createwallet('upgrade-receiver', load_on_startup=True)
        miner = self.nodes[2].get_wallet_rpc('upgrade-miner')
        receiver = self.nodes[3].get_wallet_rpc('upgrade-receiver')
        reward = miner.getnewaddress()
        destination = receiver.getnewaddress()
        if not self.options.public_testnet:
            for _ in range(10):
                self.generatetoaddress(self.nodes[2], 10, reward)
            self.generatetoaddress(self.nodes[2], 1, reward)
            report['txid'] = miner.sendtoaddress(destination, Decimal('1.00'))
            self.sync_mempools()
            self.generatetoaddress(self.nodes[2], 1, reward)
            assert_equal(receiver.getbalance(), Decimal('1.00'))
        old_snapshot = self.snapshot()
        old_balance = receiver.getbalance()
        report['before_upgrade'] = old_snapshot
        self.initial_upgrade(report)
        self.assert_transport(True)
        assert_equal(self.snapshot(), old_snapshot)
        receiver = self.nodes[3].get_wallet_rpc('upgrade-receiver')
        assert_equal(receiver.getbalance(), old_balance)
        if not self.options.public_testnet:
            self.generatetoaddress(self.nodes[2], 1, reward)
        new_snapshot = self.snapshot()
        report['after_upgrade'] = new_snapshot
        # Explicit cohort rollback on preserved owned data; no in-process
        # downgrade or off switch is introduced in the required build.
        self.switch(previous=True)
        self.assert_transport(False)
        assert_equal(self.snapshot(), new_snapshot)
        assert_equal(self.nodes[3].get_wallet_rpc('upgrade-receiver').getbalance(), old_balance)
        report['after_coordinated_rollback'] = self.snapshot()
        self.switch(previous=False)
        self.assert_transport(True)
        for n, option in ((3, '-reindex-chainstate'), (2, '-reindex')):
            self.restart_node(n, self.required_args + [option])
            assert_equal(self.nodes[n].getbestblockhash(), new_snapshot[n]['tip'])
            assert_equal(self.nodes[n].gettxoutsetinfo()['hash_serialized_3'], new_snapshot[n]['utxo'])
            assert self.nodes[n].verifychain(4, 0)
            self.connect_nodes(n, 0)
        self.assert_transport(True)
        assert_equal(self.snapshot(), new_snapshot)
        for node in self.nodes:
            assert node.verifychain(4, 0)
        report['receiver_balance_after_restore'] = str(self.nodes[3].get_wallet_rpc('upgrade-receiver').getbalance())
        report['after_reindex'] = self.snapshot()
        self.stop_nodes()
        report['all_children_exited'] = all(node.process is None for node in self.nodes)
        assert report['all_children_exited']
        report['seconds'] = time.monotonic() - started
        (Path(self.options.tmpdir) / 'pq-upgrade-evidence.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    run_owned_test(PQUpgradeRehearsal(__file__))
