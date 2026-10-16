#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Exercise native archival/pruned persistence, restart and reindex recovery.

Deterministic synchronization failures are covered at the FileCommit boundary
by blockstorage_durability_tests; this test uses normal daemon persistence.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaStorageRecoveryTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.extra_args = [["-fastprune", "-prune=1"]]

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("miner", load_on_startup=True)
        wallet = node.get_wallet_rpc("miner")
        address = wallet.getnewaddress()

        # A dedicated oversized file at height 1 gives manual pruning a real
        # early file to remove without mining hundreds of large transactions.
        self.generateblock(node, output="raw(6a" + "ff" * 0x10000 + ")", transactions=[])
        for _ in range(30):
            self.generatetoaddress(node, 10, address)
        tip = node.getbestblockhash()
        stats = node.gettxoutsetinfo()
        balance = wallet.getbalance()
        early_file = node.blocks_path / "blk00001.dat"
        assert early_file.exists()

        self.log.info("Verify normal restart preserves chainstate and PQ rewards")
        self.restart_node(0)
        assert_equal(node.getbestblockhash(), tip)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], stats["hash_serialized_3"])
        wallet = node.get_wallet_rpc("miner")
        assert_equal(wallet.getbalance(), balance)

        self.log.info("Reindex all unpruned data before exercising pruning")
        self.stop_node(0)
        node.start(extra_args=self.extra_args[0] + ["-reindex"])
        node.wait_for_rpc_connection(wait_for_import=False)
        self.wait_until(lambda: node.getblockcount() == 301 and node.getmempoolinfo()["loaded"], timeout=600)
        assert_equal(node.getbestblockhash(), tip)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], stats["hash_serialized_3"])
        wallet = node.get_wallet_rpc("miner")
        assert_equal(wallet.getbalance(), balance)

        self.log.info("Prune real old files and verify durable metadata on restart")
        assert node.pruneblockchain(1) >= 1
        assert not early_file.exists()
        self.restart_node(0)
        assert not early_file.exists()
        assert_equal(node.getbestblockhash(), tip)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], stats["hash_serialized_3"])
        wallet = node.get_wallet_rpc("miner")
        assert_equal(wallet.getbalance(), balance)
        assert_equal(node.getblockchaininfo()["pruned"], True)
        self.generatetoaddress(node, 1, address)
        assert_equal(node.getblockcount(), 302)


if __name__ == "__main__":
    MercaturaStorageRecoveryTest(__file__).main()
