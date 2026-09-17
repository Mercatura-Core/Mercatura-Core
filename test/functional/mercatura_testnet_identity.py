#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native testnet identity and PQ-address functional test."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


TESTNET_PQ_PREFIX = "tmca1z"


class MercaturaTestnetIdentityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

        self.chain = "testnet"

        self.extra_args = [[
            "-dnsseed=0",
            "-fixedseeds=0",
        ]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def assert_testnet_pq(self, wallet, address):
        assert address.startswith(
            TESTNET_PQ_PREFIX
        ), address

        #
        # Explicitly exclude inherited Bitcoin and Mercatura regtest/mainnet
        # address identities.
        #
        assert not address.startswith("bc1")
        assert not address.startswith("tb1")
        assert not address.startswith("bcrt1")
        assert not address.startswith("mcrt1")
        assert not address.startswith("mca1")

        info = wallet.getaddressinfo(
            address
        )

        assert_equal(
            info["ismine"],
            True,
        )

        assert_equal(
            info["iswitness"],
            True,
        )

        assert_equal(
            info["witness_version"],
            2,
        )

        assert_equal(
            len(info["witness_program"]),
            64,
        )

        assert_equal(
            info["scriptPubKey"],
            "5220" + info["witness_program"],
        )

        validated = self.nodes[0].validateaddress(
            address
        )

        assert_equal(
            validated["isvalid"],
            True,
        )

        assert_equal(
            validated["iswitness"],
            True,
        )

        assert_equal(
            validated["witness_version"],
            2,
        )

        assert_equal(
            validated["scriptPubKey"],
            info["scriptPubKey"],
        )

    def run_test(self):
        node = self.nodes[0]

        #
        # This test intentionally runs the real Mercatura testnet chainparams,
        # not regtest.
        #
        chain_info = node.getblockchaininfo()

        assert_equal(
            chain_info["blocks"],
            0,
        )

        assert_equal(
            chain_info["headers"],
            0,
        )

        node.createwallet(
            wallet_name="testnet_pq_wallet",
            load_on_startup=True,
        )

        wallet = node.get_wallet_rpc(
            "testnet_pq_wallet"
        )

        self.log.info(
            "Checking Mercatura testnet PQ receive address"
        )

        receive_address = wallet.getnewaddress(
            "testnet-receive"
        )

        self.assert_testnet_pq(
            wallet,
            receive_address,
        )

        self.log.info(
            "Checking Mercatura testnet PQ change address"
        )

        change_address = wallet.getrawchangeaddress()

        self.assert_testnet_pq(
            wallet,
            change_address,
        )

        assert receive_address != change_address

        #
        # Inherited Bitcoin address-type requests must still resolve to
        # Mercatura's native PQ testnet destination.
        #
        for requested_type in (
            "legacy",
            "p2sh-segwit",
            "bech32",
            "bech32m",
        ):
            self.log.info(
                f"Requesting testnet address as {requested_type}"
            )

            address = wallet.getnewaddress(
                f"forced-{requested_type}",
                requested_type,
            )

            self.assert_testnet_pq(
                wallet,
                address,
            )

        #
        # Restart the isolated testnet node and prove its chain identity and
        # PQ wallet identity survive normal startup.
        #
        self.log.info(
            "Restarting Mercatura testnet node"
        )

        self.restart_node(0)

        node = self.nodes[0]

        wallet = node.get_wallet_rpc(
            "testnet_pq_wallet"
        )

        assert_equal(
            node.getblockcount(),
            0,
        )

        after_restart = wallet.getnewaddress(
            "after-restart"
        )

        self.assert_testnet_pq(
            wallet,
            after_restart,
        )

        assert after_restart not in {
            receive_address,
            change_address,
        }

        self.log.info(
            "Mercatura native testnet identity functional test passed"
        )


if __name__ == "__main__":
    MercaturaTestnetIdentityTest(__file__).main()
