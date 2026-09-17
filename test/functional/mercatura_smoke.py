#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Basic Mercatura regtest node startup smoke test."""

from test_framework.test_framework import BitcoinTestFramework


class MercaturaSmokeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]

        blockchain_info = node.getblockchaininfo()

        self.log.info("Verifying Mercatura regtest startup")
        assert blockchain_info["chain"] == "regtest"
        assert node.getblockcount() == 0

        genesis_hash = node.getblockhash(0)
        assert len(genesis_hash) == 64

        self.log.info(f"Mercatura regtest genesis: {genesis_hash}")


if __name__ == "__main__":
    MercaturaSmokeTest(__file__).main()
