#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ multi-output batching functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaPQBatchingTest(BitcoinTestFramework):
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

        self.log.info("Creating sender and receiver PQ wallets")
        node.createwallet(wallet_name="sender")
        node.createwallet(wallet_name="receiver_a")
        node.createwallet(wallet_name="receiver_b")

        sender = node.get_wallet_rpc("sender")
        receiver_a = node.get_wallet_rpc("receiver_a")
        receiver_b = node.get_wallet_rpc("receiver_b")

        sender_address = sender.getnewaddress()
        receiver_a_address = receiver_a.getnewaddress()
        receiver_b_address = receiver_b.getnewaddress()

        assert sender_address.startswith("mcrt1z")
        assert receiver_a_address.startswith("mcrt1z")
        assert receiver_b_address.startswith("mcrt1z")

        self.log.info("Mining mature sender funds")
        self.mine_blocks_batched(
            node,
            101,
            sender_address,
        )

        assert sender.getbalance() > Decimal("0.00")

        amount_a = Decimal("0.50")
        amount_b = Decimal("0.75")

        self.log.info("Creating one PQ transaction with two recipient outputs")
        txid = sender.sendmany(
            "",
            {
                receiver_a_address: amount_a,
                receiver_b_address: amount_b,
            },
        )

        self.log.info(f"Batch transaction id: {txid}")
        assert txid in node.getrawmempool()

        pending_a = receiver_a.getbalances()["mine"]["untrusted_pending"]
        pending_b = receiver_b.getbalances()["mine"]["untrusted_pending"]

        assert_equal(pending_a, amount_a)
        assert_equal(pending_b, amount_b)

        decoded = node.getrawtransaction(txid, True)

        matched_a = False
        matched_b = False

        for output in decoded["vout"]:
            value = output["value"]
            addresses = output["scriptPubKey"].get("address")

            if addresses == receiver_a_address and value == amount_a:
                matched_a = True

            if addresses == receiver_b_address and value == amount_b:
                matched_b = True

        assert matched_a
        assert matched_b

        self.log.info("Mining batch transaction confirmation")
        hashes = self.generatetoaddress(
            node,
            1,
            sender_address,
        )

        assert_equal(len(hashes), 1)
        assert txid not in node.getrawmempool()

        assert_equal(receiver_a.getbalance(), amount_a)
        assert_equal(receiver_b.getbalance(), amount_b)

        tx_a = receiver_a.gettransaction(txid)
        tx_b = receiver_b.gettransaction(txid)

        assert_equal(tx_a["confirmations"], 1)
        assert_equal(tx_b["confirmations"], 1)

        self.log.info("Mercatura native PQ multi-output batching passed")


if __name__ == "__main__":
    MercaturaPQBatchingTest(__file__).main()
