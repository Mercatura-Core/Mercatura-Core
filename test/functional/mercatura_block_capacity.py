#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native block-capacity enforcement functional test."""

from test_framework.blocktools import (
    create_block,
    create_coinbase,
    create_tx_with_script,
)
from test_framework.messages import (
    CBlock,
    CTxOut,
    from_hex,
)
from test_framework.script import (
    CScript,
    OP_RETURN,
    OP_TRUE,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


INITIAL_CAPACITY = 1_048_576


class MercaturaBlockCapacityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = False

    def mine_op_true_blocks(self, count):
        """Mine mature anyone-can-spend Mercatura coinbases."""
        hashes = []

        while count > 0:
            batch = min(count, 10)
            hashes.extend(
                self.generatetodescriptor(
                    self.nodes[0],
                    batch,
                    "raw(51)",
                )
            )
            count -= batch

        return hashes

    def solve_mercatura_block(self, node, block):
        """Solve one custom block using mercaturad's MercaHash-v1 solver."""
        solved = node.solvemercaturablock(
            block.serialize().hex()
        )

        block.nNonce = solved["nonce"]

    def current_parent(self):
        node = self.nodes[0]

        best_hash = node.getbestblockhash()
        header = node.getblockheader(best_hash)

        return (
            int(best_hash, 16),
            node.getblockcount() + 1,
            header["time"] + 1,
        )

    def make_padded_spend(self, prev_tx, target_size):
        """Create a consensus-valid OP_TRUE spend padded with OP_RETURN outputs."""
        tx = create_tx_with_script(
            prev_tx,
            0,
            amount=prev_tx.vout[0].nValue,
            output_script=CScript([OP_TRUE]),
        )

        # Use many moderate-sized OP_RETURN outputs rather than one giant script.
        payload = bytes([0x42]) * 8_000

        while len(tx.serialize_with_witness()) < target_size:
            tx.vout.append(
                CTxOut(
                    0,
                    CScript([OP_RETURN, payload]),
                )
            )

        return tx

    def make_block(self, prev_hash, height, ntime, tx):
        coinbase = create_coinbase(
            height,
            script_pubkey=CScript([OP_TRUE]),
            nValue=0,
        )

        block = create_block(
            prev_hash,
            coinbase,
            ntime,
            txlist=[tx],
        )

        self.solve_mercatura_block(
            self.nodes[0],
            block,
        )

        return block

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mining 101 mature Mercatura OP_TRUE blocks")
        mined = self.mine_op_true_blocks(101)

        first_coinbase = from_hex(
            CBlock(),
            node.getblock(mined[0], 0),
        ).vtx[0]

        second_coinbase = from_hex(
            CBlock(),
            node.getblock(mined[1], 0),
        ).vtx[0]

        #
        # Valid block comfortably below the 1 MiB launch cap.
        #
        self.log.info("Building block below Mercatura launch capacity")

        under_tx = self.make_padded_spend(
            first_coinbase,
            900_000,
        )

        prev_hash, height, ntime = self.current_parent()

        under_block = self.make_block(
            prev_hash,
            height,
            ntime,
            under_tx,
        )

        under_size = len(under_block.serialize(with_witness=True))

        self.log.info(f"Below-cap serialized block size: {under_size}")

        assert under_size < INITIAL_CAPACITY

        result = node.submitblock(
            under_block.serialize().hex()
        )

        assert_equal(result, None)
        assert_equal(
            node.getbestblockhash(),
            under_block.hash_hex,
        )

        #
        # Validly constructed block above the 1 MiB launch cap.
        #
        self.log.info("Building block above Mercatura launch capacity")

        over_tx = self.make_padded_spend(
            second_coinbase,
            1_060_000,
        )

        prev_hash, height, ntime = self.current_parent()

        over_block = self.make_block(
            prev_hash,
            height,
            ntime,
            over_tx,
        )

        over_size = len(over_block.serialize(with_witness=True))

        self.log.info(f"Above-cap serialized block size: {over_size}")

        assert over_size > INITIAL_CAPACITY

        tip_before_rejection = node.getbestblockhash()

        result = node.submitblock(
            over_block.serialize().hex()
        )

        self.log.info(f"Above-cap rejection result: {result}")

        assert result is not None
        assert_equal(
            node.getbestblockhash(),
            tip_before_rejection,
        )

        self.log.info("Mercatura native block-capacity enforcement passed")


if __name__ == "__main__":
    MercaturaBlockCapacityTest(__file__).main()
