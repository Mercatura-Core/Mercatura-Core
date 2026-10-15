#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

"""Native PQ encryption/passphrase persistence with SQLite faults and restarts."""

from decimal import Decimal
import sqlite3

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


OLD = "old-pq-persistence-passphrase"
NEW = "new-pq-persistence-passphrase"


class WalletEncryptionPersistenceTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.rpc_timeout = 180
        self.extra_args = [["-fallbackfee=0.01", "-keypool=5"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine(self, count, address):
        while count:
            batch = min(10, count)
            self.generatetoaddress(self.nodes[0], batch, address)
            count -= batch
        self.nodes[0].syncwithvalidationinterfacequeue()

    def snapshot(self, wallet):
        return {
            "balance": wallet.getbalances()["mine"],
            "utxos": sorted((c["txid"], c["vout"], c["amount"], c["scriptPubKey"])
                            for c in wallet.listunspent()),
            "history": sorted((t["txid"], t["category"], t.get("address", ""), t["amount"])
                              for t in wallet.listtransactions("*", 1000)),
        }

    def fail_master_key_writes(self, name, *, enabled):
        # Faults exist only in a disposable test database, edited while the
        # daemon is stopped. No fault switch or RPC is added to production.
        node = self.nodes[0]
        self.stop_node(0)
        with sqlite3.connect(node.wallets_path / name / "wallet.dat") as database:
            if enabled:
                database.execute("""CREATE TRIGGER fail_master_key BEFORE INSERT ON main
                    WHEN substr(NEW.key, 1, 5) = X'046d6b6579'
                    BEGIN SELECT RAISE(ABORT, 'test master-key write failure'); END""")
            else:
                database.execute("DROP TRIGGER fail_master_key")
        self.start_node(0)
        return node.get_wallet_rpc(name)

    def check_passphrases(self, wallet, valid, invalid):
        assert_equal(wallet.getwalletinfo()["unlocked_until"], 0)
        assert_raises_rpc_error(-14, "passphrase", wallet.walletpassphrase, invalid, 30)
        wallet.walletpassphrase(valid, 30)
        wallet.walletlock()

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("source", load_on_startup=True)
        node.createwallet("receiver", load_on_startup=True)
        source = node.get_wallet_rpc("source")
        receiver = node.get_wallet_rpc("receiver")
        mining = source.getnewaddress()
        receive = source.getnewaddress()
        change = source.getrawchangeaddress()
        destination = receiver.getnewaddress()
        assert all(address.startswith("mcrt1z") for address in (mining, receive, change, destination))
        self.mine(101, mining)
        self_txid = source.sendtoaddress(receive, Decimal("1.00"))
        self.mine(1, destination)
        assert source.gettransaction(self_txid)["confirmations"] > 0
        assert source.getbalance() > 0
        assert any(source.getaddressinfo(c["address"])["ischange"] for c in source.listunspent())
        source.encryptwallet(OLD)
        expected = self.snapshot(source)

        self.log.info("Failed passphrase replacement must preserve live and persisted PQ ownership and funds")
        source = self.fail_master_key_writes("source", enabled=True)
        assert_raises_rpc_error(-20, "replacement wallet master key", source.walletpassphrasechange, OLD, NEW)
        self.check_passphrases(source, OLD, NEW)
        assert_equal(self.snapshot(source), expected)
        self.restart_node(0)
        source = node.get_wallet_rpc("source")
        self.check_passphrases(source, OLD, NEW)
        assert_equal(self.snapshot(source), expected)
        for address in (mining, receive, change):
            assert_equal(source.getaddressinfo(address)["ismine"], True)

        self.log.info("Successful replacement must persist, retain funds, and still authorize a native PQ spend")
        source = self.fail_master_key_writes("source", enabled=False)
        assert_equal(source.walletpassphrasechange(OLD, NEW), None)
        self.check_passphrases(source, NEW, OLD)
        assert_equal(self.snapshot(source), expected)
        self.restart_node(0)
        source = node.get_wallet_rpc("source")
        receiver = node.get_wallet_rpc("receiver")
        self.check_passphrases(source, NEW, OLD)
        assert_equal(self.snapshot(source), expected)
        source.walletpassphrase(NEW, 60)
        txid = source.sendtoaddress(destination, Decimal("1.00"))
        source.walletlock()
        self.mine(1, destination)
        assert source.gettransaction(txid)["confirmations"] > 0
        assert_equal(receiver.getbalance(), Decimal("1.00"))

        self.log.info("Rejected initial-encryption writes must retain the plaintext wallet and its funds")
        node.createwallet("initial", load_on_startup=True)
        initial = node.get_wallet_rpc("initial")
        initial_address = initial.getnewaddress()
        funding_txid = receiver.sendtoaddress(initial_address, Decimal("0.40"))
        self.mine(1, destination)
        assert initial.gettransaction(funding_txid)["confirmations"] > 0
        assert_equal(initial.getbalance(), Decimal("0.40"))
        initial_expected = self.snapshot(initial)
        initial = self.fail_master_key_writes("initial", enabled=True)
        assert_raises_rpc_error(-16, "persist wallet encryption master key", initial.encryptwallet, OLD)
        assert "unlocked_until" not in initial.getwalletinfo()
        assert_equal(self.snapshot(initial), initial_expected)
        assert_equal(initial.getaddressinfo(initial_address)["ismine"], True)
        after_failure_address = initial.getnewaddress()
        assert after_failure_address.startswith("mcrt1z")
        self.restart_node(0)
        initial = node.get_wallet_rpc("initial")
        assert "unlocked_until" not in initial.getwalletinfo()
        assert_equal(self.snapshot(initial), initial_expected)
        assert_equal(initial.getaddressinfo(after_failure_address)["ismine"], True)

        self.log.info("Retry encryption, restart, and spend the preserved native PQ output")
        initial = self.fail_master_key_writes("initial", enabled=False)
        initial.encryptwallet(OLD)
        self.check_passphrases(initial, OLD, NEW)
        assert_equal(self.snapshot(initial), initial_expected)
        self.restart_node(0)
        initial = node.get_wallet_rpc("initial")
        self.check_passphrases(initial, OLD, NEW)
        assert_equal(self.snapshot(initial), initial_expected)
        initial.walletpassphrase(OLD, 60)
        initial_spend = initial.sendtoaddress(destination, Decimal("0.10"))
        initial.walletlock()
        self.mine(1, destination)
        details = initial.gettransaction(initial_spend)
        assert details["confirmations"] > 0
        assert_equal(initial.getbalance(), Decimal("0.30") + details["fee"])
        assert initial.gettransaction(funding_txid)["confirmations"] > 0


if __name__ == "__main__":
    WalletEncryptionPersistenceTest(__file__).main()
