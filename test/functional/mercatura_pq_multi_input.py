#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native multi-input PQ Authorization v1 functional test."""

from decimal import Decimal
from io import BytesIO

from test_framework.messages import CTransaction
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


PQ_SIGNATURE_SIZE = 3309
PQ_PUBLIC_KEY_SIZE = 1952


class MercaturaPQMultiInputTest(BitcoinTestFramework):
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
    def tx_from_hex(raw_hex):
        tx = CTransaction()

        tx.deserialize(
            BytesIO(bytes.fromhex(raw_hex))
        )

        return tx

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
            "pq-multi-input-mining"
        )

        receive_address = receiver.getnewaddress(
            "pq-multi-input-receive"
        )

        assert mining_address.startswith("mcrt1z")
        assert receive_address.startswith("mcrt1z")

        #
        # Mine enough blocks for at least two distinct PQ coinbase outputs
        # to become spendable.
        #
        self.log.info(
            "Mining two mature native PQ funding outputs"
        )

        self.mine_blocks_batched(
            node,
            mining_address,
            102,
        )

        spendable = sender.listunspent(100)

        assert len(spendable) >= 2

        first_utxo = spendable[0]
        second_utxo = spendable[1]

        assert (
            first_utxo["txid"],
            first_utxo["vout"],
        ) != (
            second_utxo["txid"],
            second_utxo["vout"],
        )

        #
        # Explicitly force two independent PQ inputs into one transaction.
        #
        self.log.info(
            "Creating two-input native PQ PSBT"
        )

        funded = sender.walletcreatefundedpsbt(
            [
                {
                    "txid": first_utxo["txid"],
                    "vout": first_utxo["vout"],
                },
                {
                    "txid": second_utxo["txid"],
                    "vout": second_utxo["vout"],
                },
            ],
            [
                {
                    receive_address: Decimal("2.00"),
                }
            ],
            0,
            {
                "add_inputs": False,
            },
        )

        processed = sender.walletprocesspsbt(
            psbt=funded["psbt"],
            finalize=False,
        )

        assert "psbt" in processed

        finalized = node.finalizepsbt(
            processed["psbt"]
        )

        assert_equal(
            finalized["complete"],
            True,
        )

        valid_hex = finalized["hex"]

        valid_tx = self.tx_from_hex(
            valid_hex
        )

        #
        # The resulting transaction must contain exactly the two explicit
        # native PQ inputs.
        #
        assert_equal(
            len(valid_tx.vin),
            2,
        )

        assert_equal(
            len(valid_tx.wit.vtxinwit),
            2,
        )

        for index, witness in enumerate(
            valid_tx.wit.vtxinwit
        ):
            stack = witness.scriptWitness.stack

            assert_equal(
                len(stack),
                2,
            )

            assert_equal(
                len(stack[0]),
                PQ_SIGNATURE_SIZE,
            )

            assert_equal(
                len(stack[1]),
                PQ_PUBLIC_KEY_SIZE,
            )

            self.log.info(
                f"PQ input {index}: "
                f"signature={len(stack[0])} bytes, "
                f"public_key={len(stack[1])} bytes"
            )

        #
        # Establish that both PQ authorizations verify together.
        #
        baseline = node.testmempoolaccept(
            [valid_hex]
        )[0]

        self.log.info(
            f"Two-input PQ baseline: "
            f"allowed={baseline['allowed']}"
        )

        assert_equal(
            baseline["allowed"],
            True,
        )

        valid_txid = valid_tx.txid_hex
        valid_wtxid = valid_tx.wtxid_hex

        #
        # Mutate only input 1's ML-DSA signature.
        #
        # Because witness data is changed but the base transaction is not,
        # the txid must remain unchanged while the wtxid changes.
        #
        self.log.info(
            "Corrupting second PQ input signature"
        )

        mutated = CTransaction(
            valid_tx
        )

        signature = bytearray(
            mutated
            .wit
            .vtxinwit[1]
            .scriptWitness
            .stack[0]
        )

        signature[0] ^= 0x01

        mutated.wit.vtxinwit[1].scriptWitness.stack[0] = bytes(
            signature
        )

        assert_equal(
            len(
                mutated
                .wit
                .vtxinwit[1]
                .scriptWitness
                .stack[0]
            ),
            PQ_SIGNATURE_SIZE,
        )

        assert_equal(
            mutated.txid_hex,
            valid_txid,
        )

        assert mutated.wtxid_hex != valid_wtxid

        mutated_result = node.testmempoolaccept(
            [mutated.serialize().hex()]
        )[0]

        self.log.info(
            f"Corrupted second-input signature: "
            f"allowed={mutated_result['allowed']}, "
            f"reason={mutated_result.get('reject-reason')}"
        )

        assert_equal(
            mutated_result["allowed"],
            False,
        )

        assert mutated_result.get("reject-reason")

        #
        # The failed dry-run must not affect the original transaction.
        #
        second_baseline = node.testmempoolaccept(
            [valid_hex]
        )[0]

        assert_equal(
            second_baseline["allowed"],
            True,
        )

        #
        # Broadcast and confirm the untouched transaction.
        #
        self.log.info(
            "Broadcasting valid two-input PQ transaction"
        )

        txid = node.sendrawtransaction(
            valid_hex
        )

        assert_equal(
            txid,
            valid_txid,
        )

        assert txid in node.getrawmempool()

        confirmation_address = sender.getnewaddress(
            "post-multi-input-mining"
        )

        self.generatetoaddress(
            node,
            1,
            confirmation_address,
        )

        assert txid not in node.getrawmempool()

        receiver_info = receiver.gettransaction(
            txid
        )

        assert receiver_info["confirmations"] >= 1

        assert_equal(
            receiver.getreceivedbyaddress(
                receive_address
            ),
            Decimal("2.00"),
        )

        self.log.info(
            "Mercatura native multi-input PQ authorization test passed"
        )


if __name__ == "__main__":
    MercaturaPQMultiInputTest(__file__).main()
