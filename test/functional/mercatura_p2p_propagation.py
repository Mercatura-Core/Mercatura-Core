#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ transaction and block propagation functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaP2PPropagationTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.uses_wallet = True

    def mine_blocks_batched(self, node, count, address, batch_size=10):
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

        self.log.info("Creating native PQ wallets on both nodes")

        node0.createwallet(wallet_name="sender")
        node1.createwallet(wallet_name="receiver")

        sender = node0.get_wallet_rpc("sender")
        receiver = node1.get_wallet_rpc("receiver")

        sender_address = sender.getnewaddress()
        receiver_address = receiver.getnewaddress()

        assert sender_address.startswith("mcrt1z")
        assert receiver_address.startswith("mcrt1z")

        self.log.info("Mining mature sender funds on node 0")

        self.mine_blocks_batched(
            node0,
            101,
            sender_address,
        )

        self.sync_blocks()

        assert_equal(
            node0.getblockcount(),
            101,
        )

        assert_equal(
            node1.getblockcount(),
            101,
        )

        assert_equal(
            node0.getbestblockhash(),
            node1.getbestblockhash(),
        )

        assert sender.getbalance() > Decimal("0.00")
        assert_equal(
            receiver.getbalance(),
            Decimal("0.00"),
        )

        self.log.info(
            "Broadcasting native PQ transaction from node 0"
        )

        txid = sender.sendtoaddress(
            receiver_address,
            Decimal("1.00"),
        )

        assert txid in node0.getrawmempool()

        self.log.info(
            "Waiting for PQ transaction to propagate to node 1"
        )

        self.wait_until(
            lambda: txid in node1.getrawmempool(),
            timeout=30,
        )

        assert txid in node1.getrawmempool()

        pending = receiver.getbalances()["mine"]["untrusted_pending"]

        assert_equal(
            pending,
            Decimal("1.00"),
        )

        self.log.info(
            "Mining propagated PQ transaction on node 1"
        )

        mining_address = receiver.getnewaddress()

        assert mining_address.startswith("mcrt1z")

        mined_hashes = self.generatetoaddress(
            node1,
            1,
            mining_address,
        )

        assert_equal(
            len(mined_hashes),
            1,
        )

        assert txid not in node1.getrawmempool()

        self.log.info(
            "Waiting for mined block to propagate back to node 0"
        )

        self.sync_blocks()

        assert_equal(
            node0.getblockcount(),
            102,
        )

        assert_equal(
            node1.getblockcount(),
            102,
        )

        assert_equal(
            node0.getbestblockhash(),
            node1.getbestblockhash(),
        )

        assert txid not in node0.getrawmempool()

        assert_equal(
            receiver.getbalance(),
            Decimal("1.00"),
        )

        tx = receiver.gettransaction(txid)

        assert_equal(
            tx["confirmations"],
            1,
        )

        self.log.info(
            "Mercatura native PQ transaction and block propagation passed"
        )


if __name__ == "__main__":
    MercaturaP2PPropagationTest(__file__).main()
