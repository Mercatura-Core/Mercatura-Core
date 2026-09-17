#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native coinbase maturity functional test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


COINBASE_MATURITY = 100


class MercaturaCoinbaseMaturityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

    def mine_blocks_batched(self, node, count, address, batch_size=10):
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

        self.log.info("Creating PQ miner and receiver wallets")

        node.createwallet(wallet_name="miner")
        node.createwallet(wallet_name="receiver")

        miner = node.get_wallet_rpc("miner")
        receiver = node.get_wallet_rpc("receiver")

        miner_address = miner.getnewaddress()
        receiver_address = receiver.getnewaddress()

        assert miner_address.startswith("mcrt1z")
        assert receiver_address.startswith("mcrt1z")

        #
        # Mine exactly one coinbase.
        #
        self.log.info("Mining initial PQ coinbase")

        first_hashes = self.generatetoaddress(
            node,
            1,
            miner_address,
        )

        assert_equal(len(first_hashes), 1)
        assert_equal(node.getblockcount(), 1)

        balances = miner.getbalances()["mine"]

        self.log.info(
            f"After first block: trusted={balances['trusted']}, "
            f"immature={balances['immature']}"
        )

        assert_equal(
            balances["trusted"],
            Decimal("0.00"),
        )

        assert balances["immature"] > Decimal("0.00")

        first_coinbase_value = balances["immature"]

        #
        # Mine to height 99: the first coinbase has 99 confirmations.
        #
        self.log.info(
            "Mining to one block before coinbase maturity boundary"
        )

        self.mine_blocks_batched(
            node,
            COINBASE_MATURITY - 2,
            miner_address,
        )

        assert_equal(
            node.getblockcount(),
            COINBASE_MATURITY - 1,
        )

        balances = miner.getbalances()["mine"]

        assert_equal(
            balances["trusted"],
            Decimal("0.00"),
        )

        assert balances["immature"] >= first_coinbase_value

        #
        # Height 100: first coinbase should still be immature under Bitcoin-style
        # COINBASE_MATURITY semantics because it has not yet reached depth 101.
        #
        self.log.info(
            "Mining block at the maturity boundary"
        )

        self.generatetoaddress(
            node,
            1,
            miner_address,
        )

        assert_equal(
            node.getblockcount(),
            COINBASE_MATURITY,
        )

        balances = miner.getbalances()["mine"]

        self.log.info(
            f"At height {COINBASE_MATURITY}: "
            f"trusted={balances['trusted']}, "
            f"immature={balances['immature']}"
        )

        assert_equal(
            balances["trusted"],
            Decimal("0.00"),
        )

        #
        # Height 101: first coinbase becomes spendable.
        #
        self.log.info(
            "Crossing the coinbase maturity boundary"
        )

        self.generatetoaddress(
            node,
            1,
            miner_address,
        )

        assert_equal(
            node.getblockcount(),
            COINBASE_MATURITY + 1,
        )

        balances = miner.getbalances()["mine"]

        self.log.info(
            f"At height {COINBASE_MATURITY + 1}: "
            f"trusted={balances['trusted']}, "
            f"immature={balances['immature']}"
        )

        assert balances["trusted"] >= first_coinbase_value

        #
        # Spend matured PQ coinbase funds.
        #
        self.log.info(
            "Spending matured PQ coinbase funds"
        )

        txid = miner.sendtoaddress(
            receiver_address,
            Decimal("1.00"),
        )

        assert txid in node.getrawmempool()

        pending = receiver.getbalances()["mine"]["untrusted_pending"]

        assert_equal(
            pending,
            Decimal("1.00"),
        )

        #
        # Confirm spend.
        #
        self.log.info(
            "Confirming matured coinbase PQ spend"
        )

        self.generatetoaddress(
            node,
            1,
            miner_address,
        )

        assert txid not in node.getrawmempool()

        assert_equal(
            receiver.getbalance(),
            Decimal("1.00"),
        )

        tx = receiver.gettransaction(txid)

        assert_equal(
            tx["confirmations"],
            1,
        )

        self.log.info(
            "Mercatura native coinbase maturity passed"
        )


if __name__ == "__main__":
    MercaturaCoinbaseMaturityTest(__file__).main()
