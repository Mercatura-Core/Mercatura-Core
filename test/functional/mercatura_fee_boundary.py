#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura full-byte PQ fee boundary regression test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


EXPECTED_SIZE = 6001
EXPECTED_FEE = Decimal("0.07")
PQ_OUTPUT_COUNT = 14
DATA_PAYLOAD_BYTES = 67


class MercaturaFeeBoundaryTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.rpc_timeout = 180

        self.extra_args = [[
            "-fallbackfee=0.01",
        ]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine_batched(self, node, count, address):
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

        sender = node.get_wallet_rpc("sender")
        receiver = node.get_wallet_rpc("receiver")

        mining_address = sender.getnewaddress(
            "fee-boundary-funding"
        )

        assert mining_address.startswith("mcrt1z")

        #
        # At height 101 exactly the first coinbase is mature. This gives us
        # one known native PQ input without needing wallet coin selection.
        #
        self.log.info(
            "Mining one mature native PQ coinbase"
        )

        self.mine_batched(
            node,
            101,
            mining_address,
        )

        mature = [
            utxo
            for utxo in sender.listunspent(101)
            if utxo.get("address") == mining_address
        ]

        assert_equal(len(mature), 1)

        funding = mature[0]
        input_amount = funding["amount"]

        #
        # Construct the exact serialized-size boundary:
        #
        #   native PQ input                         5,309 bytes
        #   static witness transaction overhead       12 bytes
        #   14 native PQ outputs: 14 * 43            602 bytes
        #   67-byte OP_RETURN output                   78 bytes
        #                                            -----
        #                                           6,001 bytes
        #
        # Before the production fix, tx_noinputs_size was one byte short
        # and the wallet estimated this as exactly 6,000 bytes.
        #
        destinations = [
            receiver.getnewaddress(
                f"fee-boundary-{i}"
            )
            for i in range(PQ_OUTPUT_COUNT)
        ]

        for address in destinations:
            assert address.startswith("mcrt1z")

        #
        # Monetary outputs sum exactly to the selected input before fees.
        # subtractFeeFromOutputs therefore prevents creation of a change
        # output, which keeps the serialized-size fixture deterministic.
        #
        small_output = Decimal("1.00")

        first_output_amount = (
            input_amount
            - small_output * (PQ_OUTPUT_COUNT - 1)
        )

        outputs = [
            {destinations[0]: first_output_amount}
        ]

        outputs.extend(
            {address: small_output}
            for address in destinations[1:]
        )

        outputs.append({
            "data": "42" * DATA_PAYLOAD_BYTES
        })

        self.log.info(
            "Creating exact 6,001-byte PQ fee-boundary PSBT"
        )

        funded = sender.walletcreatefundedpsbt(
            [{
                "txid": funding["txid"],
                "vout": funding["vout"],
            }],
            outputs,
            0,
            {
                "add_inputs": False,
                "subtractFeeFromOutputs": [0],
            },
            True,
        )

        #
        # No change output may be introduced, otherwise the byte-boundary
        # fixture would no longer be the transaction we designed.
        #
        assert_equal(
            funded["changepos"],
            -1,
        )

        self.log.info(
            f"Wallet-calculated fee: {funded['fee']} MCA"
        )

        assert_equal(
            funded["fee"],
            EXPECTED_FEE,
        )

        processed = sender.walletprocesspsbt(
            funded["psbt"]
        )

        assert processed["complete"]

        finalized = node.finalizepsbt(
            processed["psbt"]
        )

        assert finalized["complete"]

        tx_hex = finalized["hex"]

        decoded = node.decoderawtransaction(
            tx_hex
        )

        self.log.info(
            f"Final serialized size: {decoded['size']} bytes"
        )

        assert_equal(
            decoded["size"],
            EXPECTED_SIZE,
        )

        #
        # Mercatura's fee metric deliberately has no witness discount.
        #
        assert_equal(
            decoded["vsize"],
            EXPECTED_SIZE,
        )

        assert_equal(
            len(decoded["vin"]),
            1,
        )

        assert_equal(
            len(decoded["vout"]),
            PQ_OUTPUT_COUNT + 1,
        )

        witness = decoded["vin"][0][
            "txinwitness"
        ]

        assert_equal(
            len(witness),
            2,
        )

        assert_equal(
            len(bytes.fromhex(witness[0])),
            3309,
        )

        assert_equal(
            len(bytes.fromhex(witness[1])),
            1952,
        )

        #
        # 6,001 full bytes require seven started 1,000-byte fee units:
        #
        #   ceil(6001 / 1000) * 0.01 MCA = 0.07 MCA
        #
        acceptance = node.testmempoolaccept(
            [tx_hex]
        )[0]

        if not acceptance["allowed"]:
            raise AssertionError(
                f"Fee-boundary transaction rejected: "
                f"{acceptance}"
            )

        txid = node.sendrawtransaction(
            tx_hex
        )

        assert txid in node.getrawmempool()

        #
        # Confirm it and verify the recipient side sees the transaction.
        #
        confirm_address = sender.getnewaddress(
            "fee-boundary-confirm"
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

        assert received["confirmations"] >= 1

        self.log.info(
            "Mercatura 6,001-byte full-fee boundary test passed"
        )


if __name__ == "__main__":
    MercaturaFeeBoundaryTest(__file__).main()
