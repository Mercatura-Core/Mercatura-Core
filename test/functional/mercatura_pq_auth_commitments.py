#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura PQ Authorization v1 transaction-field commitment test."""

from decimal import Decimal
from io import BytesIO

from test_framework.messages import CTransaction
from test_framework.script import CScript
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaPQAuthCommitmentsTest(BitcoinTestFramework):
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

    def assert_rejected(self, node, label, tx):
        result = node.testmempoolaccept(
            [tx.serialize().hex()]
        )[0]

        self.log.info(
            f"{label}: "
            f"allowed={result['allowed']}, "
            f"reason={result.get('reject-reason')}"
        )

        assert_equal(
            result["allowed"],
            False,
        )

        assert result.get("reject-reason")

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
            "pq-mining"
        )

        receive_address = receiver.getnewaddress(
            "pq-auth-receive"
        )

        assert mining_address.startswith("mcrt1z")
        assert receive_address.startswith("mcrt1z")

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
        # Produce one genuine single-input PQ transaction.
        #
        self.log.info(
            "Creating genuine wallet-signed PQ transaction"
        )

        funded = sender.walletcreatefundedpsbt(
            [
                {
                    "txid": utxo["txid"],
                    "vout": utxo["vout"],
                }
            ],
            [
                {
                    receive_address: Decimal("1.00"),
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

        assert_equal(
            len(valid_tx.vin),
            1,
        )

        assert len(valid_tx.vout) >= 1

        #
        # Establish the valid baseline first.
        #
        baseline = node.testmempoolaccept(
            [valid_hex]
        )[0]

        self.log.info(
            f"Baseline signed transaction: "
            f"allowed={baseline['allowed']}"
        )

        assert_equal(
            baseline["allowed"],
            True,
        )

        #
        # Find the exact receiver output so mutations do not accidentally
        # target wallet change.
        #
        receiver_script_hex = node.validateaddress(
            receive_address
        )["scriptPubKey"]

        receiver_index = None

        for index, output in enumerate(valid_tx.vout):
            if output.scriptPubKey.hex() == receiver_script_hex:
                receiver_index = index
                break

        assert receiver_index is not None

        #
        # Case 1:
        # Change the signed output amount by one Mercatura base unit.
        #
        # Reduce it rather than increase it so the mutation increases the
        # transaction fee and cannot fail because of insufficient fee.
        #
        self.log.info(
            "Testing output-amount commitment"
        )

        amount_mutation = CTransaction(
            valid_tx
        )

        assert amount_mutation.vout[receiver_index].nValue > 1

        amount_mutation.vout[receiver_index].nValue -= 1

        self.assert_rejected(
            node,
            "mutated output amount",
            amount_mutation,
        )

        #
        # Case 2:
        # Change one byte of the receiver's 32-byte PQ commitment.
        #
        # The resulting output remains witness-v2 with exactly 32 program
        # bytes, so this is still structurally a native Mercatura PQ output.
        #
        self.log.info(
            "Testing output-script commitment"
        )

        script_mutation = CTransaction(
            valid_tx
        )

        original_script = bytearray(
            script_mutation
            .vout[receiver_index]
            .scriptPubKey
        )

        assert_equal(
            len(original_script),
            34,
        )

        assert_equal(
            original_script[0],
            0x52,
        )

        assert_equal(
            original_script[1],
            0x20,
        )

        original_script[-1] ^= 0x01

        script_mutation.vout[receiver_index].scriptPubKey = CScript(
            original_script
        )

        self.assert_rejected(
            node,
            "mutated output script",
            script_mutation,
        )

        #
        # Case 3:
        # Change the input sequence while leaving the spent outpoint and all
        # witness authorization bytes untouched.
        #
        self.log.info(
            "Testing input-sequence commitment"
        )

        sequence_mutation = CTransaction(
            valid_tx
        )

        original_sequence = sequence_mutation.vin[0].nSequence

        if original_sequence > 0:
            sequence_mutation.vin[0].nSequence = (
                original_sequence - 1
            )
        else:
            sequence_mutation.vin[0].nSequence = 1

        assert (
            sequence_mutation.vin[0].nSequence
            != original_sequence
        )

        self.assert_rejected(
            node,
            "mutated input sequence",
            sequence_mutation,
        )

        #
        # Case 4:
        # Change nLockTime to a small already-satisfied height.
        #
        # This keeps the transaction final at the current chain height, so a
        # rejection demonstrates authorization commitment rather than an
        # ordinary non-final transaction failure.
        #
        self.log.info(
            "Testing locktime commitment"
        )

        locktime_mutation = CTransaction(
            valid_tx
        )

        original_locktime = locktime_mutation.nLockTime

        locktime_mutation.nLockTime = (
            1 if original_locktime != 1 else 2
        )

        assert (
            locktime_mutation.nLockTime
            != original_locktime
        )

        assert node.getblockcount() > locktime_mutation.nLockTime

        self.assert_rejected(
            node,
            "mutated locktime",
            locktime_mutation,
        )

        #
        # Prove none of the dry-run mutants consumed or poisoned the UTXO.
        #
        self.log.info(
            "Broadcasting untouched PQ transaction"
        )

        txid = node.sendrawtransaction(
            valid_hex
        )

        assert_equal(
            txid,
            valid_tx.txid_hex,
        )

        assert txid in node.getrawmempool()

        confirmation_address = sender.getnewaddress(
            "post-auth-commitment-mining"
        )

        self.generatetoaddress(
            node,
            1,
            confirmation_address,
        )

        assert txid not in node.getrawmempool()

        tx_info = sender.gettransaction(
            txid
        )

        assert tx_info["confirmations"] >= 1

        self.log.info(
            "Mercatura PQ Authorization transaction-field commitments passed"
        )


if __name__ == "__main__":
    MercaturaPQAuthCommitmentsTest(__file__).main()
