#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ wallet backup/restore functional test."""

from decimal import Decimal
from pathlib import Path

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaWalletBackupRestoreTest(BitcoinTestFramework):
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

        self.log.info("Creating source Mercatura PQ wallet")
        node.createwallet(wallet_name="source")
        source = node.get_wallet_rpc("source")

        source_address = source.getnewaddress()
        assert source_address.startswith("mcrt1z")

        self.log.info("Mining mature PQ funds")
        self.mine_blocks_batched(
            node,
            101,
            source_address,
        )

        assert_equal(node.getblockcount(), 101)

        source_balance = source.getbalance()
        self.log.info(f"Source balance: {source_balance}")
        assert source_balance > Decimal("0.00")

        backup_path = str(
            Path(self.options.tmpdir) / "mercatura-pq-wallet.backup"
        )

        self.log.info(f"Backing up wallet to {backup_path}")
        source.backupwallet(backup_path)

        assert Path(backup_path).exists()

        self.log.info("Unloading source wallet")
        node.unloadwallet("source")

        assert "source" not in node.listwallets()

        self.log.info("Restoring wallet under a new name")
        node.restorewallet(
            "restored",
            backup_path,
        )

        restored = node.get_wallet_rpc("restored")

        restored_balance = restored.getbalance()
        self.log.info(f"Restored balance: {restored_balance}")

        assert_equal(restored_balance, source_balance)

        restored_info = restored.getaddressinfo(source_address)
        assert_equal(restored_info["ismine"], True)

        self.log.info("Creating receiver wallet")
        node.createwallet(wallet_name="receiver")
        receiver = node.get_wallet_rpc("receiver")

        receiver_address = receiver.getnewaddress()
        assert receiver_address.startswith("mcrt1z")

        self.log.info("Spending 1.00 MCA from restored PQ wallet")
        txid = restored.sendtoaddress(
            receiver_address,
            Decimal("1.00"),
        )

        assert txid in node.getrawmempool()

        self.log.info("Mining confirmation")
        restored_mining_address = restored.getnewaddress()

        confirm_hashes = self.generatetoaddress(
            node,
            1,
            restored_mining_address,
        )

        assert_equal(len(confirm_hashes), 1)
        assert txid not in node.getrawmempool()

        receiver_balance = receiver.getbalance()
        self.log.info(f"Receiver confirmed balance: {receiver_balance}")

        assert_equal(receiver_balance, Decimal("1.00"))

        tx = receiver.gettransaction(txid)
        assert_equal(tx["confirmations"], 1)

        self.log.info("Mercatura native PQ wallet backup/restore passed")


if __name__ == "__main__":
    MercaturaWalletBackupRestoreTest(__file__).main()
