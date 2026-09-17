#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native reindex persistence and spendability functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaReindexTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
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
        node = self.nodes[0]

        self.log.info("Creating persistent PQ wallets")
        node.createwallet(
            wallet_name="sender",
            load_on_startup=True,
        )
        node.createwallet(
            wallet_name="receiver",
            load_on_startup=True,
        )

        sender = node.get_wallet_rpc("sender")
        receiver = node.get_wallet_rpc("receiver")

        sender_address = sender.getnewaddress()
        receiver_address = receiver.getnewaddress()

        assert sender_address.startswith("mcrt1z")
        assert receiver_address.startswith("mcrt1z")

        self.log.info("Mining mature sender funds")
        self.mine_blocks_batched(
            node,
            101,
            sender_address,
        )

        assert_equal(node.getblockcount(), 101)
        assert sender.getbalance() > Decimal("0.00")

        self.log.info("Creating confirmed PQ spend before reindex")
        txid_before = sender.sendtoaddress(
            receiver_address,
            Decimal("1.00"),
        )

        assert txid_before in node.getrawmempool()

        confirm_hashes = self.generatetoaddress(
            node,
            1,
            sender_address,
        )

        assert_equal(len(confirm_hashes), 1)
        assert txid_before not in node.getrawmempool()

        height_before = node.getblockcount()
        tip_before = node.getbestblockhash()
        sender_balance_before = sender.getbalance()
        receiver_balance_before = receiver.getbalance()

        self.log.info(f"Height before reindex: {height_before}")
        self.log.info(f"Tip before reindex: {tip_before}")
        self.log.info(f"Sender balance before reindex: {sender_balance_before}")
        self.log.info(f"Receiver balance before reindex: {receiver_balance_before}")

        assert_equal(height_before, 102)
        assert_equal(receiver_balance_before, Decimal("1.00"))

        self.log.info("Restarting Mercatura node with -reindex")
        self.restart_node(
            0,
            extra_args=["-reindex"],
        )

        node = self.nodes[0]

        assert "sender" in node.listwallets()
        assert "receiver" in node.listwallets()

        sender = node.get_wallet_rpc("sender")
        receiver = node.get_wallet_rpc("receiver")

        height_after = node.getblockcount()
        tip_after = node.getbestblockhash()
        sender_balance_after = sender.getbalance()
        receiver_balance_after = receiver.getbalance()

        self.log.info(f"Height after reindex: {height_after}")
        self.log.info(f"Tip after reindex: {tip_after}")
        self.log.info(f"Sender balance after reindex: {sender_balance_after}")
        self.log.info(f"Receiver balance after reindex: {receiver_balance_after}")

        assert_equal(height_after, height_before)
        assert_equal(tip_after, tip_before)
        assert_equal(sender_balance_after, sender_balance_before)
        assert_equal(receiver_balance_after, receiver_balance_before)

        sender_info = sender.getaddressinfo(sender_address)
        receiver_info = receiver.getaddressinfo(receiver_address)

        assert_equal(sender_info["ismine"], True)
        assert_equal(receiver_info["ismine"], True)

        self.log.info("Creating a second PQ spend after reindex")
        receiver_after_address = receiver.getnewaddress()

        assert receiver_after_address.startswith("mcrt1z")

        txid_after = sender.sendtoaddress(
            receiver_after_address,
            Decimal("0.50"),
        )

        assert txid_after in node.getrawmempool()

        self.log.info("Confirming post-reindex PQ spend")
        post_reindex_hashes = self.generatetoaddress(
            node,
            1,
            sender_address,
        )

        assert_equal(len(post_reindex_hashes), 1)
        assert txid_after not in node.getrawmempool()

        tx_after = receiver.gettransaction(txid_after)
        assert_equal(tx_after["confirmations"], 1)

        self.log.info("Mercatura native reindex persistence and spendability passed")


if __name__ == "__main__":
    MercaturaReindexTest(__file__).main()
