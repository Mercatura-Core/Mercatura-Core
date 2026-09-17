#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ package and mempool ancestry test."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


PARENT_VALUE = Decimal("5.00")
PARENT_FEE = Decimal("0.10")
CHILD_VALUE = Decimal("4.90")


class MercaturaPQPackageTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.rpc_timeout = 180

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

    @staticmethod
    def find_output(decoded, address):
        for output in decoded["vout"]:
            script = output["scriptPubKey"]

            if script.get("address") == address:
                return output

        raise AssertionError(
            f"Unable to find output for address {address}"
        )

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

        sender = node.get_wallet_rpc("sender")
        receiver = node.get_wallet_rpc("receiver")

        mining_address = sender.getnewaddress(
            "package-mining"
        )

        receiver_address = receiver.getnewaddress(
            "package-receiver"
        )

        assert mining_address.startswith("mcrt1z")
        assert receiver_address.startswith("mcrt1z")

        self.log.info(
            "Mining one mature native PQ coinbase"
        )

        self.mine_batched(
            node,
            101,
            mining_address,
        )

        spendable = [
            coin
            for coin in sender.listunspent()
            if coin.get("spendable", False)
        ]

        assert spendable

        utxo = spendable[0]
        input_amount = Decimal(str(utxo["amount"]))

        child_funding_address = sender.getnewaddress(
            "package-child-funding"
        )

        change_address = sender.getrawchangeaddress()

        change_value = (
            input_amount
            - PARENT_VALUE
            - PARENT_FEE
        )

        assert change_value > Decimal("0.02")

        #
        # Construct and sign the parent without broadcasting it.
        #
        self.log.info(
            "Constructing unbroadcast PQ parent"
        )

        parent_raw = node.createrawtransaction(
            [
                {
                    "txid": utxo["txid"],
                    "vout": utxo["vout"],
                    "sequence": 0xfffffffd,
                }
            ],
            {
                child_funding_address: PARENT_VALUE,
                change_address: change_value,
            },
        )

        parent_signed = sender.signrawtransactionwithwallet(
            parent_raw
        )

        assert_equal(
            parent_signed["complete"],
            True,
        )

        parent_hex = parent_signed["hex"]
        parent_decoded = node.decoderawtransaction(
            parent_hex
        )

        parent_txid = parent_decoded["txid"]
        parent_wtxid = parent_decoded["hash"]

        funding_output = self.find_output(
            parent_decoded,
            child_funding_address,
        )

        parent_vout = funding_output["n"]

        assert_equal(
            Decimal(str(funding_output["value"])),
            PARENT_VALUE,
        )

        parent_script = funding_output[
            "scriptPubKey"
        ]["hex"]

        #
        # Construct a child spending the unconfirmed native PQ parent
        # output. Because the parent has never entered the mempool,
        # supply the exact PQ prevout amount and script explicitly.
        #
        self.log.info(
            "Constructing unbroadcast PQ child"
        )

        child_raw = node.createrawtransaction(
            [
                {
                    "txid": parent_txid,
                    "vout": parent_vout,
                    "sequence": 0xfffffffd,
                }
            ],
            {
                receiver_address: CHILD_VALUE,
            },
        )

        child_signed = sender.signrawtransactionwithwallet(
            child_raw,
            [
                {
                    "txid": parent_txid,
                    "vout": parent_vout,
                    "scriptPubKey": parent_script,
                    "amount": PARENT_VALUE,
                }
            ],
        )

        assert_equal(
            child_signed["complete"],
            True,
        )

        child_hex = child_signed["hex"]
        child_decoded = node.decoderawtransaction(
            child_hex
        )

        child_txid = child_decoded["txid"]
        child_wtxid = child_decoded["hash"]

        #
        # The child cannot enter the mempool by itself because its
        # parent is neither confirmed nor already in the mempool.
        #
        self.log.info(
            "Confirming standalone child is rejected"
        )

        standalone = node.testmempoolaccept(
            [child_hex]
        )[0]

        assert_equal(
            standalone["allowed"],
            False,
        )

        assert standalone.get("reject-reason")

        assert_equal(
            node.getrawmempool(),
            [],
        )

        #
        # Submit parent and child atomically as a package.
        #
        self.log.info(
            "Submitting native PQ parent/child package"
        )

        package = node.submitpackage(
            [
                parent_hex,
                child_hex,
            ]
        )

        assert_equal(
            package["package_msg"],
            "success",
        )

        assert parent_wtxid in package["tx-results"]
        assert child_wtxid in package["tx-results"]

        assert_equal(
            package["tx-results"][parent_wtxid]["txid"],
            parent_txid,
        )

        assert_equal(
            package["tx-results"][child_wtxid]["txid"],
            child_txid,
        )

        mempool = set(node.getrawmempool())

        assert_equal(
            mempool,
            {
                parent_txid,
                child_txid,
            },
        )

        #
        # Verify the dependency graph exposed by the real mempool.
        #
        self.log.info(
            "Checking PQ ancestor/descendant relationships"
        )

        ancestors = node.getmempoolancestors(
            child_txid
        )

        descendants = node.getmempooldescendants(
            parent_txid
        )

        assert_equal(
            set(ancestors),
            {parent_txid},
        )

        assert_equal(
            set(descendants),
            {child_txid},
        )

        parent_entry = node.getmempoolentry(
            parent_txid
        )

        child_entry = node.getmempoolentry(
            child_txid
        )

        assert_equal(
            set(child_entry["depends"]),
            {parent_txid},
        )

        assert_equal(
            set(parent_entry["spentby"]),
            {child_txid},
        )

        assert_equal(
            parent_entry["ancestorcount"],
            1,
        )

        assert_equal(
            parent_entry["descendantcount"],
            2,
        )

        assert_equal(
            child_entry["ancestorcount"],
            2,
        )

        assert_equal(
            child_entry["descendantcount"],
            1,
        )

        #
        # Mercatura fee accounting counts all serialized PQ bytes.
        # Keep the end-to-end mempool accounting tied to that rule.
        #
        assert_equal(
            parent_entry["vsize"],
            parent_decoded["size"],
        )

        assert_equal(
            child_entry["vsize"],
            child_decoded["size"],
        )

        expected_package_size = (
            parent_entry["vsize"]
            + child_entry["vsize"]
        )

        assert_equal(
            child_entry["ancestorsize"],
            expected_package_size,
        )

        assert_equal(
            parent_entry["descendantsize"],
            expected_package_size,
        )

        #
        # Mine one block. Dependency ordering must allow both
        # transactions into the same block.
        #
        self.log.info(
            "Mining PQ package"
        )

        block_hash = self.generatetoaddress(
            node,
            1,
            mining_address,
        )[0]

        assert_equal(
            node.getrawmempool(),
            [],
        )

        block = node.getblock(
            block_hash
        )

        assert parent_txid in block["tx"]
        assert child_txid in block["tx"]

        assert (
            block["tx"].index(parent_txid)
            < block["tx"].index(child_txid)
        )

        receiver_tx = receiver.gettransaction(
            child_txid
        )

        assert receiver_tx["confirmations"] >= 1

        assert_equal(
            receiver.getbalance(),
            CHILD_VALUE,
        )

        self.log.info(
            "Mercatura native PQ package/ancestry test passed"
        )


if __name__ == "__main__":
    MercaturaPQPackageTest(__file__).main()
