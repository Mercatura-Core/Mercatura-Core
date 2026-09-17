#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ wallet send/receive functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaPQSendReceiveTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

    def mine_blocks_batched(self, node, count, address, batch_size=10):
        """Mine MercaHash regtest blocks in bounded RPC batches."""
        block_hashes = []

        while count > 0:
            batch = min(count, batch_size)

            block_hashes.extend(
                self.generatetoaddress(
                    node,
                    batch,
                    address,
                )
            )

            count -= batch

        return block_hashes

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Creating sender and receiver PQ wallets")
        node.createwallet(wallet_name="sender")
        node.createwallet(wallet_name="receiver")

        sender = node.get_wallet_rpc("sender")
        receiver = node.get_wallet_rpc("receiver")

        sender_address = sender.getnewaddress()
        receiver_address = receiver.getnewaddress()

        self.log.info(f"Sender address:   {sender_address}")
        self.log.info(f"Receiver address: {receiver_address}")

        assert sender_address.startswith("mcrt1z")
        assert receiver_address.startswith("mcrt1z")

        self.log.info("Mining 101 blocks to mature sender coinbase funds")
        block_hashes = self.mine_blocks_batched(
            node,
            101,
            sender_address,
        )

        assert_equal(len(block_hashes), 101)
        assert_equal(node.getblockcount(), 101)

        sender_balance = sender.getbalance()
        self.log.info(f"Sender mature balance: {sender_balance}")

        assert sender_balance > Decimal("0.00")
        assert_equal(receiver.getbalance(), Decimal("0.00"))

        self.log.info("Sending 1.00 MCA from sender to receiver")
        txid = sender.sendtoaddress(
            receiver_address,
            Decimal("1.00"),
        )

        self.log.info(f"Transaction id: {txid}")

        mempool = node.getrawmempool()
        assert txid in mempool

        unconfirmed = receiver.getbalances()["mine"]["untrusted_pending"]
        self.log.info(f"Receiver unconfirmed balance: {unconfirmed}")
        assert_equal(unconfirmed, Decimal("1.00"))

        self.log.info("Mining one block to confirm the PQ transaction")
        confirm_hashes = self.generatetoaddress(
            node,
            1,
            sender_address,
        )

        assert_equal(len(confirm_hashes), 1)
        assert_equal(node.getblockcount(), 102)
        assert txid not in node.getrawmempool()

        receiver_balance = receiver.getbalance()
        self.log.info(f"Receiver confirmed balance: {receiver_balance}")

        assert_equal(receiver_balance, Decimal("1.00"))

        tx = receiver.gettransaction(txid)
        assert_equal(tx["confirmations"], 1)

        self.log.info("Mercatura native PQ send/receive passed")


if __name__ == "__main__":
    MercaturaPQSendReceiveTest(__file__).main()
