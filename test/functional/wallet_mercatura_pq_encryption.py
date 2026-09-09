#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license.

"""Mercatura PQ encrypted-wallet spending lifecycle."""

from decimal import Decimal

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


PQ_ADDRESS_PREFIX = "mcrt1z"


class MercaturaPQWalletEncryptionTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

        self.extra_args = [
            ["-fallbackfee=0.01"],
        ]

        self.rpc_timeout = 120

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def assert_pq_address(self, wallet, address):
        assert address.startswith(PQ_ADDRESS_PREFIX), address

        info = wallet.getaddressinfo(address)

        assert_equal(info["ismine"], True)
        assert_equal(info["iswitness"], True)
        assert_equal(info["witness_version"], 2)
        assert_equal(len(info["witness_program"]), 64)
        assert_equal(
            info["scriptPubKey"],
            "5220" + info["witness_program"],
        )

    def mine_to_address(self, node, address, blocks):
        remaining = blocks

        while remaining > 0:
            batch = min(remaining, 20)

            node.generatetoaddress(
                batch,
                address,
                called_by_framework=True,
            )

            remaining -= batch

    def make_funded_psbt(
        self,
        wallet,
        utxo,
        destination,
    ):
        funded = wallet.walletcreatefundedpsbt(
            [
                {
                    "txid": utxo["txid"],
                    "vout": utxo["vout"],
                },
            ],
            {
                destination: Decimal("0.40"),
            },
            0,
            {
                "add_inputs": False,
            },
        )

        assert "psbt" in funded
        assert funded["fee"] > 0

        return funded["psbt"]

    def run_test(self):
        node = self.nodes[0]

        passphrase = "mercatura-pq-encryption-test"

        self.log.info(
            "Creating Mercatura PQ wallets"
        )

        node.createwallet(
            wallet_name="pq_encrypted_sender"
        )
        node.createwallet(
            wallet_name="pq_encryption_receiver"
        )

        sender = node.get_wallet_rpc(
            "pq_encrypted_sender"
        )
        receiver = node.get_wallet_rpc(
            "pq_encryption_receiver"
        )

        # ----------------------------------------------------------
        # Fund the sender while it is still unencrypted.
        # ----------------------------------------------------------

        mining_address = sender.getnewaddress(
            "encryption-mining"
        )

        self.assert_pq_address(
            sender,
            mining_address,
        )

        self.log.info(
            "Mining mature PQ funds"
        )

        self.mine_to_address(
            node,
            mining_address,
            COINBASE_MATURITY + 2,
        )

        spendable = sender.listunspent(
            1,
            9999999,
        )

        assert len(spendable) >= 2

        # Use two distinct mature inputs so the second PSBT remains
        # valid after the first encrypted-wallet spend is broadcast.
        first_utxo = spendable[0]
        second_utxo = spendable[1]

        assert first_utxo["txid"] != second_utxo["txid"]

        first_destination = receiver.getnewaddress(
            "encrypted-spend-1"
        )
        second_destination = receiver.getnewaddress(
            "encrypted-spend-2"
        )

        self.assert_pq_address(
            receiver,
            first_destination,
        )
        self.assert_pq_address(
            receiver,
            second_destination,
        )

        # Construct both transactions before encryption so locked
        # change-address derivation cannot obscure the signing test.
        first_psbt = self.make_funded_psbt(
            sender,
            first_utxo,
            first_destination,
        )

        second_psbt = self.make_funded_psbt(
            sender,
            second_utxo,
            second_destination,
        )

        # ----------------------------------------------------------
        # Encryption must leave the PQ wallet locked.
        # ----------------------------------------------------------

        self.log.info(
            "Encrypting Mercatura PQ wallet"
        )

        sender.encryptwallet(passphrase)

        assert_equal(
            sender.getwalletinfo()["unlocked_until"],
            0,
        )

        # Public PQ PSBT signing must not bypass wallet locking.
        assert_raises_rpc_error(
            -13,
            "Please enter the wallet passphrase "
            "with walletpassphrase first",
            sender.walletprocesspsbt,
            first_psbt,
        )

        # ----------------------------------------------------------
        # Unlock must restore PQ signing capability.
        # ----------------------------------------------------------

        self.log.info(
            "Unlocking and signing PQ spend"
        )

        sender.walletpassphrase(
            passphrase,
            60,
        )

        assert (
            sender.getwalletinfo()["unlocked_until"] > 0
        )

        processed = sender.walletprocesspsbt(
            psbt=first_psbt,
            finalize=False,
        )

        assert "psbt" in processed

        finalized = node.finalizepsbt(
            processed["psbt"]
        )

        assert_equal(
            finalized["complete"],
            True,
        )

        first_txid = node.sendrawtransaction(
            finalized["hex"]
        )

        confirmation_address = receiver.getnewaddress(
            "encryption-confirmation"
        )

        self.assert_pq_address(
            receiver,
            confirmation_address,
        )

        self.mine_to_address(
            node,
            confirmation_address,
            1,
        )

        assert (
            sender.gettransaction(
                first_txid
            )["confirmations"] > 0
        )

        # ----------------------------------------------------------
        # Explicit relock must remove PQ signing capability again.
        # ----------------------------------------------------------

        self.log.info(
            "Relocking Mercatura PQ wallet"
        )

        sender.walletlock()

        assert_equal(
            sender.getwalletinfo()["unlocked_until"],
            0,
        )

        assert_raises_rpc_error(
            -13,
            "Please enter the wallet passphrase "
            "with walletpassphrase first",
            sender.walletprocesspsbt,
            second_psbt,
        )

        # Unlock once more and prove the second PSBT itself is valid;
        # its prior failure was specifically caused by wallet locking.
        sender.walletpassphrase(
            passphrase,
            60,
        )

        second_processed = sender.walletprocesspsbt(
            psbt=second_psbt,
            finalize=False,
        )

        second_finalized = node.finalizepsbt(
            second_processed["psbt"]
        )

        assert_equal(
            second_finalized["complete"],
            True,
        )

        sender.walletlock()

        assert_equal(
            sender.getwalletinfo()["unlocked_until"],
            0,
        )

        self.log.info(
            "Mercatura PQ encrypted-wallet spending "
            "lifecycle passed"
        )


if __name__ == "__main__":
    MercaturaPQWalletEncryptionTest(__file__).main()
