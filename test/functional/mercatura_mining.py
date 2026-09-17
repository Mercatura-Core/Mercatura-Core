#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native regtest mining functional test."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaMiningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Creating Mercatura PQ mining wallet")
        node.createwallet(wallet_name="miner")
        wallet = node.get_wallet_rpc("miner")

        mining_address = wallet.getnewaddress()
        self.log.info(f"Mining address: {mining_address}")

        assert mining_address.startswith("mcrt1z")
        assert_equal(node.getblockcount(), 0)

        self.log.info("Mining first Mercatura block")
        block_hashes = self.generatetoaddress(node, 1, mining_address)

        assert_equal(len(block_hashes), 1)
        assert_equal(node.getblockcount(), 1)
        assert_equal(node.getbestblockhash(), block_hashes[0])

        first_block = node.getblock(block_hashes[0])
        assert_equal(first_block["height"], 1)
        assert_equal(first_block["confirmations"], 1)

        self.log.info("Mining second Mercatura block")
        second_hashes = self.generatetoaddress(node, 1, mining_address)

        assert_equal(len(second_hashes), 1)
        assert_equal(node.getblockcount(), 2)
        assert_equal(node.getbestblockhash(), second_hashes[0])

        first_block = node.getblock(block_hashes[0])
        assert_equal(first_block["confirmations"], 2)

        self.log.info("Mercatura native regtest mining passed")


if __name__ == "__main__":
    MercaturaMiningTest(__file__).main()
