#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ mempool conflict and RBF replacement test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


LOW_FEE_RATE = Decimal("0.002")
HIGH_FEE_RATE = Decimal("0.008")


class MercaturaPQRBFTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

        self.extra_args = [[
            "-fallbackfee=0.01",
        ]]

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

    @staticmethod
    def create_signed_psbt(
        wallet,
        node,
        *,
        txid,
        vout,
        destination,
        fee_rate,
    ):
        funded = wallet.walletcreatefundedpsbt(
            [
                {
                    "txid": txid,
                    "vout": vout,
                }
            ],
            [
                {
                    destination: Decimal("1.00"),
                }
            ],
            0,
            {
                "add_inputs": False,
                "replaceable": True,
                "fee_rate": fee_rate,
            },
        )

        processed = wallet.walletprocesspsbt(
            psbt=funded["psbt"],
            finalize=False,
        )

        finalized = node.finalizepsbt(
            processed["psbt"]
        )

        assert_equal(
            finalized["complete"],
            True,
        )

        return {
            "hex": finalized["hex"],
            "fee": funded["fee"],
        }

    def run_test(self):
        node = self.nodes[0]

        node.createwallet(
            wallet_name="sender",
            load_on_startup=True,
        )

        node.createwallet(
            wallet_name="receiver_a",
            load_on_startup=True,
        )

        node.createwallet(
            wallet_name="receiver_b",
            load_on_startup=True,
        )

        sender = node.get_wallet_rpc("sender")
        receiver_a = node.get_wallet_rpc("receiver_a")
        receiver_b = node.get_wallet_rpc("receiver_b")

        mining_address = sender.getnewaddress(
            "pq-rbf-mining"
        )

        destination_a = receiver_a.getnewaddress(
            "first-rbf-destination"
        )

        destination_b = receiver_b.getnewaddress(
            "replacement-rbf-destination"
        )

        assert mining_address.startswith("mcrt1z")
        assert destination_a.startswith("mcrt1z")
        assert destination_b.startswith("mcrt1z")

        #
        # Mature a native PQ UTXO.
        #
        self.log.info(
            "Mining mature PQ funding"
        )

        self.mine_blocks_batched(
            node,
            mining_address,
            101,
        )

        spendable = sender.listunspent(100)

        assert len(spendable) > 0

        utxo = spendable[0]

        #
        # Construct both transactions before broadcasting either one.
        # They deliberately spend the exact same native PQ outpoint.
        #
        self.log.info(
            "Creating low-fee replaceable PQ transaction"
        )

        first = self.create_signed_psbt(
            sender,
            node,
            txid=utxo["txid"],
            vout=utxo["vout"],
            destination=destination_a,
            fee_rate=LOW_FEE_RATE,
        )

        self.log.info(
            "Creating higher-fee PQ replacement"
        )

        replacement = self.create_signed_psbt(
            sender,
            node,
            txid=utxo["txid"],
            vout=utxo["vout"],
            destination=destination_b,
            fee_rate=HIGH_FEE_RATE,
        )

        self.log.info(
            f"Original fee: {first['fee']} MCA; "
            f"replacement fee: {replacement['fee']} MCA"
        )

        assert replacement["fee"] > first["fee"]

        #
        # Both transactions are individually valid against the chain UTXO
        # before either reaches the mempool.
        #
        first_acceptance = node.testmempoolaccept(
            [first["hex"]]
        )[0]

        replacement_acceptance = node.testmempoolaccept(
            [replacement["hex"]]
        )[0]

        assert_equal(
            first_acceptance["allowed"],
            True,
        )

        assert_equal(
            replacement_acceptance["allowed"],
            True,
        )

        #
        # Put the low-fee transaction in the mempool.
        #
        self.log.info(
            "Broadcasting original PQ transaction"
        )

        first_txid = node.sendrawtransaction(
            first["hex"]
        )

        mempool = node.getrawmempool()

        assert first_txid in mempool

        assert_equal(
            len(mempool),
            1,
        )

        #
        # The higher-fee transaction spends the same input and signals
        # replacement. It must replace the original transaction.
        #
        self.log.info(
            "Broadcasting higher-fee PQ replacement"
        )

        replacement_txid = node.sendrawtransaction(
            replacement["hex"]
        )

        assert replacement_txid != first_txid

        mempool = node.getrawmempool()

        assert replacement_txid in mempool
        assert first_txid not in mempool

        assert_equal(
            len(mempool),
            1,
        )

        #
        # Verify the surviving transaction actually pays receiver B rather
        # than the destination from the evicted original transaction.
        #
        replacement_decoded = node.getrawtransaction(
            replacement_txid,
            True,
        )

        replacement_scripts = {
            output["scriptPubKey"]["hex"]
            for output in replacement_decoded["vout"]
        }

        destination_a_script = node.validateaddress(
            destination_a
        )["scriptPubKey"]

        destination_b_script = node.validateaddress(
            destination_b
        )["scriptPubKey"]

        assert destination_b_script in replacement_scripts
        assert destination_a_script not in replacement_scripts

        #
        # Mine the replacement and verify only it confirms.
        #
        self.log.info(
            "Mining replacement transaction"
        )

        confirmation_address = sender.getnewaddress(
            "post-rbf-mining"
        )

        self.generatetoaddress(
            node,
            1,
            confirmation_address,
        )

        assert_equal(
            node.getrawmempool(),
            [],
        )

        replacement_info = receiver_b.gettransaction(
            replacement_txid
        )

        assert replacement_info["confirmations"] >= 1

        assert_equal(
            receiver_b.getreceivedbyaddress(
                destination_b
            ),
            Decimal("1.00"),
        )

        assert_equal(
            receiver_a.getreceivedbyaddress(
                destination_a
            ),
            Decimal("0.00"),
        )

        self.log.info(
            "Mercatura native PQ RBF replacement test passed"
        )


if __name__ == "__main__":
    MercaturaPQRBFTest(__file__).main()
