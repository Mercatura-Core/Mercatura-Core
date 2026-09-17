#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura PQ historical wallet recovery and rescan test."""

from decimal import Decimal
from pathlib import Path

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


RECOVERY_AMOUNT = Decimal("1.00")


class MercaturaWalletHistoricalRecoveryTest(BitcoinTestFramework):
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

    def run_test(self):
        node = self.nodes[0]

        #
        # Fund an independent miner wallet first so the historical
        # payment does not depend on the wallet being recovered.
        #
        node.createwallet(
            wallet_name="miner",
            load_on_startup=True,
        )

        miner = node.get_wallet_rpc("miner")
        miner_address = miner.getnewaddress(
            "historical-recovery-miner"
        )

        assert miner_address.startswith("mcrt1z")

        self.log.info(
            "Mining mature funds for independent miner wallet"
        )

        self.mine_batched(
            node,
            101,
            miner_address,
        )

        assert miner.getbalance() > Decimal("0.00")

        #
        # Create the wallet we will later recover. Derive the receive
        # address before taking the backup so its PQ key locator and
        # derivation state are part of that backup.
        #
        node.createwallet(
            wallet_name="recovery_source",
            load_on_startup=True,
        )

        recovery_source = node.get_wallet_rpc(
            "recovery_source"
        )

        recovery_address = recovery_source.getnewaddress(
            "historical-recovery"
        )

        assert recovery_address.startswith("mcrt1z")

        assert_equal(
            recovery_source.getbalance(),
            Decimal("0.00"),
        )

        backup_path = str(
            Path(self.options.tmpdir)
            / "mercatura-pq-prefunding.backup"
        )

        self.log.info(
            "Backing up PQ wallet before it receives any funds"
        )

        recovery_source.backupwallet(
            backup_path
        )

        assert Path(backup_path).exists()

        #
        # Remove the wallet completely from the running node before
        # the payment exists. The wallet therefore cannot learn about
        # the transaction through normal live wallet notifications.
        #
        self.log.info(
            "Unloading pre-funding wallet"
        )

        node.unloadwallet(
            "recovery_source"
        )

        assert "recovery_source" not in node.listwallets()

        #
        # Pay the pre-derived native PQ address while the wallet is
        # absent, then confirm the transaction.
        #
        self.log.info(
            "Creating historical PQ payment while wallet is unloaded"
        )

        historical_txid = miner.sendtoaddress(
            recovery_address,
            RECOVERY_AMOUNT,
        )

        assert historical_txid in node.getrawmempool()

        confirm_address = miner.getnewaddress(
            "historical-recovery-confirm"
        )

        self.generatetoaddress(
            node,
            1,
            confirm_address,
        )

        assert historical_txid not in node.getrawmempool()

        tip_height = node.getblockcount()

        #
        # Restore the backup that predates the transaction. restorewallet
        # must rescan chain history and rediscover the native PQ output.
        #
        self.log.info(
            "Restoring pre-funding backup and requiring historical rediscovery"
        )

        node.restorewallet(
            "recovered",
            backup_path,
            True,
        )

        recovered = node.get_wallet_rpc(
            "recovered"
        )

        recovered_info = recovered.getaddressinfo(
            recovery_address
        )

        assert_equal(
            recovered_info["ismine"],
            True,
        )

        recovered_tx = recovered.gettransaction(
            historical_txid
        )

        assert recovered_tx["confirmations"] >= 1

        assert_equal(
            recovered.getbalance(),
            RECOVERY_AMOUNT,
        )

        self.log.info(
            "Historical native PQ payment rediscovered successfully"
        )

        #
        # Also exercise the explicit rescanblockchain RPC over the full
        # historical range. This must be idempotent and preserve the
        # rediscovered wallet state.
        #
        self.log.info(
            "Running explicit PQ wallet rescanblockchain"
        )

        rescan = recovered.rescanblockchain(
            0,
            tip_height,
        )

        assert_equal(
            rescan["start_height"],
            0,
        )

        assert_equal(
            rescan["stop_height"],
            tip_height,
        )

        assert_equal(
            recovered.getbalance(),
            RECOVERY_AMOUNT,
        )

        rescanned_tx = recovered.gettransaction(
            historical_txid
        )

        assert rescanned_tx["confirmations"] >= 1

        assert_equal(
            rescanned_tx["txid"],
            historical_txid,
        )

        #
        # Finally prove the recovered wallet can actually spend the
        # historically rediscovered PQ output.
        #
        receiver_address = miner.getnewaddress(
            "historical-recovery-spend"
        )

        spend_txid = recovered.sendtoaddress(
            receiver_address,
            Decimal("0.50"),
        )

        assert spend_txid in node.getrawmempool()

        self.log.info(
            "Recovered historical PQ output is spendable"
        )


if __name__ == "__main__":
    MercaturaWalletHistoricalRecoveryTest(__file__).main()
