#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""MCA-A-005: wallet creation and signing reject disabled ownership templates."""

from decimal import Decimal

from test_framework.address import (
    address_to_scriptpubkey, key_to_p2pkh, key_to_p2wpkh, program_to_witness,
    script_to_p2sh, script_to_p2wsh,
)
from test_framework.key import ECKey
from test_framework.messages import COIN, CTransaction, CTxOut, tx_from_hex
from test_framework.pq_lab_proxy import run_owned_test
from test_framework.script import CScript, OP_CHECKMULTISIG, OP_CHECKSIG, OP_RETURN, OP_TRUE, taproot_construct
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


ERROR = "disabled classical signature authorization"
AMOUNT = Decimal("0.10")


class ClassicalPaymentSafetyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.wallet_names = [False]
        self.extra_args = [["-mercahashv2height=0", "-mercaweaveheight=1000"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    @staticmethod
    def raw_output(script, amount=int(AMOUNT * COIN)):
        tx = CTransaction()
        tx.vout = [CTxOut(amount, script)]
        return tx.serialize().hex()

    def run_test(self):
        node = self.nodes[0]
        for name in ("sender", "receiver"):
            node.createwallet(name, load_on_startup=True)
        sender, receiver = node.get_wallet_rpc("sender"), node.get_wallet_rpc("receiver")
        source, pq = sender.getnewaddress(), receiver.getnewaddress()
        self.generatetoaddress(node, 101, source, maxtries=1000)
        key = ECKey()
        key.set(bytes.fromhex("00" * 31 + "01"), compressed=True)
        pubkey = key.get_pubkey().get_bytes()
        classical = [key_to_p2pkh(pubkey), key_to_p2wpkh(pubkey)]

        self.log.info("Single sends, batching, funding, funded PSBTs and custom change reject disabled outputs")
        for address in classical:
            assert node.validateaddress(address)["isvalid"]  # Keep general address decoding intact.
            assert_raises_rpc_error(-6, ERROR, sender.sendtoaddress, address, AMOUNT)
            assert_raises_rpc_error(-6, ERROR, sender.sendmany, "", {pq: AMOUNT, address: AMOUNT})
            assert_raises_rpc_error(-4, ERROR, sender.send, [{pq: AMOUNT}, {address: AMOUNT}])
            assert_raises_rpc_error(-4, ERROR, sender.walletcreatefundedpsbt, [], [{pq: AMOUNT}, {address: AMOUNT}])
            raw = node.createrawtransaction([], {address: AMOUNT})
            assert_raises_rpc_error(-4, ERROR, sender.fundrawtransaction, raw)
            safe_raw = node.createrawtransaction([], {pq: AMOUNT})
            assert_raises_rpc_error(-4, ERROR, sender.fundrawtransaction, safe_raw, {"changeAddress": address})
            assert_raises_rpc_error(-4, ERROR, sender.walletcreatefundedpsbt, [], {pq: AMOUNT},
                                    options={"changeAddress": address})
            # sendall creates transactions without CreateTransaction; its wallet
            # PSBT signing boundary must apply the same output restriction.
            assert_raises_rpc_error(-8, "does not support PSBT", sender.sendall, [address],
                                    options={"psbt": True, "add_to_wallet": False})

        self.log.info("Raw P2PK/multisig templates and externally constructed PSBTs cannot bypass signing safety")
        disabled_scripts = [address_to_scriptpubkey(a) for a in classical] + [
            CScript([pubkey, OP_CHECKSIG]), CScript([1, pubkey, 1, OP_CHECKMULTISIG]),
        ]
        coin = sender.listunspent(1)[0]
        for script in disabled_scripts:
            assert_raises_rpc_error(-4, ERROR, sender.fundrawtransaction, self.raw_output(script))
            raw = node.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {pq: AMOUNT})
            tx = tx_from_hex(raw)
            tx.vout[0].scriptPubKey = script
            raw = tx.serialize().hex()
            signed = sender.signrawtransactionwithwallet(raw)
            assert not signed["complete"]
            assert ERROR in signed["errors"][0]["error"]
            psbt = node.converttopsbt(raw)
            metadata = sender.walletprocesspsbt(psbt, sign=False)
            assert not metadata["complete"]  # Metadata-only processing remains available.
            assert_raises_rpc_error(-8, "does not support PSBT", sender.walletprocesspsbt, metadata["psbt"])

        self.log.info("Explicit raw scripts, OP_RETURN and non-signature script containers remain fundable and signable")
        anyone = CScript([OP_TRUE])
        taproot = taproot_construct(pubkey[1:], [("non_signature", anyone)])
        controls = [anyone, CScript([OP_RETURN, b"MCA-A-005"]),
                    address_to_scriptpubkey(script_to_p2sh(anyone)),
                    address_to_scriptpubkey(script_to_p2wsh(anyone)), taproot.scriptPubKey]
        for script in controls:
            raw = self.raw_output(script, 0 if script[0] == OP_RETURN else int(AMOUNT * COIN))
            funded = sender.fundrawtransaction(raw)
            signed = sender.signrawtransactionwithwallet(funded["hex"])
            assert signed["complete"]
            assert any(out.scriptPubKey == script for out in tx_from_hex(signed["hex"]).vout)

        self.log.info("PQ v2 batching, funded PSBT signing, and broadcasting still work")
        funded = sender.walletcreatefundedpsbt([], [{pq: AMOUNT}, {receiver.getnewaddress(): AMOUNT}])
        processed = sender.walletprocesspsbt(funded["psbt"])
        assert processed["complete"]
        final = node.finalizepsbt(processed["psbt"])
        assert final["complete"] and node.testmempoolaccept([final["hex"]])[0]["allowed"]
        node.sendrawtransaction(final["hex"])
        self.generatetoaddress(node, 1, source)

        inactive = program_to_witness(3, bytes([1]) * 32)
        assert_raises_rpc_error(-6, "not active", sender.sendtoaddress, inactive, AMOUNT)
        assert_raises_rpc_error(-4, "not active", sender.walletcreatefundedpsbt, [], {inactive: AMOUNT})

        self.log.info("Active MercaWeave payments and grouped-input signing remain available")
        self.restart_node(0, extra_args=["-mercahashv2height=0", "-mercaweaveheight=0"])
        sender, receiver = node.get_wallet_rpc("sender"), node.get_wallet_rpc("receiver")
        grouped = receiver.getnewaddress("active", "mercaweave")
        assert_equal(receiver.getaddressinfo(grouped)["witness_version"], 3)
        sender.sendmany("", {grouped: Decimal("1"), pq: AMOUNT})
        self.generatetoaddress(node, 1, source)
        funded = receiver.walletcreatefundedpsbt([], {source: AMOUNT},
                    options={"spending_mode": "v3", "fee_rate": Decimal("0.001")})
        processed = receiver.walletprocesspsbt(funded["psbt"])
        assert processed["complete"]
        final = node.finalizepsbt(processed["psbt"])
        assert final["complete"] and node.testmempoolaccept([final["hex"]])[0]["allowed"]
        node.sendrawtransaction(final["hex"])
        self.generatetoaddress(node, 1, source)


if __name__ == "__main__":
    run_owned_test(ClassicalPaymentSafetyTest(__file__))
