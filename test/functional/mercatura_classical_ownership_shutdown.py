#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native classical-ownership shutdown functional test."""

from test_framework.key import ECKey
from test_framework.messages import (
    CBlock,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
    from_hex,
)
from test_framework.script import (
    CScript,
    OP_0,
    OP_CHECKSIG,
    SIGHASH_ALL,
    SegwitV0SignatureHash,
    sha256,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaClassicalOwnershipShutdownTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

        self.rpc_timeout = 120

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine_descriptor_batched(
        self,
        node,
        descriptor,
        count,
    ):
        hashes = []
        remaining = count

        while remaining:
            batch = min(10, remaining)

            hashes.extend(
                self.generatetodescriptor(
                    node,
                    batch,
                    descriptor,
                )
            )

            remaining -= batch

        return hashes

    def run_test(self):
        node = self.nodes[0]

        node.createwallet(
            wallet_name="pq_receiver",
            load_on_startup=True,
        )

        receiver = node.get_wallet_rpc(
            "pq_receiver"
        )

        receive_address = receiver.getnewaddress(
            "pq-receiver"
        )

        assert receive_address.startswith("mcrt1z")

        receive_script = CScript(
            bytes.fromhex(
                node.validateaddress(
                    receive_address
                )["scriptPubKey"]
            )
        )

        #
        # Generate a genuine secp256k1 ECDSA key completely outside the
        # Mercatura wallet.
        #
        classical_key = ECKey()
        classical_key.generate(
            compressed=True
        )

        classical_pubkey = (
            classical_key
            .get_pubkey()
        )

        classical_pubkey_bytes = (
            classical_pubkey
            .get_bytes()
        )

        assert_equal(
            len(classical_pubkey_bytes),
            33,
        )

        #
        # Classical witness script:
        #
        #     <compressed secp256k1 pubkey> OP_CHECKSIG
        #
        # Its P2WSH output remains structurally valid witness-v0. Mercatura
        # deliberately disables the classical signature authorization inside
        # that script rather than disabling witness-v0 itself.
        #
        witness_script = CScript([
            classical_pubkey_bytes,
            OP_CHECKSIG,
        ])

        funding_script = CScript([
            OP_0,
            sha256(witness_script),
        ])

        assert_equal(
            len(funding_script),
            34,
        )

        funding_descriptor = (
            f"raw({funding_script.hex()})"
        )

        #
        # Mine the classical P2WSH output directly as coinbase payout.
        # This does not ask the Mercatura wallet to generate classical
        # ownership; it deliberately constructs the consensus test fixture.
        #
        self.log.info(
            "Mining mature classical P2WSH funding output"
        )

        mined = self.mine_descriptor_batched(
            node,
            funding_descriptor,
            101,
        )

        assert_equal(
            node.getblockcount(),
            101,
        )

        #
        # Locate the exact P2WSH output in the first matured coinbase.
        #
        funding_block = from_hex(
            CBlock(),
            node.getblock(
                mined[0],
                0,
            ),
        )

        coinbase = funding_block.vtx[0]

        funding_vout = None

        for index, output in enumerate(
            coinbase.vout
        ):
            if output.scriptPubKey == funding_script:
                funding_vout = index
                break

        assert funding_vout is not None

        funding_output = coinbase.vout[
            funding_vout
        ]

        input_amount = funding_output.nValue

        assert input_amount > 10

        coinbase_txid = coinbase.txid_hex

        self.log.info(
            f"Classical funding outpoint: "
            f"{coinbase_txid}:{funding_vout}"
        )

        #
        # Build a normal spend to a native Mercatura PQ destination.
        # Leave 0.10 MCA (10 base units) as fee.
        #
        spend = CTransaction()
        spend.version = 2

        spend.vin = [
            CTxIn(
                COutPoint(
                    int(coinbase_txid, 16),
                    funding_vout,
                )
            )
        ]

        spend.vout = [
            CTxOut(
                input_amount - 10,
                receive_script,
            )
        ]

        spend.wit.vtxinwit = [
            CTxInWitness()
        ]

        #
        # Produce the real BIP143/SegWit-v0 sighash and a genuine DER ECDSA
        # signature using the matching private key.
        #
        sighash = SegwitV0SignatureHash(
            witness_script,
            spend,
            0,
            SIGHASH_ALL,
            input_amount,
        )

        der_signature = classical_key.sign_ecdsa(
            sighash,
            rfc6979=True,
        )

        #
        # Independently prove that the generated ECDSA signature is valid for
        # exactly this key and exactly this transaction authorization digest.
        #
        assert classical_pubkey.verify_ecdsa(
            der_signature,
            sighash,
        )

        classical_signature = (
            der_signature +
            bytes([SIGHASH_ALL])
        )

        spend.wit.vtxinwit[
            0
        ].scriptWitness.stack = [
            classical_signature,
            bytes(witness_script),
        ]

        assert_equal(
            spend.wit.vtxinwit[
                0
            ].scriptWitness.stack[1],
            bytes(witness_script),
        )

        self.log.info(
            "ECDSA signature independently verifies; "
            "submitting classical ownership spend to Mercatura"
        )

        #
        # Mercatura must reject this despite the cryptographically valid ECDSA
        # authorization. The failure must be an opcode-authorization failure,
        # not a malformed-signature failure.
        #
        acceptance = node.testmempoolaccept(
            [spend.serialize().hex()]
        )[0]

        reason = acceptance.get(
            "reject-reason",
            "",
        )

        self.log.info(
            f"Classical spend: "
            f"allowed={acceptance['allowed']}, "
            f"reason={reason}"
        )

        assert_equal(
            acceptance["allowed"],
            False,
        )

        assert reason

        reason_lower = reason.lower()

        assert "opcode" in reason_lower, reason

        #
        # Because the rejected transaction never entered the mempool or chain,
        # its classical funding output must remain unspent.
        #
        assert_equal(
            node.getrawmempool(),
            [],
        )

        unspent = node.gettxout(
            coinbase_txid,
            funding_vout,
        )

        assert unspent is not None

        #
        # Prove the rejection did not damage normal Mercatura operation.
        #
        self.log.info(
            "Mining normal native PQ block after classical rejection"
        )

        self.generatetoaddress(
            node,
            1,
            receive_address,
        )

        assert_equal(
            node.getblockcount(),
            102,
        )

        unspent_after = node.gettxout(
            coinbase_txid,
            funding_vout,
        )

        assert unspent_after is not None

        self.log.info(
            "Mercatura native classical ownership shutdown test passed"
        )


if __name__ == "__main__":
    MercaturaClassicalOwnershipShutdownTest(__file__).main()
