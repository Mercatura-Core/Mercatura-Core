#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura malformed PQ authorization over P2P regression test."""

from copy import deepcopy
from decimal import Decimal
from io import BytesIO

from test_framework.messages import CTransaction, msg_tx
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaPQP2PMalformedTest(BitcoinTestFramework):
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

    @staticmethod
    def transaction_from_hex(raw_hex):
        tx = CTransaction()

        tx.deserialize(
            BytesIO(
                bytes.fromhex(raw_hex)
            )
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

        sender = node.get_wallet_rpc("sender")
        receiver = node.get_wallet_rpc("receiver")

        mining_address = sender.getnewaddress(
            "p2p-malformed-mining"
        )

        receiver_address = receiver.getnewaddress(
            "p2p-malformed-receiver"
        )

        assert mining_address.startswith("mcrt1z")
        assert receiver_address.startswith("mcrt1z")

        self.log.info(
            "Mining mature native PQ funding"
        )

        self.mine_batched(
            node,
            101,
            mining_address,
        )

        assert sender.getbalance() > Decimal("0.00")

        #
        # Create and sign a normal wallet transaction without
        # broadcasting it.
        #
        self.log.info(
            "Creating valid unbroadcast PQ transaction"
        )

        funded = sender.walletcreatefundedpsbt(
            [],
            [
                {
                    receiver_address: Decimal("1.00")
                }
            ],
        )

        processed = sender.walletprocesspsbt(
            funded["psbt"]
        )

        assert_equal(
            processed["complete"],
            True,
        )

        finalized = node.finalizepsbt(
            processed["psbt"]
        )

        assert_equal(
            finalized["complete"],
            True,
        )

        valid_hex = finalized["hex"]

        decoded = node.decoderawtransaction(
            valid_hex
        )

        valid_txid = decoded["txid"]

        assert_equal(
            len(decoded["vin"]),
            1,
        )

        input_txid = decoded["vin"][0]["txid"]
        input_vout = decoded["vin"][0]["vout"]

        valid_tx = self.transaction_from_hex(
            valid_hex
        )

        assert_equal(
            len(valid_tx.vin),
            1,
        )

        witness = valid_tx.wit.vtxinwit[0].scriptWitness.stack

        assert_equal(
            len(witness),
            2,
        )

        assert_equal(
            len(witness[0]),
            3309,
        )

        assert_equal(
            len(witness[1]),
            1952,
        )

        assert_equal(
            valid_tx.vin[0].scriptSig,
            b"",
        )

        #
        # Establish that the untouched transaction is acceptable
        # without actually submitting it.
        #
        valid_acceptance = node.testmempoolaccept(
            [valid_hex]
        )[0]

        if not valid_acceptance["allowed"]:
            raise AssertionError(
                f"Valid baseline PQ transaction rejected: "
                f"{valid_acceptance}"
            )

        assert_equal(
            node.getrawmempool(),
            [],
        )

        #
        # Corrupt exactly one byte of the ML-DSA signature.
        #
        # The witness remains structurally canonical:
        #
        #   item 0 = exactly 3309-byte signature
        #   item 1 = exactly 1952-byte public key
        #
        # Only authorization validity changes.
        #
        malformed_tx = deepcopy(
            valid_tx
        )

        signature = bytearray(
            malformed_tx.wit.vtxinwit[0].scriptWitness.stack[0]
        )

        mutation_offset = len(signature) // 2

        signature[mutation_offset] ^= 0x01

        malformed_tx.wit.vtxinwit[0].scriptWitness.stack[0] = bytes(
            signature
        )

        malformed_witness = (
            malformed_tx.wit.vtxinwit[0].scriptWitness.stack
        )

        assert_equal(
            len(malformed_witness),
            2,
        )

        assert_equal(
            len(malformed_witness[0]),
            3309,
        )

        assert_equal(
            len(malformed_witness[1]),
            1952,
        )

        assert_equal(
            malformed_tx.vin[0].scriptSig,
            b"",
        )

        #
        # Because only witness bytes changed, the malformed transaction
        # has the same txid as the valid transaction but a different
        # authorization witness.
        #
        malformed_decoded = node.decoderawtransaction(
            malformed_tx.serialize_with_witness().hex()
        )

        assert_equal(
            malformed_decoded["txid"],
            valid_txid,
        )

        #
        # Connect a real P2P peer and inject the malformed transaction
        # using the wire-level tx message.
        #
        self.log.info(
            "Sending malformed PQ authorization over P2P"
        )

        peer = node.add_p2p_connection(
            P2PInterface()
        )

        peer.send_and_ping(
            msg_tx(malformed_tx)
        )

        #
        # Invalid PQ authorization must never enter the mempool.
        #
        assert_equal(
            node.getrawmempool(),
            [],
        )

        #
        # The rejected transaction must not consume its input.
        #
        unspent = node.gettxout(
            input_txid,
            input_vout,
        )

        assert unspent is not None

        #
        # Exercise several RPC paths immediately after malformed
        # P2P processing to prove the node remains healthy.
        #
        assert_equal(
            node.getblockcount(),
            101,
        )

        best_hash = node.getbestblockhash()

        assert best_hash

        mempool_info = node.getmempoolinfo()

        assert_equal(
            mempool_info["size"],
            0,
        )

        #
        # Send the ORIGINAL valid PQ transaction over P2P after the
        # malformed one. This proves transaction/P2P processing remains
        # operational and the rejected same-txid witness variant did not
        # poison normal acceptance.
        #
        self.log.info(
            "Sending valid PQ transaction after malformed P2P input"
        )

        peer.send_and_ping(
            msg_tx(valid_tx)
        )

        self.wait_until(
            lambda: valid_txid in node.getrawmempool(),
            timeout=30,
        )

        assert valid_txid in node.getrawmempool()

        pending = receiver.getbalances()[
            "mine"
        ]["untrusted_pending"]

        assert_equal(
            pending,
            Decimal("1.00"),
        )

        #
        # Finally mine normally after the malformed P2P event.
        #
        self.log.info(
            "Mining valid PQ transaction after malformed P2P input"
        )

        block_hash = self.generatetoaddress(
            node,
            1,
            mining_address,
        )[0]

        assert_equal(
            node.getblockcount(),
            102,
        )

        assert valid_txid not in node.getrawmempool()

        block = node.getblock(
            block_hash
        )

        assert valid_txid in block["tx"]

        received = receiver.gettransaction(
            valid_txid
        )

        assert received["confirmations"] >= 1

        assert_equal(
            receiver.getbalance(),
            Decimal("1.00"),
        )

        self.log.info(
            "Mercatura malformed PQ P2P rejection and node-survival "
            "test passed"
        )


if __name__ == "__main__":
    MercaturaPQP2PMalformedTest(__file__).main()
