#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native PQ wallet address-enforcement functional test."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


PQ_PREFIX = "mcrt1z"
PQ_SCRIPT_TYPE = "witness_v2_mercatura_pq"

REQUESTED_TYPES = (
    "legacy",
    "p2sh-segwit",
    "bech32",
    "bech32m",
)


class MercaturaPQAddressEnforcementTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def assert_native_pq_address(self, wallet, address):
        assert address.startswith(PQ_PREFIX), address

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

        if "scriptPubKey" in validated:
            assert_equal(
                validated["scriptPubKey"],
                "5220" + info["witness_program"],
            )

    def run_test(self):
        node = self.nodes[0]

        node.createwallet(
            wallet_name="pq_wallet",
            load_on_startup=True,
        )

        wallet = node.get_wallet_rpc(
            "pq_wallet"
        )

        #
        # Default wallet receive and change destinations must be native
        # Mercatura PQ witness-v2 outputs.
        #
        self.log.info(
            "Checking default receive address"
        )

        default_receive = wallet.getnewaddress(
            "default-pq"
        )

        self.assert_native_pq_address(
            wallet,
            default_receive,
        )

        self.log.info(
            "Checking default change address"
        )

        default_change = wallet.getrawchangeaddress()

        self.assert_native_pq_address(
            wallet,
            default_change,
        )

        #
        # Mercatura intentionally disables classical ownership as a normal
        # wallet destination. Requests using inherited Bitcoin address-type
        # labels must therefore still return native Mercatura PQ ownership.
        #
        for requested_type in REQUESTED_TYPES:
            self.log.info(
                f"Requesting receive address as {requested_type}"
            )

            address = wallet.getnewaddress(
                f"forced-{requested_type}",
                requested_type,
            )

            self.assert_native_pq_address(
                wallet,
                address,
            )

            #
            # Confirm the wallet did not merely return a different encoding
            # of the same classical script family.
            #
            info = wallet.getaddressinfo(
                address
            )

            assert_equal(
                info["scriptPubKey"][:4],
                "5220",
            )

        #
        # Exercise the corresponding inherited change-address requests.
        #
        for requested_type in REQUESTED_TYPES:
            self.log.info(
                f"Requesting change address as {requested_type}"
            )

            address = wallet.getrawchangeaddress(
                requested_type
            )

            self.assert_native_pq_address(
                wallet,
                address,
            )

        #
        # Each fresh derivation should remain a distinct PQ destination.
        #
        addresses = {
            default_receive,
            default_change,
        }

        for _ in range(4):
            addresses.add(
                wallet.getnewaddress()
            )

        assert_equal(
            len(addresses),
            6,
        )

        for address in addresses:
            assert address.startswith(PQ_PREFIX)

        self.log.info(
            "Mercatura native PQ wallet address enforcement test passed"
        )


if __name__ == "__main__":
    MercaturaPQAddressEnforcementTest(__file__).main()
