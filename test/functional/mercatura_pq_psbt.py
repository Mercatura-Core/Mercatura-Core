#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ PSBT end-to-end functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaPQPSBTTest(BitcoinTestFramework):
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

        self.log.info("Creating PQ sender and receiver wallets")
        node.createwallet(wallet_name="sender")
        node.createwallet(wallet_name="receiver")

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

        assert sender.getbalance() > Decimal("0.00")

        amount = Decimal("1.00")

        self.log.info("Creating funded PQ PSBT")
        funded = sender.walletcreatefundedpsbt(
            [],
            [{receiver_address: amount}],
        )

        assert "psbt" in funded
        assert funded["psbt"]

        self.log.info("Signing PQ PSBT")
        processed = sender.walletprocesspsbt(funded["psbt"])

        assert "psbt" in processed
        assert processed["complete"]

        self.log.info("Finalizing PQ PSBT")
        finalized = node.finalizepsbt(processed["psbt"])

        assert_equal(finalized["complete"], True)
        assert "hex" in finalized

        rawtx = finalized["hex"]

        self.log.info("Checking mempool acceptance")
        acceptance = node.testmempoolaccept([rawtx])

        assert_equal(len(acceptance), 1)
        assert_equal(acceptance[0]["allowed"], True)

        self.log.info("Broadcasting finalized PQ transaction")
        txid = node.sendrawtransaction(rawtx)

        assert txid in node.getrawmempool()

        pending = receiver.getbalances()["mine"]["untrusted_pending"]
        assert_equal(pending, amount)

        self.log.info("Mining PQ transaction confirmation")
        hashes = self.generatetoaddress(
            node,
            1,
            sender_address,
        )

        assert_equal(len(hashes), 1)
        assert txid not in node.getrawmempool()

        assert_equal(receiver.getbalance(), amount)

        tx = receiver.gettransaction(txid)
        assert_equal(tx["confirmations"], 1)

        self.log.info("Mercatura native PQ PSBT round-trip passed")


if __name__ == "__main__":
    MercaturaPQPSBTTest(__file__).main()
