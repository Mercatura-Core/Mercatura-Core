#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license.
"""Bounded modified-fee state, PQ replacement/package relay and wallet errors."""

from decimal import Decimal

from test_framework.messages import tx_from_hex
from test_framework.psbt import PSBT, PSBT_IN_WITNESS_UTXO
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

MAX = (1 << 63) - 1
MIN = -(1 << 63)
MAX_MONEY = 999_999_999_999_999_999


class ModifiedFeesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.extra_args = [["-checkmempool=1", "-fallbackfee=0.01"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def spend(self, wallet, coin, fee, previous=None):
        node = self.nodes[0]
        raw = node.createrawtransaction(
            [{"txid": coin["txid"], "vout": coin["vout"]}],
            {wallet.getnewaddress(): coin["amount"] - fee},
        )
        partial = PSBT.from_base64(node.converttopsbt(raw))
        if previous is not None:
            partial.i[0].map[PSBT_IN_WITNESS_UTXO] = tx_from_hex(previous).vout[coin["vout"]].serialize()
        signed = wallet.walletprocesspsbt(partial.to_base64())
        assert_equal(signed["complete"], True)
        final = node.finalizepsbt(signed["psbt"])
        assert_equal(final["complete"], True)
        return final["hex"]

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("fees", load_on_startup=True)
        wallet = node.get_wallet_rpc("fees")
        address = wallet.getnewaddress()
        assert address.startswith("mcrt1z")
        for count in [10] * 10 + [3]:
            self.generatetoaddress(node, count, address)
        coins = wallet.listunspent(101)
        assert_equal(len(coins), 3)

        original = self.spend(wallet, coins[0], Decimal("0.20"))
        txid = node.sendrawtransaction(original)
        candidate = self.spend(wallet, coins[1], Decimal("0.20"))
        candidate_id = node.sendrawtransaction(candidate)
        self.log.info("Checking atomic priority rejection and admission rollback")
        node.prioritisetransaction(txid, 0, MAX - 40)
        before = node.getmempoolentry(txid)
        assert_raises_rpc_error(-8, "Modified fee aggregate is out of range", node.prioritisetransaction, txid, 0, 1)
        assert_equal(node.getmempoolentry(txid)["fees"], before["fees"])

        pending = self.spend(wallet, coins[2], Decimal("0.20"))
        rejected = node.testmempoolaccept([pending])[0]
        assert_equal(rejected["allowed"], False)
        assert_equal(rejected["reject-reason"], "modified-fee-out-of-range")
        assert_equal(set(node.getrawmempool()), {txid, candidate_id})
        node.prioritisetransaction(txid, 0, -(MAX - 40))
        pending_id = node.decoderawtransaction(pending)["txid"]
        node.prioritisetransaction(pending_id, 0, MIN)
        assert_equal(node.testmempoolaccept([pending])[0]["reject-reason"], "modified-fee-out-of-range")
        node.prioritisetransaction(pending_id, 0, MAX)
        node.prioritisetransaction(pending_id, 0, 1)
        assert_equal(node.testmempoolaccept([pending])[0]["allowed"], True)
        for delta in (10, -20, 10):
            node.prioritisetransaction(txid, 0, delta)
        assert_equal(node.getmempoolentry(txid)["fees"]["modified"], Decimal("0.20"))

        self.log.info("Checking normal PQ RBF and persisted fee state")
        replacement = self.spend(wallet, coins[0], Decimal("0.40"))
        replacement_id = node.sendrawtransaction(replacement)
        assert txid not in node.getrawmempool()
        node.prioritisetransaction(replacement_id, 0, 7)
        self.restart_node(0)
        node = self.nodes[0]
        wallet = node.get_wallet_rpc("fees")
        assert_equal(set(node.getrawmempool()), {replacement_id, candidate_id})
        assert_equal(node.getmempoolentry(replacement_id)["fees"]["modified"], Decimal("0.47"))
        node.prioritisetransaction(replacement_id, 0, -7)

        self.log.info("Checking controlled wallet failure for an unpayable bump")
        node.prioritisetransaction(replacement_id, 0, -2 * MAX_MONEY)
        assert_raises_rpc_error(
            -4, "Selected input fees are out of range", wallet.walletcreatefundedpsbt,
            [{"txid": replacement_id, "vout": 0}], [{address: Decimal("1.00")}], 0,
            {"add_inputs": False, "fee_rate": Decimal("0.001")},
        )
        node.prioritisetransaction(replacement_id, 0, 2 * MAX_MONEY)

        self.log.info("Checking normal PQ parent/child package acceptance")
        parent = self.spend(wallet, coins[2], Decimal("0.00"))
        parent_id = node.decoderawtransaction(parent)["txid"]
        child_coin = {"txid": parent_id, "vout": 0, "amount": coins[2]["amount"]}
        child = self.spend(wallet, child_coin, Decimal("0.30"), previous=parent)
        package = node.submitpackage([parent, child])
        assert_equal(package["package_msg"], "success")
        assert_equal(node.getmempoolinfo()["size"], 4)
        self.generatetoaddress(node, 1, address)
        assert_equal(node.getrawmempool(), [])


if __name__ == "__main__":
    ModifiedFeesTest(__file__).main()
