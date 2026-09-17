#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ fee and dust policy functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


BASE_UNIT = Decimal("0.01")
DUST_FLOOR = Decimal("0.02")
MIN_FEE_RATE = Decimal("0.001")


class MercaturaFeePolicyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
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

    def sign_raw_with_wallet(self, node, wallet, rawtx):
        """Convert a raw transaction to PSBT and sign it with the PQ wallet."""
        psbt = node.converttopsbt(rawtx)

        processed = wallet.walletprocesspsbt(psbt)

        assert_equal(processed["complete"], True)

        finalized = node.finalizepsbt(processed["psbt"])

        assert_equal(finalized["complete"], True)

        return finalized["hex"]

    def make_manual_spend(
        self,
        node,
        wallet,
        utxo,
        recipient_address,
        recipient_amount,
        fee,
    ):
        """Build and PQ-sign a transaction with an exact requested fee."""
        change_address = wallet.getrawchangeaddress()

        change_amount = (
            Decimal(str(utxo["amount"]))
            - recipient_amount
            - fee
        )

        assert change_amount > DUST_FLOOR

        rawtx = node.createrawtransaction(
            [
                {
                    "txid": utxo["txid"],
                    "vout": utxo["vout"],
                }
            ],
            {
                recipient_address: recipient_amount,
                change_address: change_amount,
            },
        )

        return self.sign_raw_with_wallet(
            node,
            wallet,
            rawtx,
        )

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

        self.log.info("Mining mature PQ funds")

        self.mine_blocks_batched(
            node,
            101,
            sender_address,
        )

        utxos = sender.listunspent()

        assert len(utxos) >= 1

        utxo = utxos[0]

        #
        # Zero-fee transaction.
        #
        self.log.info("Checking rejection of zero-fee PQ transaction")

        zero_fee_hex = self.make_manual_spend(
            node,
            sender,
            utxo,
            receiver_address,
            Decimal("1.00"),
            Decimal("0.00"),
        )

        zero_fee_result = node.testmempoolaccept(
            [zero_fee_hex]
        )[0]

        assert_equal(zero_fee_result["allowed"], False)

        self.log.info(
            f"Zero-fee rejection reason: "
            f"{zero_fee_result.get('reject-reason')}"
        )

        #
        # One-base-unit output must be dust.
        #
        self.log.info("Checking 0.01 MCA PQ output is dust")

        dust_hex = self.make_manual_spend(
            node,
            sender,
            utxo,
            receiver_address,
            Decimal("0.01"),
            Decimal("0.06"),
        )

        dust_result = node.testmempoolaccept(
            [dust_hex]
        )[0]

        assert_equal(dust_result["allowed"], False)

        self.log.info(
            f"Dust rejection reason: "
            f"{dust_result.get('reject-reason')}"
        )

        assert "dust" in dust_result.get(
            "reject-reason",
            "",
        ).lower()

        #
        # Two-base-unit output is exactly the Mercatura dust boundary.
        #
        self.log.info(
            "Checking 0.02 MCA PQ output is not dust"
        )

        boundary_hex = self.make_manual_spend(
            node,
            sender,
            utxo,
            receiver_address,
            DUST_FLOOR,
            Decimal("0.06"),
        )

        boundary_result = node.testmempoolaccept(
            [boundary_hex]
        )[0]

        self.log.info(
            f"0.02 MCA boundary result: "
            f"allowed={boundary_result['allowed']}, "
            f"reason={boundary_result.get('reject-reason')}"
        )

        assert_equal(boundary_result["allowed"], True)

        #
        # Verify started-1000-byte fee calculation with real PQ authorization.
        #
        self.log.info(
            "Creating PQ transaction at Mercatura minimum fee rate"
        )

        funded = sender.walletcreatefundedpsbt(
            [],
            [
                {
                    receiver_address: Decimal("1.00"),
                }
            ],
            0,
            {
                "fee_rate": MIN_FEE_RATE,
            },
        )

        assert funded["fee"] > Decimal("0.00")

        processed = sender.walletprocesspsbt(
            funded["psbt"]
        )

        assert_equal(processed["complete"], True)

        finalized = node.finalizepsbt(
            processed["psbt"]
        )

        assert_equal(finalized["complete"], True)

        valid_hex = finalized["hex"]

        accepted = node.testmempoolaccept(
            [valid_hex]
        )[0]

        assert_equal(accepted["allowed"], True)

        effective_size = accepted["vsize"]

        expected_fee_base_units = (
            effective_size + 999
        ) // 1000

        expected_fee = (
            Decimal(expected_fee_base_units)
            * BASE_UNIT
        )

        actual_fee = Decimal(str(funded["fee"]))

        self.log.info(
            f"Mercatura effective fee size: "
            f"{effective_size} bytes"
        )

        self.log.info(
            f"Expected minimum fee: "
            f"{expected_fee} MCA"
        )

        self.log.info(
            f"Wallet-created fee: "
            f"{actual_fee} MCA"
        )

        assert_equal(
            actual_fee,
            expected_fee,
        )

        #
        # Broadcast and confirm the correctly-fee'd transaction.
        #
        self.log.info(
            "Broadcasting correctly fee'd PQ transaction"
        )

        txid = node.sendrawtransaction(
            valid_hex
        )

        assert txid in node.getrawmempool()

        self.generatetoaddress(
            node,
            1,
            sender_address,
        )

        assert txid not in node.getrawmempool()

        tx = receiver.gettransaction(txid)

        assert_equal(
            tx["confirmations"],
            1,
        )

        self.log.info(
            "Mercatura native fee and dust policy passed"
        )


if __name__ == "__main__":
    MercaturaFeePolicyTest(__file__).main()
