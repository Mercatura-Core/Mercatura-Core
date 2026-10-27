#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Tip-independent required transport through fresh IBD, offline restore and reorg.

The synthetic boundary below exists only in this regression's observations. No
production activation trigger, height, clock rule or public-network contact.
"""
import json
from pathlib import Path

from test_framework.pq_lab_proxy import run_owned_test
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class PQActivationTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [['-pqp2p=required', '-connect=0', '-dnsseed=0', '-fixedseeds=0',
                            '-discover=0', '-natpmp=0', '-listenonion=0'] for _ in range(4)]

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)

    def policy(self, node):
        info = self.nodes[node].getnetworkinfo()['pqp2p']
        assert info['required']
        assert_equal(info['transport'], 'pq-v1')
        for peer in self.nodes[node].getpeerinfo():
            assert_equal(peer['transport_protocol_type'], 'pq-v1')
            if peer['version']:
                assert peer['pq_authenticated'] and peer['pq_encrypted']
        return info

    def run_test(self):
        boundary = 4  # Synthetic observation, never a proposed public height.
        events = []
        self.generatetodescriptor(self.nodes[0], 2, 'raw(51)', sync_fun=lambda: self.sync_blocks(self.nodes[:2]))
        self.connect_nodes(2, 0)
        self.sync_blocks(self.nodes[:3])
        self.stop_node(2)  # Preserved offline datadir is below the boundary.
        before = self.nodes[0].getpeerinfo()[0]['session_id']
        self.generatetodescriptor(self.nodes[0], 5, 'raw(51)', sync_fun=lambda: self.sync_blocks(self.nodes[:2]))
        assert_equal(self.nodes[0].getpeerinfo()[0]['session_id'], before)
        self.policy(0)
        events.append({'scenario': 'synchronized_miner_crosses_synthetic_boundary', 'height': 7,
                       'existing_pq_session_preserved': True})

        assert_equal(self.nodes[3].getblockcount(), 0)
        self.policy(3)
        self.connect_nodes(3, 0)
        self.sync_blocks([self.nodes[0], self.nodes[3]])
        relay_session = next(p['session_id'] for p in self.nodes[3].getpeerinfo() if not p['inbound'])
        events.append({'scenario': 'fresh_genesis_ibd_after_boundary', 'height': self.nodes[3].getblockcount()})

        # A local rollback below H must not switch transport policy. Then mine
        # a competing branch whose greater work reorganizes the other cohort.
        self.nodes[1].setnetworkactive(False)
        self.wait_until(lambda: not self.nodes[1].getpeerinfo())
        fork_block = self.nodes[1].getblockhash(3)
        old_tip = self.nodes[0].getbestblockhash()
        self.nodes[1].invalidateblock(fork_block)
        assert_equal(self.nodes[1].getblockcount(), 2)
        self.policy(1)
        self.nodes[1].setnetworkactive(True)
        self.generatetodescriptor(self.nodes[1], 7, 'raw(51)', sync_fun=self.no_op)
        self.connect_nodes(1, 0)
        self.sync_blocks([self.nodes[0], self.nodes[1], self.nodes[3]])
        assert_equal(self.nodes[0].getblockcount(), 9)
        assert self.nodes[0].getbestblockhash() != old_tip
        assert_equal(next(p['session_id'] for p in self.nodes[3].getpeerinfo() if not p['inbound']), relay_session)
        events.append({'scenario': 'most_work_reorg_across_synthetic_boundary', 'fork_height': 2,
                       'new_height': 9, 'uninterrupted_pq_relay_session': True})

        self.start_node(2, self.extra_args[2])
        assert_equal(self.nodes[2].getblockcount(), 2)
        self.policy(2)
        self.connect_nodes(2, 0)
        self.sync_blocks()
        events.append({'scenario': 'offline_lagging_datadir_rejoins', 'from_height': 2, 'to_height': 9})
        self.restart_node(3, self.extra_args[3] + ['-reindex'])
        self.policy(3)
        self.connect_nodes(3, 0)
        self.sync_blocks()
        utxo = self.nodes[0].gettxoutsetinfo()['hash_serialized_3']
        for n, node in enumerate(self.nodes):
            self.policy(n)
            assert node.verifychain(4, 0)
            assert_equal(node.gettxoutsetinfo()['hash_serialized_3'], utxo)
        report = {'scope': 'owned regtest; tip-independent required mode', 'synthetic_boundary': boundary,
                  'public_activation_selected': False, 'public_network_contact': False, 'events': events,
                  'tip': self.nodes[0].getbestblockhash(), 'utxo': utxo,
                  'statuses': [self.policy(n) for n in range(4)]}
        self.stop_nodes()
        report['all_children_exited'] = all(node.process is None for node in self.nodes)
        assert report['all_children_exited']
        (Path(self.options.tmpdir) / 'pq-activation-evidence.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    run_owned_test(PQActivationTest(__file__))
