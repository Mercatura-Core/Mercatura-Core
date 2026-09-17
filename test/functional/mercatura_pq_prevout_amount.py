#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ external-prevout amount validation test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


class MercaturaPQPrevoutAmountTest(BitcoinTestFramework):
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

    def run_test(self):
        node = self.nodes[0]

        node.createwallet(
            wallet_name="sender",
            load_on_startup=True,
        )

        node.createwallet(
            wallet_name="receiver",
            load_on_startup=True,
        )

        sender = node.get_wallet_rpc(
            "sender"
        )

        receiver = node.get_wallet_rpc(
            "receiver"
        )

        mining_address = sender.getnewaddress(
            "pq-prevout-mining"
        )

        receive_address = receiver.getnewaddress(
            "pq-prevout-receive"
        )

        assert mining_address.startswith("mcrt1z")
        assert receive_address.startswith("mcrt1z")

        #
        # Mature one real native PQ UTXO.
        #
        self.log.info(
            "Mining mature native PQ funding"
        )

        self.mine_blocks_batched(
            node,
            mining_address,
            101,
        )

        utxos = sender.listunspent(100)

        assert len(utxos) > 0

        utxo = utxos[0]

        input_amount = utxo["amount"]

        #
        # Leave a comfortably valid fee for a large ML-DSA-65 witness.
        #
        output_amount = (
            input_amount -
            Decimal("0.20")
        )

        assert output_amount > Decimal("0.00")

        raw = sender.createrawtransaction(
            [
                {
                    "txid": utxo["txid"],
                    "vout": utxo["vout"],
                }
            ],
            [
                {
                    receive_address: output_amount,
                }
            ],
        )

        #
        # Explicit prevout metadata is useful for offline/external signing,
        # but Mercatura PQ Authorization v1 commits to the exact spent amount.
        #
        # A native PQ prevout must therefore never fall through to the
        # inherited MAX_MONEY missing-amount sentinel.
        #
        missing_amount_prevout = [
            {
                "txid": utxo["txid"],
                "vout": utxo["vout"],
                "scriptPubKey": utxo["scriptPubKey"],
            }
        ]

        self.log.info(
            "Checking missing PQ prevout amount rejection"
        )

        assert_raises_rpc_error(
            -3,
            "Missing amount",
            sender.signrawtransactionwithwallet,
            raw,
            missing_amount_prevout,
        )

        #
        # Supplying the exact amount must allow the wallet to construct a
        # valid native PQ authorization.
        #
        exact_prevout = [
            {
                "txid": utxo["txid"],
                "vout": utxo["vout"],
                "scriptPubKey": utxo["scriptPubKey"],
                "amount": input_amount,
            }
        ]

        self.log.info(
            "Signing with exact PQ prevout amount"
        )

        exact_signed = sender.signrawtransactionwithwallet(
            raw,
            exact_prevout,
        )

        assert_equal(
            exact_signed["complete"],
            True,
        )

        exact_acceptance = node.testmempoolaccept(
            [exact_signed["hex"]]
        )[0]

        self.log.info(
            f"Exact-amount authorization: "
            f"allowed={exact_acceptance['allowed']}"
        )

        assert_equal(
            exact_acceptance["allowed"],
            True,
        )

        #
        # Now deliberately lie about the spent amount by one Mercatura base
        # unit. The wallet may sign the supplied external-prevout context,
        # but that authorization must not validate against the real UTXO.
        #
        wrong_prevout = [
            {
                "txid": utxo["txid"],
                "vout": utxo["vout"],
                "scriptPubKey": utxo["scriptPubKey"],
                "amount": input_amount + Decimal("0.01"),
            }
        ]

        self.log.info(
            "Signing with incorrect PQ prevout amount"
        )

        wrong_signed = sender.signrawtransactionwithwallet(
            raw,
            wrong_prevout,
        )

        assert_equal(
            wrong_signed["complete"],
            True,
        )

        wrong_acceptance = node.testmempoolaccept(
            [wrong_signed["hex"]]
        )[0]

        self.log.info(
            f"Wrong-amount authorization: "
            f"allowed={wrong_acceptance['allowed']}, "
            f"reason={wrong_acceptance.get('reject-reason')}"
        )

        assert_equal(
            wrong_acceptance["allowed"],
            False,
        )

        assert wrong_acceptance.get(
            "reject-reason"
        )

        #
        # The untouched exact-amount transaction must remain valid after the
        # failed dry-run and must still broadcast and confirm normally.
        #
        exact_again = node.testmempoolaccept(
            [exact_signed["hex"]]
        )[0]

        assert_equal(
            exact_again["allowed"],
            True,
        )

        self.log.info(
            "Broadcasting exact-amount PQ transaction"
        )

        txid = node.sendrawtransaction(
            exact_signed["hex"]
        )

        assert txid in node.getrawmempool()

        confirmation_address = sender.getnewaddress(
            "post-prevout-mining"
        )

        self.generatetoaddress(
            node,
            1,
            confirmation_address,
        )

        assert txid not in node.getrawmempool()

        received = receiver.gettransaction(
            txid
        )

        assert received["confirmations"] >= 1

        assert_equal(
            receiver.getreceivedbyaddress(
                receive_address
            ),
            output_amount,
        )

        self.log.info(
            "Mercatura native PQ prevout amount validation test passed"
        )


if __name__ == "__main__":
    MercaturaPQPrevoutAmountTest(__file__).main()
