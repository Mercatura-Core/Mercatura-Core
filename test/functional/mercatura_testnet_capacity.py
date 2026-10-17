#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license.

"""Public-testnet software before activation: templates, PQ signing and recovery.

Runs real public-testnet parameters in an isolated genesis-only datadir, without
mining thousands of public-difficulty MercaHash blocks. Boundary validation at
7999/8000/8001 is in testnet_capacity_tests; large-block relay/recovery is covered
by mercatura_block_capacity.py and mercatura_pq_capacity.py on regtest.
"""

from decimal import Decimal

from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.messages import COutPoint, CTransaction, CTxIn, CTxOut, from_hex
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaTestnetCapacityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.chain = "testnet"
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.extra_args = [["-dnsseed=0", "-fixedseeds=0", "-mocktime=1791352800"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        genesis = "b3fc707c9a89dc45d1cf6a837284b22ad2b6fd41b99af601079db9dcbeff6920"
        assert_equal(node.getbestblockhash(), genesis)
        node.createwallet("preactivation", load_on_startup=True)
        wallet = node.get_wallet_rpc("preactivation")
        address = wallet.getnewaddress()
        assert address.startswith("tmca1z")
        script = CScript(bytes.fromhex(wallet.getaddressinfo(address)["scriptPubKey"]))
        template = node.getblocktemplate({"rules": ["segwit"]})
        assert_equal(template["height"], 1)
        assert_equal(template["sizelimit"], 1_048_576)
        assert_equal(template["weightlimit"], 4_194_304)
        cb = create_coinbase(1, script_pubkey=script, nValue=0)
        block = create_block(coinbase=cb, tmpl=template)
        add_witness_commitment(block)
        assert_equal(node.getblocktemplate({"mode": "proposal", "data": block.serialize().hex(), "rules": ["segwit"]}), None)

        # Supplied prevouts exercise actual wallet ML-DSA signing without mining
        # public-difficulty coinbases. These coins are synthetic, never broadcast.
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(1, 0))]
        tx.vout = [CTxOut(900, script)]
        previous = {"txid": "0" * 63 + "1", "vout": 0, "scriptPubKey": script.hex(), "amount": Decimal("10.00")}
        signed = wallet.signrawtransactionwithwallet(tx.serialize().hex(), [previous])
        assert_equal(signed["complete"], True)
        pq = from_hex(CTransaction(), signed["hex"])
        assert_equal([len(item) for item in pq.wit.vtxinwit[0].scriptWitness.stack], [3309, 1952])

        # The proposal path enforces the historical capacity before UTXO lookup.
        # Padding inputs are deliberately unavailable; this is a capacity-rejection
        # test, not acceptance evidence for these synthetic transactions.
        for target in [1_048_577, 4_194_304, 4_194_305]:
            candidate = create_block(coinbase=create_coinbase(1, script_pubkey=script, nValue=0), tmpl=template, txlist=[pq])
            count = (target + 899_999) // 900_000
            for index in range(count):
                padding = CTransaction()
                padding.vin = [CTxIn(COutPoint(2, index))]
                padding.vout = [CTxOut(0, CScript([OP_RETURN, bytes(100_000 if index + 1 == count else 900_000)]))]
                candidate.vtx.append(padding)
            add_witness_commitment(candidate)
            extra = target - len(candidate.serialize())
            candidate.vtx[-1].vout[0].scriptPubKey = CScript([OP_RETURN, bytes(100_000 + extra)])
            candidate.vtx[0].vout.pop()
            add_witness_commitment(candidate)
            assert_equal(len(candidate.serialize()), target)
            assert_equal(node.getblocktemplate({"mode": "proposal", "data": candidate.serialize().hex(), "rules": ["segwit"]}), "bad-blk-capacity")
        assert_equal(node.getbestblockhash(), genesis)
        stats = node.gettxoutsetinfo()["hash_serialized_3"]
        self.restart_node(0, extra_args=self.extra_args[0] + ["-reindex"])
        wallet = node.get_wallet_rpc("preactivation")
        assert_equal(node.getbestblockhash(), genesis)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], stats)
        assert_equal(wallet.getaddressinfo(address)["ismine"], True)
        assert_equal(wallet.getbalance(), Decimal("0.00"))
        assert_equal(wallet.listunspent(), [])
        assert_equal(wallet.listtransactions(), [])
        assert_equal(node.getblocktemplate({"rules": ["segwit"]})["sizelimit"], 1_048_576)


if __name__ == "__main__":
    MercaturaTestnetCapacityTest(__file__).main()
