#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""MCA-A-002: monetary bounds precede wallet recipient additions."""

from decimal import Decimal

from test_framework.messages import COIN
from test_framework.pq_lab_proxy import run_owned_test
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


MAX_BASE = 999999999999999999
LIMIT_ERROR = "Transaction amounts exceed the monetary safety limit"


class RecipientTotalsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.wallet_names = [False]
        self.extra_args = [["-mercahashv2height=0", "-mercaweaveheight=0"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        for name in ("sender", "receiver"):
            node.createwallet(name)
        sender, receiver = node.get_wallet_rpc("sender"), node.get_wallet_rpc("receiver")
        source = sender.getnewaddress()
        addresses = [receiver.getnewaddress() for _ in range(11)]
        grouped = receiver.getnewaddress("grouped", "mercaweave")
        self.generatetoaddress(node, 101, source, maxtries=1000)

        self.log.info("Reject sums above MAX_MONEY, including ten individually valid near-maximum recipients")
        for amounts in ([MAX_BASE - 2, 3], [MAX_BASE, 2], [MAX_BASE] * 10):
            # Include a genuine active-v3 destination alongside native-v2 outputs.
            outputs = {address: Decimal(amount) / COIN
                       for address, amount in zip([grouped] + addresses, amounts)}
            for subtract in (False, True):
                assert_raises_rpc_error(-6, LIMIT_ERROR, sender.sendmany, "", outputs,
                                        subtractfeefrom=list(outputs) if subtract else [])
                assert_raises_rpc_error(-4, LIMIT_ERROR, sender.send, outputs,
                                        options={"subtract_fee_from_outputs": [0] if subtract else []})
                options = {"subtractFeeFromOutputs": [0] if subtract else []}
                assert_raises_rpc_error(-4, LIMIT_ERROR, sender.walletcreatefundedpsbt, [], outputs, options=options)
                raw = node.createrawtransaction([], outputs)
                assert_raises_rpc_error(-4, LIMIT_ERROR, sender.fundrawtransaction, raw, options)
            assert_raises_rpc_error(-8, LIMIT_ERROR, sender.sendall,
                                    [{address: amount} for address, amount in outputs.items()] + [addresses[-1]])
        assert_equal(node.getrawmempool(), [])

        self.log.info("Exactly MAX_MONEY passes range validation and fails only for unavailable funds")
        for amounts in ([MAX_BASE], [MAX_BASE // 2, MAX_BASE - MAX_BASE // 2]):
            outputs = {address: Decimal(amount) / COIN for address, amount in zip(addresses, amounts)}
            for method, args in ((sender.sendmany, ("", outputs)),
                                 (sender.walletcreatefundedpsbt, ([], outputs))):
                try:
                    method(*args)
                except Exception as exc:
                    assert "Insufficient funds" in str(exc), str(exc)
                    assert LIMIT_ERROR not in str(exc)
                else:
                    raise AssertionError("Disposable wallet cannot fund MAX_MONEY")

        self.log.info("Zero, negative and individually excessive amounts retain rejection")
        assert_raises_rpc_error(-6, "Transaction amount too small", sender.sendtoaddress, addresses[0], 0)
        assert_raises_rpc_error(-3, "Amount out of range", sender.sendtoaddress, addresses[0], Decimal("-0.01"))
        assert_raises_rpc_error(-3, "Invalid amount", sender.sendtoaddress, addresses[0], Decimal(MAX_BASE + 1) / COIN)

        self.log.info("Normal single and batched payments, fee subtraction and funded PSBTs remain valid")
        sender.sendtoaddress(addresses[0], Decimal("1"))
        outputs = {addresses[1]: Decimal("1"), grouped: Decimal("2")}
        txid = sender.sendmany("", outputs, subtractfeefrom=list(outputs))
        info = sender.gettransaction(txid)
        decoded = node.decoderawtransaction(info["hex"])
        paid = sum(out["value"] for out in decoded["vout"]
                   if out["scriptPubKey"].get("address") in outputs)
        assert_equal(paid, Decimal("3") + info["fee"])
        funded = sender.walletcreatefundedpsbt([], outputs, options={"subtractFeeFromOutputs": [0, 1]})
        processed = sender.walletprocesspsbt(funded["psbt"])
        assert processed["complete"]
        final = node.finalizepsbt(processed["psbt"])
        assert final["complete"] and node.testmempoolaccept([final["hex"]])[0]["allowed"]
        node.sendrawtransaction(final["hex"])
        self.generatetoaddress(node, 1, source)
        # An explicit zero-value data output remains a supported raw workflow.
        raw = node.createrawtransaction([], {addresses[2]: Decimal("1"), "data": "4233"})
        funded = sender.fundrawtransaction(raw)
        signed = sender.signrawtransactionwithwallet(funded["hex"])
        assert signed["complete"] and node.testmempoolaccept([signed["hex"]])[0]["allowed"]


if __name__ == "__main__":
    run_owned_test(RecipientTotalsTest(__file__))
