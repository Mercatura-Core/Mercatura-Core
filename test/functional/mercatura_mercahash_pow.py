#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native MercaHash proof-of-work validation test."""

from copy import deepcopy

from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CBlockHeader
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


MAX_NONCE_MUTATIONS = 64


class MercaturaMercaHashPoWTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = False

    def solve_mercatura_block(self, node, block):
        """Solve a custom block with Mercatura's MercaHash-v1 solver."""
        solved = node.solvemercaturablock(
            block.serialize().hex()
        )

        block.nNonce = solved["nonce"]

    def run_test(self):
        node = self.nodes[0]

        #
        # Construct a valid custom block on the current regtest tip.
        #
        parent_hash = node.getbestblockhash()
        parent_header = node.getblockheader(
            parent_hash
        )

        parent_height = node.getblockcount()
        height = parent_height + 1

        self.log.info(
            "Constructing correctly solved MercaHash block"
        )

        coinbase = create_coinbase(
            height,
            script_pubkey=CScript([OP_TRUE]),
            nValue=0,
        )

        valid_block = create_block(
            int(parent_hash, 16),
            coinbase,
            parent_header["time"] + 1,
        )

        #
        # Use the actual current chain difficulty explicitly rather than
        # depending on create_block()'s inherited regtest constant.
        #
        valid_block.nBits = int(
            parent_header["bits"],
            16,
        )

        self.solve_mercatura_block(
            node,
            valid_block,
        )

        solved_nonce = valid_block.nNonce

        self.log.info(
            f"Solved MercaHash nonce: {solved_nonce}"
        )

        #
        # A correctly solved MercaHash block must be consensus-valid.
        #
        result = node.submitblock(
            valid_block.serialize().hex()
        )

        assert_equal(
            result,
            None,
        )

        accepted_tip = node.getbestblockhash()

        assert_equal(
            node.getblockcount(),
            height,
        )

        accepted_header = node.getblockheader(
            accepted_tip
        )

        assert_equal(
            accepted_header["height"],
            height,
        )

        assert_equal(
            int(accepted_header["bits"], 16),
            valid_block.nBits,
        )

        self.log.info(
            "Correctly solved MercaHash block accepted"
        )

        #
        # Now alter only the nonce while keeping the same parent, timestamp,
        # merkle root, version and required nBits.
        #
        # Regtest's PoW target is intentionally easy, so a single arbitrary
        # nonce mutation could still satisfy MercaHash by chance. Try several
        # alternate nonces until consensus rejects one for invalid PoW.
        #
        self.log.info(
            "Searching for nonce mutation rejected by MercaHash validation"
        )

        rejected_reason = None
        rejected_nonce = None
        accidentally_valid = 0

        for offset in range(1, MAX_NONCE_MUTATIONS + 1):
            candidate = deepcopy(
                valid_block
            )

            candidate.nNonce = (
                solved_nonce + offset
            ) & 0xffffffff

            candidate_header = CBlockHeader(
                candidate
            )

            try:
                node.submitheader(
                    candidate_header.serialize().hex()
                )

                #
                # With regtest's deliberately easy target an altered nonce can
                # occasionally still satisfy MercaHash. Such a valid header is
                # only an equal-work side header and must not alter the active
                # block chain.
                #
                accidentally_valid += 1

                assert_equal(
                    node.getbestblockhash(),
                    accepted_tip,
                )

                assert_equal(
                    node.getblockcount(),
                    height,
                )

            except JSONRPCException as exc:
                rejected_reason = exc.error["message"]
                rejected_nonce = candidate.nNonce
                break

        assert rejected_reason is not None
        assert rejected_nonce is not None

        self.log.info(
            f"Rejected mutated nonce {rejected_nonce}: "
            f"{rejected_reason}"
        )

        self.log.info(
            f"Accidentally valid alternate nonces before rejection: "
            f"{accidentally_valid}"
        )

        #
        # The candidate differs from the accepted header only by nonce.
        # Header validation must therefore reject an invalid MercaHash proof
        # explicitly as high-hash.
        #
        assert "high-hash" in rejected_reason.lower()

        #
        # Invalid proof must not alter the active chain.
        #
        assert_equal(
            node.getbestblockhash(),
            accepted_tip,
        )

        assert_equal(
            node.getblockcount(),
            height,
        )

        #
        # Finally prove normal MercaHash mining continues after rejection.
        #
        self.log.info(
            "Mining another block after invalid-PoW rejection"
        )

        hashes = self.generatetodescriptor(
            node,
            1,
            "raw(51)",
        )

        assert_equal(
            len(hashes),
            1,
        )

        assert_equal(
            node.getblockcount(),
            height + 1,
        )

        assert_equal(
            node.getbestblockhash(),
            hashes[0],
        )

        self.log.info(
            "Mercatura native MercaHash proof-of-work validation passed"
        )


if __name__ == "__main__":
    MercaturaMercaHashPoWTest(__file__).main()
