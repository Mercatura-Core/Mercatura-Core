#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native invalid PQ Authorization v1 functional test."""

from decimal import Decimal
from io import BytesIO

from test_framework.messages import CTransaction
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


PQ_SIGNATURE_SIZE = 3309
PQ_PUBLIC_KEY_SIZE = 1952


class MercaturaPQInvalidAuthTest(BitcoinTestFramework):
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

        #
        # Create independent sender and receiver PQ wallets.
        #
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
            "pq-invalid-auth-receive"
        )

        assert mining_address.startswith("mcrt1z")
        assert receive_address.startswith("mcrt1z")

        #
        # Mature one or more native PQ coinbase outputs.
        #
        self.log.info(
            "Mining mature native PQ funding"
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
        # Force exactly one known PQ input so witness mutations remain
        # simple and deterministic.
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

        assert "psbt" in processed

        finalized = node.finalizepsbt(
            processed["psbt"]
        )

        assert_equal(
            finalized["complete"],
            True,
        )

        assert "hex" in finalized

        valid_hex = finalized["hex"]

        valid_tx = self.tx_from_hex(
            valid_hex
        )

        #
        # Explicitly prove the baseline transaction is valid before making
        # any witness mutations.
        #
        baseline = node.testmempoolaccept(
            [valid_hex]
        )[0]

        self.log.info(
            f"Baseline PQ transaction: "
            f"allowed={baseline['allowed']}"
        )

        assert_equal(
            baseline["allowed"],
            True,
        )

        assert_equal(
            len(valid_tx.vin),
            1,
        )

        assert_equal(
            len(valid_tx.wit.vtxinwit),
            1,
        )

        stack = (
            valid_tx
            .wit
            .vtxinwit[0]
            .scriptWitness
            .stack
        )

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

        #
        # Case 1:
        # Keep the exact signature length but alter one byte.
        #
        # Structure remains canonical, so authorization must fail at
        # ML-DSA signature verification.
        #
        self.log.info(
            "Testing same-size corrupted ML-DSA signature"
        )

        bad_signature = CTransaction(
            valid_tx
        )

        signature = bytearray(
            bad_signature
            .wit
            .vtxinwit[0]
            .scriptWitness
            .stack[0]
        )

        signature[0] ^= 0x01

        bad_signature.wit.vtxinwit[0].scriptWitness.stack[0] = bytes(
            signature
        )

        assert_equal(
            len(
                bad_signature
                .wit
                .vtxinwit[0]
                .scriptWitness
                .stack[0]
            ),
            PQ_SIGNATURE_SIZE,
        )

        self.assert_rejected(
            node,
            "corrupted signature",
            bad_signature,
        )

        #
        # Case 2:
        # Keep the public-key size exact but alter one byte.
        #
        # The witness-v2 output commits to the original public key, so this
        # must fail the PQ key-commitment relationship.
        #
        self.log.info(
            "Testing same-size corrupted ML-DSA public key"
        )

        bad_pubkey = CTransaction(
            valid_tx
        )

        public_key = bytearray(
            bad_pubkey
            .wit
            .vtxinwit[0]
            .scriptWitness
            .stack[1]
        )

        public_key[0] ^= 0x01

        bad_pubkey.wit.vtxinwit[0].scriptWitness.stack[1] = bytes(
            public_key
        )

        assert_equal(
            len(
                bad_pubkey
                .wit
                .vtxinwit[0]
                .scriptWitness
                .stack[1]
            ),
            PQ_PUBLIC_KEY_SIZE,
        )

        self.assert_rejected(
            node,
            "corrupted public key commitment",
            bad_pubkey,
        )

        #
        # Case 3:
        # Native PQ Authorization v1 requires exactly two witness elements.
        #
        self.log.info(
            "Testing missing PQ public-key witness element"
        )

        missing_element = CTransaction(
            valid_tx
        )

        missing_element.wit.vtxinwit[0].scriptWitness.stack.pop()

        assert_equal(
            len(
                missing_element
                .wit
                .vtxinwit[0]
                .scriptWitness
                .stack
            ),
            1,
        )

        self.assert_rejected(
            node,
            "missing witness element",
            missing_element,
        )

        #
        # Case 4:
        # Extra witness elements are also forbidden.
        #
        self.log.info(
            "Testing extra PQ witness element"
        )

        extra_element = CTransaction(
            valid_tx
        )

        extra_element.wit.vtxinwit[0].scriptWitness.stack.append(
            b"\x00"
        )

        assert_equal(
            len(
                extra_element
                .wit
                .vtxinwit[0]
                .scriptWitness
                .stack
            ),
            3,
        )

        self.assert_rejected(
            node,
            "extra witness element",
            extra_element,
        )

        #
        # Case 5:
        # Signature length is consensus-exact at 3309 bytes.
        #
        self.log.info(
            "Testing shortened PQ signature"
        )

        short_signature = CTransaction(
            valid_tx
        )

        short_signature.wit.vtxinwit[0].scriptWitness.stack[0] = (
            short_signature
            .wit
            .vtxinwit[0]
            .scriptWitness
            .stack[0][:-1]
        )

        assert_equal(
            len(
                short_signature
                .wit
                .vtxinwit[0]
                .scriptWitness
                .stack[0]
            ),
            PQ_SIGNATURE_SIZE - 1,
        )

        self.assert_rejected(
            node,
            "short signature",
            short_signature,
        )

        #
        # None of the dry-run rejection checks may poison or consume the
        # underlying UTXO. Broadcast the untouched genuine transaction and
        # confirm it normally.
        #
        self.log.info(
            "Broadcasting untouched valid PQ transaction"
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
            "post-invalid-auth-mining"
        )

        self.generatetoaddress(
            node,
            1,
            confirmation_address,
        )

        assert txid not in node.getrawmempool()

        transaction = sender.gettransaction(
            txid
        )

        assert transaction["confirmations"] >= 1

        self.log.info(
            "Mercatura invalid PQ Authorization v1 functional test passed"
        )


if __name__ == "__main__":
    MercaturaPQInvalidAuthTest(__file__).main()
