#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native three-node end-to-end PQ network test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaThreeNodePQTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.uses_wallet = True

        self.extra_args = [
            ["-fallbackfee=0.01"],
            ["-fallbackfee=0.01"],
            ["-fallbackfee=0.01"],
        ]

        self.rpc_timeout = 120

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine_blocks_batched(self, node, address, count):
        remaining = count

        while remaining:
            batch = min(10, remaining)

            self.generatetoaddress(
                node,
                batch,
                address,
            )

            remaining -= batch

    def assert_common_tip(self):
        tips = {
            node.getbestblockhash()
            for node in self.nodes
        }

        heights = {
            node.getblockcount()
            for node in self.nodes
        }

        assert_equal(
            len(tips),
            1,
        )

        assert_equal(
            len(heights),
            1,
        )

    def run_test(self):
        node0, node1, node2 = self.nodes

        #
        # Give each node its own wallet role.
        #
        node0.createwallet(
            wallet_name="sender",
            load_on_startup=True,
        )

        node1.createwallet(
            wallet_name="receiver",
            load_on_startup=True,
        )

        node2.createwallet(
            wallet_name="miner",
            load_on_startup=True,
        )

        sender = node0.get_wallet_rpc(
            "sender"
        )

        receiver = node1.get_wallet_rpc(
            "receiver"
        )

        miner = node2.get_wallet_rpc(
            "miner"
        )

        sender_mining_address = sender.getnewaddress(
            "sender-mining"
        )

        receiver_address = receiver.getnewaddress(
            "receiver"
        )

        miner_address = miner.getnewaddress(
            "independent-miner"
        )

        assert sender_mining_address.startswith("mcrt1z")
        assert receiver_address.startswith("mcrt1z")
        assert miner_address.startswith("mcrt1z")

        #
        # Fund node0 while all three nodes remain synchronized.
        #
        self.log.info(
            "Mining mature PQ funding on node0"
        )

        self.mine_blocks_batched(
            node0,
            sender_mining_address,
            101,
        )

        self.sync_blocks()

        self.assert_common_tip()

        assert_equal(
            node0.getblockcount(),
            101,
        )

        assert sender.getbalance() > Decimal("0.00")

        #
        # Node0 -> node1:
        # Create a real native PQ transaction and require all three mempools
        # to learn it through normal P2P relay.
        #
        self.log.info(
            "Sending PQ payment from node0 to node1"
        )

        receiver_before = receiver.getreceivedbyaddress(
            receiver_address
        )

        first_txid = sender.sendtoaddress(
            receiver_address,
            Decimal("1.25"),
        )

        self.sync_mempools()

        for index, node in enumerate(self.nodes):
            mempool = node.getrawmempool()

            assert first_txid in mempool, (
                f"node{index} did not receive first PQ transaction"
            )

        #
        # Node2, which did not create the transaction or own either side of
        # it, mines the transaction into the chain.
        #
        self.log.info(
            "Mining node0-to-node1 PQ transaction on node2"
        )

        self.generatetoaddress(
            node2,
            1,
            miner_address,
        )

        self.sync_blocks()

        self.assert_common_tip()

        assert_equal(
            node0.getblockcount(),
            102,
        )

        for node in self.nodes:
            assert first_txid not in node.getrawmempool()

        first_info = receiver.gettransaction(
            first_txid
        )

        assert first_info["confirmations"] >= 1

        assert_equal(
            receiver.getreceivedbyaddress(
                receiver_address
            ),
            receiver_before + Decimal("1.25"),
        )

        #
        # Now reverse direction.
        #
        # Node1 spends its newly confirmed PQ output back to a fresh node0
        # destination. This exercises independent wallet signing on a second
        # node after network propagation and external mining.
        #
        self.log.info(
            "Sending PQ return payment from node1 to node0"
        )

        return_address = sender.getnewaddress(
            "return-payment"
        )

        assert return_address.startswith("mcrt1z")

        return_before = sender.getreceivedbyaddress(
            return_address
        )

        second_txid = receiver.sendtoaddress(
            return_address,
            Decimal("0.50"),
        )

        assert second_txid != first_txid

        self.sync_mempools()

        for index, node in enumerate(self.nodes):
            mempool = node.getrawmempool()

            assert second_txid in mempool, (
                f"node{index} did not receive return PQ transaction"
            )

        #
        # This time node0 mines the transaction created by node1.
        #
        self.log.info(
            "Mining node1-to-node0 PQ transaction on node0"
        )

        self.generatetoaddress(
            node0,
            1,
            sender_mining_address,
        )

        self.sync_blocks()

        self.assert_common_tip()

        assert_equal(
            node0.getblockcount(),
            103,
        )

        for node in self.nodes:
            assert second_txid not in node.getrawmempool()

        second_info = sender.gettransaction(
            second_txid
        )

        assert second_info["confirmations"] >= 1

        assert_equal(
            sender.getreceivedbyaddress(
                return_address
            ),
            return_before + Decimal("0.50"),
        )

        #
        # All three nodes must finish on the exact same active chain.
        #
        final_tip = node0.getbestblockhash()

        assert_equal(
            node1.getbestblockhash(),
            final_tip,
        )

        assert_equal(
            node2.getbestblockhash(),
            final_tip,
        )

        assert_equal(
            node1.getblockcount(),
            103,
        )

        assert_equal(
            node2.getblockcount(),
            103,
        )

        self.log.info(
            "Mercatura native three-node end-to-end PQ network test passed"
        )


if __name__ == "__main__":
    MercaturaThreeNodePQTest(__file__).main()
