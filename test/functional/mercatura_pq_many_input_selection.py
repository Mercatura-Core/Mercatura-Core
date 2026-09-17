#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native many-input PQ wallet coin-selection test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


MAX_STANDARD_TX_WEIGHT = 400_000
EXPECTED_PQ_INPUTS = 73
MATURE_OUTPUTS = 74


class MercaturaPQManyInputSelectionTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

        self.extra_args = [[
            "-fallbackfee=0.01",
            "-avoidpartialspends=1",
        ]]

        self.rpc_timeout = 180

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine_batched(
        self,
        node,
        address,
        count,
    ):
        remaining = count

        while remaining:
            batch = min(
                10,
                remaining,
            )

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

        #
        # Every coinbase is deliberately paid to exactly the same native PQ
        # script so -avoidpartialspends groups the mature outputs together.
        #
        funding_address = sender.getnewaddress(
            "many-input-funding"
        )

        receive_address = receiver.getnewaddress(
            "many-input-receiver"
        )

        assert funding_address.startswith(
            "mcrt1z"
        )

        assert receive_address.startswith(
            "mcrt1z"
        )

        self.log.info(
            "Mining 74 mature same-script PQ coinbases"
        )

        #
        # At height 174, coinbases 1 through 74 have at least 101
        # confirmations and are spendable under Mercatura's maturity rule.
        #
        self.mine_batched(
            node,
            funding_address,
            174,
        )

        assert_equal(
            node.getblockcount(),
            174,
        )

        mature = sender.listunspent(
            101
        )

        same_script = [
            utxo
            for utxo in mature
            if utxo.get("address") ==
            funding_address
        ]

        self.log.info(
            f"Mature same-script UTXOs: "
            f"{len(same_script)}"
        )

        assert_equal(
            len(same_script),
            MATURE_OUTPUTS,
        )

        #
        # The payment is deliberately larger than every individual UTXO.
        # Therefore the one-output partial group cannot fund it.
        #
        largest_single = max(
            utxo["amount"]
            for utxo in same_script
        )

        payment = (
            largest_single +
            Decimal("1.00")
        )

        self.log.info(
            f"Largest single UTXO: "
            f"{largest_single} MCA"
        )

        self.log.info(
            f"Requested payment: "
            f"{payment} MCA"
        )

        #
        # Let the real wallet perform coin selection, fee calculation,
        # ML-DSA signing, and mempool submission.
        #
        self.log.info(
            "Creating wallet-selected many-input PQ transaction"
        )

        txid = sender.sendtoaddress(
            receive_address,
            payment,
            fee_rate=Decimal("0.001"),
        )

        assert txid in node.getrawmempool()

        raw = sender.gettransaction(
            txid
        )["hex"]

        decoded = node.decoderawtransaction(
            raw
        )

        input_count = len(
            decoded["vin"]
        )

        self.log.info(
            f"Wallet selected {input_count} PQ inputs"
        )

        #
        # With weight-aware same-script grouping, the first group contains
        # exactly 73 native PQ inputs. The 74th output is placed in its own
        # group because adding it would exceed the transaction weight budget.
        #
        assert_equal(
            input_count,
            EXPECTED_PQ_INPUTS,
        )

        #
        # The fully signed transaction itself must remain standard-weight.
        #
        self.log.info(
            f"Signed transaction weight: "
            f"{decoded['weight']} WU"
        )

        assert (
            decoded["weight"] <=
            MAX_STANDARD_TX_WEIGHT
        )

        #
        # Mercatura fee accounting uses full serialized bytes rather than
        # Bitcoin's witness-discount vsize.
        #
        assert_equal(
            decoded["vsize"],
            decoded["size"],
        )

        #
        # Every selected native PQ input must carry the canonical
        # ML-DSA-65 witness:
        #
        #   [3309-byte signature, 1952-byte public key]
        #
        for vin in decoded["vin"]:
            witness = vin.get(
                "txinwitness"
            )

            assert witness is not None

            assert_equal(
                len(witness),
                2,
            )

            assert_equal(
                len(bytes.fromhex(
                    witness[0]
                )),
                3309,
            )

            assert_equal(
                len(bytes.fromhex(
                    witness[1]
                )),
                1952,
            )

        #
        # Receiver should see the broadcast transaction as pending.
        #
        pending = receiver.getbalances()[
            "mine"
        ]["untrusted_pending"]

        assert_equal(
            pending,
            payment,
        )

        #
        # Confirm the large PQ transaction and prove the normal wallet path
        # remains healthy afterward.
        #
        self.log.info(
            "Mining many-input PQ transaction"
        )

        confirm_address = sender.getnewaddress(
            "many-input-confirm"
        )

        self.generatetoaddress(
            node,
            1,
            confirm_address,
        )

        assert txid not in node.getrawmempool()

        received = receiver.gettransaction(
            txid
        )

        assert received[
            "confirmations"
        ] >= 1

        assert_equal(
            receiver.getreceivedbyaddress(
                receive_address
            ),
            payment,
        )

        self.log.info(
            "Mercatura native many-input PQ coin-selection test passed"
        )


if __name__ == "__main__":
    MercaturaPQManyInputSelectionTest(__file__).main()
