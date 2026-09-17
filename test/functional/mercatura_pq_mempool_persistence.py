#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ mempool persistence functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaPQMempoolPersistenceTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

        self.extra_args = [[
            "-fallbackfee=0.01",
        ]]

        self.rpc_timeout = 120

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine_blocks_batched(self, node, address, count):
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

        node.createwallet(
            wallet_name="sender",
            load_on_startup=True,
        )

        node.createwallet(
            wallet_name="receiver",
            load_on_startup=True,
        )

        sender = node.get_wallet_rpc(
            "sender"
        )

        receiver = node.get_wallet_rpc(
            "receiver"
        )

        mining_address = sender.getnewaddress(
            "pq-mempool-mining"
        )

        receive_address = receiver.getnewaddress(
            "pq-mempool-receive"
        )

        assert mining_address.startswith("mcrt1z")
        assert receive_address.startswith("mcrt1z")

        #
        # Mature native PQ funding.
        #
        self.log.info(
            "Mining mature PQ funding"
        )

        self.mine_blocks_batched(
            node,
            mining_address,
            101,
        )

        #
        # Create and broadcast a genuine PQ transaction.
        #
        self.log.info(
            "Broadcasting native PQ transaction"
        )

        receiver_before = receiver.getbalance()

        txid = sender.sendtoaddress(
            receive_address,
            Decimal("1.00"),
        )

        mempool = node.getrawmempool()

        assert txid in mempool

        entry_before = node.getmempoolentry(
            txid
        )

        assert entry_before["vsize"] > 0

        receiver_balances = receiver.getbalances()

        assert_equal(
            receiver_balances["mine"]["untrusted_pending"],
            Decimal("1.00"),
        )

        height_before = node.getblockcount()
        tip_before = node.getbestblockhash()

        #
        # Restart the node normally. Mercatura inherits mempool persistence,
        # so the native PQ transaction should be reloaded from disk.
        #
        self.log.info(
            "Restarting node with PQ transaction in mempool"
        )

        self.restart_node(0)

        node = self.nodes[0]

        #
        # Reacquire wallet RPC proxies after restart.
        #
        sender = node.get_wallet_rpc(
            "sender"
        )

        receiver = node.get_wallet_rpc(
            "receiver"
        )

        assert_equal(
            node.getblockcount(),
            height_before,
        )

        assert_equal(
            node.getbestblockhash(),
            tip_before,
        )

        #
        # The transaction must still exist in the mempool after restart.
        #
        self.log.info(
            "Checking persisted PQ mempool transaction"
        )

        mempool_after = node.getrawmempool()

        assert txid in mempool_after

        entry_after = node.getmempoolentry(
            txid
        )

        assert_equal(
            entry_after["vsize"],
            entry_before["vsize"],
        )

        receiver_balances_after = receiver.getbalances()

        assert_equal(
            receiver_balances_after["mine"]["untrusted_pending"],
            Decimal("1.00"),
        )

        #
        # Mine the persisted transaction after restart.
        #
        self.log.info(
            "Mining persisted PQ transaction"
        )

        confirmation_address = sender.getnewaddress(
            "post-restart-mining"
        )

        self.generatetoaddress(
            node,
            1,
            confirmation_address,
        )

        assert txid not in node.getrawmempool()

        tx_info = receiver.gettransaction(
            txid
        )

        assert tx_info["confirmations"] >= 1

        assert_equal(
            receiver.getbalance(),
            receiver_before + Decimal("1.00"),
        )

        self.log.info(
            "Mercatura native PQ mempool persistence test passed"
        )


if __name__ == "__main__":
    MercaturaPQMempoolPersistenceTest(__file__).main()
