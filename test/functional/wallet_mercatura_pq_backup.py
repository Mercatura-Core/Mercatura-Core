#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license.

"""Mercatura PQ wallet backup/restore functional coverage."""

from decimal import Decimal

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


PQ_ADDRESS_PREFIX = "mcrt1z"
PQ_SCRIPT_TYPE = "witness_v2_mercatura_pq"


class MercaturaPQWalletBackupTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

        self.extra_args = [
            ["-fallbackfee=0.01"],
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

    def assert_transaction_outputs_are_pq(self, wallet, txid):
        wallet_tx = wallet.gettransaction(txid)
        decoded = wallet.decoderawtransaction(wallet_tx["hex"])

        assert len(decoded["vout"]) > 0

        for output in decoded["vout"]:
            assert_equal(
                output["scriptPubKey"]["type"],
                PQ_SCRIPT_TYPE,
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

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]

        self.log.info("Creating source and recipient PQ wallets")

        node0.createwallet(wallet_name="pq_backup_source")
        node0.createwallet(wallet_name="pq_backup_recipient")

        source = node0.get_wallet_rpc("pq_backup_source")
        recipient = node0.get_wallet_rpc("pq_backup_recipient")

        # ----------------------------------------------------------
        # Establish PQ derivation state before the backup.
        # ----------------------------------------------------------

        self.log.info("Creating PQ state before wallet backup")

        backed_receive = source.getnewaddress(
            "backed-receive"
        )
        backed_change = source.getrawchangeaddress()

        self.assert_pq_address(
            source,
            backed_receive,
        )
        self.assert_pq_address(
            source,
            backed_change,
        )

        assert backed_receive != backed_change

        backup_file = (
            node0.datadir_path /
            "mercatura_pq_wallet_backup.dat"
        )

        self.log.info("Backing up PQ wallet")

        source.backupwallet(backup_file)

        # Derive the next external and internal destinations only
        # after the backup. A wallet restored from that backup must
        # reproduce these exact addresses if its PQ master seed,
        # derivation version, account, branch, and indexes survived.
        expected_next_receive = source.getnewaddress(
            "expected-next-receive"
        )
        expected_next_change = source.getrawchangeaddress()

        self.assert_pq_address(
            source,
            expected_next_receive,
        )
        self.assert_pq_address(
            source,
            expected_next_change,
        )

        assert expected_next_receive not in {
            backed_receive,
            backed_change,
        }

        assert expected_next_change not in {
            backed_receive,
            backed_change,
            expected_next_receive,
        }

        # ----------------------------------------------------------
        # Create wallet activity after the backup was taken.
        #
        # The restored wallet cannot know this transaction history
        # from its backup file. It must discover the outputs during
        # restore/rescan using the backed-up PQ ownership material.
        # ----------------------------------------------------------

        self.log.info(
            "Mining post-backup funds to backed-up PQ destination"
        )

        self.mine_to_address(
            node0,
            backed_receive,
            COINBASE_MATURITY + 1,
        )

        self.sync_blocks()

        source_balance = source.getbalance()

        assert source_balance > 0

        # ----------------------------------------------------------
        # Restore the backup into node 1's separate regtest datadir.
        # restorewallet loads the copied database and performs the
        # required wallet rescan against node 1's chain.
        # ----------------------------------------------------------

        self.log.info(
            "Restoring PQ backup on separate node/datadir"
        )

        restore_result = node1.restorewallet(
            "pq_backup_restored",
            backup_file,
        )

        assert_equal(
            restore_result["name"],
            "pq_backup_restored",
        )

        restored = node1.get_wallet_rpc(
            "pq_backup_restored"
        )

        restored.syncwithvalidationinterfacequeue()

        # The backed-up destinations must still be recognized as PQ
        # wallet ownership after restore.
        self.assert_pq_address(
            restored,
            backed_receive,
        )
        self.assert_pq_address(
            restored,
            backed_change,
        )

        # Funds created only after the backup must have been found by
        # the restore rescan.
        assert_equal(
            restored.getbalance(),
            source_balance,
        )

        # ----------------------------------------------------------
        # Prove deterministic PQ state was restored.
        #
        # These are the first external/internal addresses generated
        # from the restored backup, so they must exactly equal the
        # addresses independently derived on the source immediately
        # after the backup was taken.
        # ----------------------------------------------------------

        restored_next_receive = restored.getnewaddress(
            "restored-next-receive"
        )
        restored_next_change = restored.getrawchangeaddress()

        self.assert_pq_address(
            restored,
            restored_next_receive,
        )
        self.assert_pq_address(
            restored,
            restored_next_change,
        )

        assert_equal(
            restored_next_receive,
            expected_next_receive,
        )

        assert_equal(
            restored_next_change,
            expected_next_change,
        )

        # ----------------------------------------------------------
        # Finally prove restored PQ private material is usable for an
        # actual ML-DSA spend, not merely address recognition.
        # ----------------------------------------------------------

        self.log.info(
            "Spending funds from restored PQ wallet"
        )

        recipient_address = recipient.getnewaddress(
            "restore-recipient"
        )

        self.assert_pq_address(
            recipient,
            recipient_address,
        )

        received_before = recipient.getreceivedbyaddress(
            recipient_address
        )

        restored_txid = restored.sendtoaddress(
            recipient_address,
            Decimal("0.10"),
        )

        self.sync_mempools()

        self.assert_transaction_outputs_are_pq(
            restored,
            restored_txid,
        )

        confirmation_address = source.getnewaddress(
            "restore-confirmation-miner"
        )

        self.assert_pq_address(
            source,
            confirmation_address,
        )

        self.mine_to_address(
            node0,
            confirmation_address,
            1,
        )

        self.sync_blocks()

        assert (
            restored.gettransaction(
                restored_txid
            )["confirmations"] > 0
        )

        assert_equal(
            recipient.getreceivedbyaddress(
                recipient_address
            ),
            received_before + Decimal("0.10"),
        )

        self.log.info(
            "Mercatura PQ backup/restore workflow passed"
        )


if __name__ == "__main__":
    MercaturaPQWalletBackupTest(__file__).main()
