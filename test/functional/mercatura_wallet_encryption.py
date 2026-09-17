#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ wallet encryption lifecycle functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_raises_rpc_error, assert_equal


class MercaturaWalletEncryptionTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

    def mine_blocks_batched(self, node, count, address, batch_size=10):
        """Mine MercaHash regtest blocks in bounded RPC batches."""
        hashes = []

        while count > 0:
            batch = min(count, batch_size)
            hashes.extend(
                self.generatetoaddress(
                    node,
                    batch,
                    address,
                )
            )
            count -= batch

        return hashes

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Creating encrypted-wallet test wallet")
        node.createwallet(
            wallet_name="encrypted",
            load_on_startup=True,
        )

        wallet = node.get_wallet_rpc("encrypted")

        funding_address = wallet.getnewaddress()
        assert funding_address.startswith("mcrt1z")

        self.log.info("Mining mature PQ funds")
        self.mine_blocks_batched(
            node,
            101,
            funding_address,
        )

        starting_balance = wallet.getbalance()
        self.log.info(f"Starting balance: {starting_balance}")

        assert starting_balance > Decimal("0.00")

        self.log.info("Creating receiver wallet")
        node.createwallet(wallet_name="receiver")
        receiver = node.get_wallet_rpc("receiver")

        receiver_address = receiver.getnewaddress()
        assert receiver_address.startswith("mcrt1z")

        passphrase = "mercatura-test-passphrase"

        self.log.info("Encrypting Mercatura PQ wallet")
        wallet.encryptwallet(passphrase)

        wallet = node.get_wallet_rpc("encrypted")

        wallet_info = wallet.getwalletinfo()
        assert "unlocked_until" in wallet_info
        assert_equal(wallet_info["unlocked_until"], 0)

        self.log.info("Verifying locked wallet cannot sign")
        assert_raises_rpc_error(
            -13,
            "wallet passphrase",
            wallet.sendtoaddress,
            receiver_address,
            Decimal("1.00"),
        )

        self.log.info("Unlocking encrypted PQ wallet")
        wallet.walletpassphrase(passphrase, 60)

        wallet_info = wallet.getwalletinfo()
        assert wallet_info["unlocked_until"] > 0

        self.log.info("Spending 1.00 MCA from unlocked PQ wallet")
        txid = wallet.sendtoaddress(
            receiver_address,
            Decimal("1.00"),
        )

        assert txid in node.getrawmempool()

        self.log.info("Relocking wallet")
        wallet.walletlock()

        wallet_info = wallet.getwalletinfo()
        assert_equal(wallet_info["unlocked_until"], 0)

        self.log.info("Mining transaction confirmation")
        mining_address = receiver.getnewaddress()

        hashes = self.generatetoaddress(
            node,
            1,
            mining_address,
        )

        assert_equal(len(hashes), 1)
        assert txid not in node.getrawmempool()

        assert_equal(receiver.getbalance(), Decimal("1.00"))

        balance_before_restart = wallet.getbalance()
        self.log.info(f"Encrypted wallet balance before restart: {balance_before_restart}")

        self.log.info("Restarting node")
        self.restart_node(0)

        node = self.nodes[0]

        assert "encrypted" in node.listwallets()

        wallet = node.get_wallet_rpc("encrypted")

        wallet_info = wallet.getwalletinfo()
        assert_equal(wallet_info["unlocked_until"], 0)

        balance_after_restart = wallet.getbalance()

        self.log.info(f"Encrypted wallet balance after restart: {balance_after_restart}")
        assert_equal(balance_after_restart, balance_before_restart)
        assert balance_after_restart > Decimal("0.00")

        self.log.info("Mercatura native PQ wallet encryption lifecycle passed")


if __name__ == "__main__":
    MercaturaWalletEncryptionTest(__file__).main()
