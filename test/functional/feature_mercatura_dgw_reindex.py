#!/usr/bin/env python3
# Copyright (c) 2026-present The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Verify Mercatura DGW next-target determinism across a full reindex."""

from test_framework.messages import CBlockHeader, from_hex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaDgwReindexTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.chain = ""  # main

    def candidate_bits(self, node):
        generated = self.generateblock(
            node,
            output="raw(51)",
            transactions=[],
            submit=False,
            sync_fun=self.no_op,
        )

        header = from_hex(
            CBlockHeader(),
            generated["hex"][:160],
        )

        assert_equal(
            f"{header.hashPrevBlock:064x}",
            node.getbestblockhash(),
        )

        return header.nBits

    def run_test(self):
        node = self.nodes[0]

        assert_equal(
            node.getblockchaininfo()["chain"],
            "main",
        )

        genesis_hash = node.getblockhash(0)
        genesis_header = node.getblockheader(genesis_hash)

        launch_bits = int(genesis_header["bits"], 16)

        self.log.info(
            "Mine 30 MAIN-consensus MercaHash blocks"
        )

        for _ in range(30):
            self.generateblock(
                node,
                output="raw(51)",
                transactions=[],
                submit=True,
                sync_fun=self.no_op,
            )

        assert_equal(
            node.getblockcount(),
            30,
        )

        tip_before = node.getbestblockhash()
        next_bits_before = self.candidate_bits(
            node,
        )

        # This must be a real post-startup DGW calculation rather than
        # merely returning the launch powLimit.
        if next_bits_before == launch_bits:
            raise AssertionError(
                "D10 did not exercise an adjusted DGW target"
            )

        self.log.info(
            "Pre-reindex tip=%s next_bits=%08x",
            tip_before,
            next_bits_before,
        )

        self.log.info(
            "Restart node with -reindex"
        )

        self.stop_nodes()
        self.start_nodes([["-reindex"]])

        node = self.nodes[0]

        assert_equal(
            node.getblockcount(),
            30,
        )

        tip_after = node.getbestblockhash()

        assert_equal(
            tip_after,
            tip_before,
        )

        next_bits_after = self.candidate_bits(
            node,
        )

        self.log.info(
            "Post-reindex tip=%s next_bits=%08x",
            tip_after,
            next_bits_after,
        )

        assert_equal(
            next_bits_after,
            next_bits_before,
        )


if __name__ == "__main__":
    MercaturaDgwReindexTest(__file__).main()
