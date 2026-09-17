#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native coinbase-subsidy enforcement functional test."""

from decimal import Decimal

from test_framework.blocktools import create_block, create_coinbase
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaCoinbaseSubsidyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = False

    def solve_mercatura_block(self, node, block):
        solved = node.solvemercaturablock(
            block.serialize().hex()
        )

        block.nNonce = solved["nonce"]

    def make_template_block(
        self,
        node,
        template,
        coinbase_value,
    ):
        height = template["height"]

        #
        # create_coinbase() normally accepts whole-coin style values.
        # Build it with zero and then assign the exact consensus base-unit
        # amount supplied by getblocktemplate.
        #
        coinbase = create_coinbase(
            height,
            script_pubkey=CScript([OP_TRUE]),
            nValue=0,
        )

        assert_equal(
            len(coinbase.vout),
            1,
        )

        coinbase.vout[0].nValue = coinbase_value

        block = create_block(
            int(template["previousblockhash"], 16),
            coinbase,
            template["curtime"],
        )

        block.nVersion = template["version"]
        block.nBits = int(
            template["bits"],
            16,
        )

        self.solve_mercatura_block(
            node,
            block,
        )

        return block

    def run_test(self):
        node = self.nodes[0]

        #
        # Height 1:
        # obtain the actual commanded Mercatura subsidy from the miner
        # template and claim exactly that amount.
        #
        self.log.info(
            "Reading live Mercatura block-1 subsidy"
        )

        template = node.getblocktemplate(
            {
                "rules": ["segwit"],
            }
        )

        assert_equal(
            template["height"],
            1,
        )

        subsidy = template["coinbasevalue"]

        assert subsidy > 0

        self.log.info(
            f"Height 1 commanded subsidy: "
            f"{subsidy} base units"
        )

        valid_block = self.make_template_block(
            node,
            template,
            subsidy,
        )

        valid_result = node.submitblock(
            valid_block.serialize().hex()
        )

        assert_equal(
            valid_result,
            None,
        )

        assert_equal(
            node.getblockcount(),
            1,
        )

        valid_tip = node.getbestblockhash()

        assert_equal(
            valid_tip,
            valid_block.hash_hex,
        )

        self.log.info(
            "Exact commanded coinbase subsidy accepted"
        )

        #
        # Height 2:
        # obtain the next real subsidy, then claim exactly one Mercatura
        # base unit (0.01 MCA) too much.
        #
        template = node.getblocktemplate(
            {
                "rules": ["segwit"],
            }
        )

        assert_equal(
            template["height"],
            2,
        )

        next_subsidy = template["coinbasevalue"]

        assert next_subsidy > 0

        self.log.info(
            f"Height 2 commanded subsidy: "
            f"{next_subsidy} base units"
        )

        overclaim_value = next_subsidy + 1

        self.log.info(
            "Constructing coinbase claiming one base unit too much"
        )

        overclaim_block = self.make_template_block(
            node,
            template,
            overclaim_value,
        )

        rejected = node.submitblock(
            overclaim_block.serialize().hex()
        )

        self.log.info(
            f"Overclaim rejection: {rejected}"
        )

        assert_equal(
            rejected,
            "bad-cb-amount",
        )

        #
        # Consensus rejection must not move the active chain.
        #
        assert_equal(
            node.getblockcount(),
            1,
        )

        assert_equal(
            node.getbestblockhash(),
            valid_tip,
        )

        #
        # The rejected candidate must not poison subsequent block creation.
        #
        self.log.info(
            "Mining normal block after subsidy overclaim rejection"
        )

        mined = self.generatetodescriptor(
            node,
            1,
            "raw(51)",
        )

        assert_equal(
            len(mined),
            1,
        )

        assert_equal(
            node.getblockcount(),
            2,
        )

        assert_equal(
            node.getbestblockhash(),
            mined[0],
        )

        #
        # Verify the accepted block's actual coinbase does not exceed the
        # exact commanded height-2 subsidy.
        #
        accepted_block = node.getblock(
            mined[0],
            2,
        )

        coinbase = accepted_block["tx"][0]

        claimed = sum(
            (
                output["value"]
                for output in coinbase["vout"]
            ),
            Decimal("0"),
        )

        #
        # RPC transaction amounts are displayed in MCA rather than integer
        # base units, so compare against the template value converted to MCA.
        #
        expected_mca = (
            Decimal(next_subsidy) /
            Decimal(100)
        )

        assert_equal(
            claimed,
            expected_mca,
        )

        self.log.info(
            "Mercatura native coinbase subsidy enforcement test passed"
        )


if __name__ == "__main__":
    MercaturaCoinbaseSubsidyTest(__file__).main()
