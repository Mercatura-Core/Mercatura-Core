#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Ordinary native-v2 conflicting branches, descendants and reconsideration.

Runs one new temporary regtest node with synchronous SQLite writes. No research
macro, v3 script, activation override or existing wallet/datadir is used.
"""

from decimal import Decimal
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "test/functional"))
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class NativeLifecycle(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.extra_args = [["-unsafesqlitesync=0", "-keypool=5"]]
        self.rpc_timeout = 180

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("wo07-native")
        wallet = node.get_wallet_rpc("wo07-native")
        mining = wallet.getnewaddress()
        assert_equal(wallet.getaddressinfo(mining)["witness_version"], 2)
        for count in [10] * 10 + [1]:
            self.generatetoaddress(node, count, mining)
        coin = wallet.listunspent()[0]
        parent_address = wallet.getnewaddress()
        child_address = wallet.getnewaddress()
        competing_address = wallet.getnewaddress()

        def signed(point, recipient, value):
            raw = node.createrawtransaction(
                [dict(point, sequence=0xFFFFFFFD)], {recipient: value}
            )
            result = wallet.signrawtransactionwithwallet(raw)
            assert_equal(result["complete"], True)
            decoded = node.decoderawtransaction(result["hex"])
            assert_equal(decoded["vin"][0].get("scriptSig", {}).get("hex", ""), "")
            return result["hex"], decoded["txid"]

        point = {"txid": coin["txid"], "vout": coin["vout"]}
        amount = coin["amount"] - Decimal("0.10")
        parent, parent_id = signed(point, parent_address, amount)
        competing, competing_id = signed(
            point, competing_address, coin["amount"] - Decimal("0.50")
        )
        assert_equal(node.testmempoolaccept([parent])[0]["allowed"], True)
        assert_equal(node.sendrawtransaction(parent), parent_id)
        node.syncwithvalidationinterfacequeue()
        parent_vout = next(
            out["n"]
            for out in node.decoderawtransaction(parent)["vout"]
            if out["scriptPubKey"].get("address") == parent_address
        )
        child, child_id = signed(
            {"txid": parent_id, "vout": parent_vout},
            child_address,
            amount - Decimal("0.10"),
        )
        assert_equal(node.sendrawtransaction(child), child_id)
        assert_equal(set(node.getmempoolancestors(child_id)), {parent_id})
        block_a = self.generatetoaddress(node, 1, mining)[0]
        node.syncwithvalidationinterfacequeue()
        assert_equal(wallet.gettransaction(child_id)["confirmations"], 1)
        state_a = node.gettxoutsetinfo()["hash_serialized_3"]
        assert node.gettxout(child_id, 0, False) is not None

        node.invalidateblock(block_a)
        assert_equal(node.getblockcount(), 101)
        assert {parent_id, child_id}.issubset(node.getrawmempool())
        assert_equal(node.sendrawtransaction(competing), competing_id)
        assert_equal(node.getrawmempool(), [competing_id])
        block_b = self.generatetoaddress(node, 1, mining)[0]
        node.syncwithvalidationinterfacequeue()
        assert wallet.gettransaction(parent_id)["confirmations"] < 0
        assert wallet.gettransaction(child_id)["confirmations"] < 0
        assert_equal(node.gettxout(child_id, 0, False), None)
        assert node.gettxout(competing_id, 0, False) is not None

        node.reconsiderblock(block_a)
        node.invalidateblock(block_b)
        assert_equal(node.getbestblockhash(), block_a)
        node.reconsiderblock(block_b)
        assert_equal(node.getbestblockhash(), block_a)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], state_a)
        node.syncwithvalidationinterfacequeue()
        assert_equal(wallet.gettransaction(parent_id)["confirmations"], 1)
        assert_equal(wallet.gettransaction(child_id)["confirmations"], 1)
        assert wallet.gettransaction(competing_id)["confirmations"] < 0
        assert_equal(
            node.testmempoolaccept([competing])[0]["reject-reason"], "missing-inputs"
        )
        self.restart_node(0)
        self.nodes[0].loadwallet("wo07-native")
        wallet = self.nodes[0].get_wallet_rpc("wo07-native")
        assert_equal(self.nodes[0].getbestblockhash(), block_a)
        assert_equal(self.nodes[0].gettxoutsetinfo()["hash_serialized_3"], state_a)
        assert_equal(wallet.gettransaction(child_id)["confirmations"], 1)
        self.log.info(
            "WO07_NATIVE_LIFECYCLE_PASS: A, disconnect, B conflict, reconsider, A, exact UTXO hash, wallet descendants, restart"
        )


if __name__ == "__main__":
    NativeLifecycle(__file__).main()
