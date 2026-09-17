#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ transaction reorg functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaPQReorgTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.uses_wallet = True

    def mine_blocks_batched(self, node, count, address, batch_size=10):
        """Mine MercaHash regtest blocks in bounded RPC batches."""
        hashes = []

        while count > 0:
            batch = min(count, batch_size)
            hashes.extend(
                self.generatetoaddress(
                    node,
                    batch,
                    address,
                )
            )
            count -= batch

        return hashes

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]

        self.log.info("Creating PQ wallets")
        node0.createwallet(wallet_name="sender")
        node0.createwallet(wallet_name="receiver")
        node1.createwallet(wallet_name="miner")

        sender = node0.get_wallet_rpc("sender")
        receiver = node0.get_wallet_rpc("receiver")
        miner = node1.get_wallet_rpc("miner")

        sender_address = sender.getnewaddress()
        receiver_address = receiver.getnewaddress()
        miner_address = miner.getnewaddress()

        assert sender_address.startswith("mcrt1z")
        assert receiver_address.startswith("mcrt1z")
        assert miner_address.startswith("mcrt1z")

        self.log.info("Mining mature sender funds")
        self.mine_blocks_batched(
            node0,
            101,
            sender_address,
        )

        self.sync_all()

        assert_equal(node0.getblockcount(), 101)
        assert_equal(node1.getblockcount(), 101)
        assert sender.getbalance() > Decimal("0.00")

        self.log.info("Splitting the two-node network")
        self.disconnect_nodes(0, 1)

        self.log.info("Creating PQ spend on node 0 branch")
        txid = sender.sendtoaddress(
            receiver_address,
            Decimal("1.00"),
        )

        assert txid in node0.getrawmempool()

        self.log.info("Mining PQ spend into node 0 branch")
        node0_branch = self.generatetoaddress(
            node0,
            1,
            sender_address,
            sync_fun=self.no_op,
        )

        assert_equal(len(node0_branch), 1)
        assert_equal(node0.getblockcount(), 102)
        assert_equal(receiver.getbalance(), Decimal("1.00"))

        self.log.info("Mining longer competing branch on node 1")
        node1_branch = self.generatetoaddress(
            node1,
            2,
            miner_address,
            sync_fun=self.no_op,
        )

        assert_equal(len(node1_branch), 2)
        assert_equal(node1.getblockcount(), 103)

        self.log.info("Reconnecting nodes and triggering reorg")
        self.connect_nodes(0, 1)
        self.sync_blocks()

        assert_equal(node0.getblockcount(), 103)
        assert_equal(node1.getblockcount(), 103)
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())

        self.log.info("Verifying disconnected PQ spend returns to originating mempool")

        assert txid in node0.getrawmempool()

        balances = receiver.getbalances()["mine"]

        assert_equal(balances["trusted"], Decimal("0.00"))
        assert_equal(balances["untrusted_pending"], Decimal("1.00"))

        self.log.info("Mining returned PQ transaction on winning chain")
        confirm_hashes = self.generatetoaddress(
            node0,
            1,
            sender_address,
        )

        assert_equal(len(confirm_hashes), 1)
        assert txid not in node0.getrawmempool()

        self.sync_all()

        assert_equal(receiver.getbalance(), Decimal("1.00"))

        tx = receiver.gettransaction(txid)
        assert_equal(tx["confirmations"], 1)

        self.log.info("Mercatura native PQ reorg recovery passed")


if __name__ == "__main__":
    MercaturaPQReorgTest(__file__).main()
