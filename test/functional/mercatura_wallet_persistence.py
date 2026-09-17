#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ wallet restart persistence functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaWalletPersistenceTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

    def mine_blocks_batched(self, node, count, address, batch_size=10):
        """Mine MercaHash regtest blocks in bounded RPC batches."""
        block_hashes = []

        while count > 0:
            batch = min(count, batch_size)
            block_hashes.extend(
                self.generatetoaddress(
                    node,
                    batch,
                    address,
                )
            )
            count -= batch

        return block_hashes

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Creating persistent Mercatura PQ wallet")
        node.createwallet(
            wallet_name="persist",
            load_on_startup=True,
        )

        wallet = node.get_wallet_rpc("persist")
        address = wallet.getnewaddress()

        assert address.startswith("mcrt1z")

        self.log.info("Mining mature PQ coinbase funds")
        self.mine_blocks_batched(
            node,
            101,
            address,
        )

        assert_equal(node.getblockcount(), 101)

        balance_before = wallet.getbalance()
        self.log.info(f"Balance before restart: {balance_before}")

        assert balance_before > Decimal("0.00")

        address_info_before = wallet.getaddressinfo(address)
        assert_equal(address_info_before["ismine"], True)

        self.log.info("Restarting Mercatura node")
        self.restart_node(0)

        node = self.nodes[0]

        loaded_wallets = node.listwallets()
        self.log.info(f"Loaded wallets after restart: {loaded_wallets}")

        assert "persist" in loaded_wallets

        wallet = node.get_wallet_rpc("persist")

        balance_after = wallet.getbalance()
        self.log.info(f"Balance after restart: {balance_after}")

        assert_equal(balance_after, balance_before)
        assert_equal(node.getblockcount(), 101)

        address_info_after = wallet.getaddressinfo(address)
        assert_equal(address_info_after["ismine"], True)

        self.log.info("Deriving a new PQ address after restart")
        new_address = wallet.getnewaddress()

        assert new_address.startswith("mcrt1z")
        assert new_address != address

        self.log.info("Mercatura native PQ wallet restart persistence passed")


if __name__ == "__main__":
    MercaturaWalletPersistenceTest(__file__).main()
