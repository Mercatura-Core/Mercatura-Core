#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Exact 4 MiB regtest capacity, native chunk relay and persisted large blocks.

Synthetic padding is deliberate here; real ML-DSA workload measurements live in
mercatura_pq_capacity.py. Each stripped transaction stays below 1 MB.
"""

from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase, create_tx_with_script
from test_framework.messages import CBlock, CTxOut, from_hex
from test_framework.script import CScript, OP_RETURN, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

INITIAL_CAPACITY = 4_194_304


class MercaturaBlockCapacityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.uses_wallet = False
        self.rpc_timeout = 180
        self.extra_args = [["-debug=net", "-checkblockindex=1"] for _ in range(2)]

    @staticmethod
    def padded_spend(prev_tx, size):
        tx = create_tx_with_script(prev_tx, 0, amount=prev_tx.vout[0].nValue, output_script=CScript([OP_TRUE]))
        tx.vout.append(CTxOut(0, CScript([OP_RETURN, bytes(100_000)])))
        payload_size = 100_000 + size - len(tx.serialize())
        assert payload_size > 65_535  # Stable pushdata and CompactSize widths.
        tx.vout[-1].scriptPubKey = CScript([OP_RETURN, bytes(payload_size)])
        assert_equal(len(tx.serialize()), size)
        assert len(tx.serialize_without_witness()) < 1_000_000
        return tx

    def exact_block(self, prev_tx, target):
        node = self.nodes[0]
        template = node.getblocktemplate({"rules": ["segwit"]})
        assert_equal(template["sizelimit"], INITIAL_CAPACITY)
        txs = []
        # At most 900 kB per transaction; transaction count stays below 253.
        for _ in range((target + 899_999) // 900_000 - 1):
            txs.append(self.padded_spend(prev_tx, 900_000))
            prev_tx = txs[-1]
        txs.append(self.padded_spend(prev_tx, 100_000))
        cb = create_coinbase(template["height"], script_pubkey=CScript([OP_TRUE]), nValue=0)
        block = create_block(coinbase=cb, tmpl=template, txlist=txs)
        add_witness_commitment(block)
        remainder = target - len(block.serialize())
        block.vtx[-1] = self.padded_spend(prev_tx, 100_000 + remainder)
        block.vtx[0].vout.pop()  # Recompute commitment after changing the last tx.
        add_witness_commitment(block)
        assert_equal(len(block.serialize()), target)
        block.nNonce = node.solvemercaturablock(block.serialize().hex())["nonce"]
        return block

    def run_test(self):
        node, peer = self.nodes
        mined = []
        for _ in range(10):
            mined.extend(self.generatetodescriptor(node, 10, "raw(51)"))
        mined.extend(self.generatetodescriptor(node, 1, "raw(51)"))
        self.sync_blocks()
        funding = from_hex(CBlock(), node.getblock(mined[0], 0)).vtx[0]
        accepted_sizes = [1_100_000, 4_000_001, INITIAL_CAPACITY - 1, INITIAL_CAPACITY]
        accepted_hashes = []
        for size in accepted_sizes:
            block = self.exact_block(funding, size)
            raw = block.serialize().hex()
            assert_equal(node.getblocktemplate({"mode": "proposal", "data": raw, "rules": ["segwit"]}), None)
            if size > 4_000_000:
                with peer.assert_debug_log(["completed inbound Mercatura chunk transfer"]):
                    assert_equal(node.submitblock(raw), None)
                    self.sync_blocks(timeout=180)
            else:
                assert_equal(node.submitblock(raw), None)
                self.sync_blocks()
            assert_equal(peer.getbestblockhash(), block.hash_hex)
            assert_equal(peer.getblock(block.hash_hex)["size"], size)
            assert_equal(len(bytes.fromhex(peer.getblock(block.hash_hex, 0))), size)
            self.log.info("Accepted and relayed exact serialized block size: %d", size)
            accepted_hashes.append(block.hash_hex)
            funding = block.vtx[-1]

        over = self.exact_block(funding, INITIAL_CAPACITY + 1)
        tip = node.getbestblockhash()
        # All other construction/commitment/PoW requirements match accepted blocks.
        assert_equal(node.submitblock(over.serialize().hex()), "bad-blk-capacity")
        assert_equal(node.getbestblockhash(), tip)
        self.log.info("Rejected exact serialized block size: %d (bad-blk-capacity)", len(over.serialize()))
        # A same-parent control removes one padding byte, proving that the
        # rejected construction did not also hide an invalid funding/spend path.
        control = self.exact_block(funding, INITIAL_CAPACITY)
        assert_equal(control.hashPrevBlock, over.hashPrevBlock)
        assert_equal(node.submitblock(control.serialize().hex()), None)
        self.sync_blocks(timeout=180)
        accepted_hashes.append(control.hash_hex)
        funding = control.vtx[-1]
        # A 65,536-transaction block fits the ordinary message envelope but
        # exceeds BIP152's 16-bit count. It must fall back to a full BLOCK, and
        # its authenticated proofs must not hit the inherited 16,666-tx bound.
        tiny = []
        for _ in range(65_535):
            funding = create_tx_with_script(funding, 0, amount=funding.vout[0].nValue, output_script=CScript([OP_TRUE]))
            tiny.append(funding)
        template = node.getblocktemplate({"rules": ["segwit"]})
        cb = create_coinbase(template["height"], script_pubkey=CScript([OP_TRUE]), nValue=0)
        many = create_block(coinbase=cb, tmpl=template, txlist=tiny)
        add_witness_commitment(many)
        many.nNonce = node.solvemercaturablock(many.serialize().hex())["nonce"]
        assert len(many.serialize()) < 4_000_000
        with peer.assert_debug_log(["received: block"]):
            assert_equal(node.submitblock(many.serialize().hex()), None)
            self.sync_blocks(timeout=180)
        proof = node.gettxoutproof([tiny[-1].txid_hex], many.hash_hex)
        assert_equal(node.verifytxoutproof(proof), [tiny[-1].txid_hex])
        assert_equal(peer.verifytxoutproof(proof), [tiny[-1].txid_hex])
        tip = many.hash_hex
        self.log.info("Verified RPC proof from a block containing %d transactions", len(many.vtx))
        stats = node.gettxoutsetinfo()["hash_serialized_3"]
        self.restart_node(0)
        assert_equal(node.getbestblockhash(), tip)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], stats)
        self.stop_node(0)
        node.start(extra_args=self.extra_args[0] + ["-reindex"])
        node.wait_for_rpc_connection(wait_for_import=False)
        self.wait_until(lambda: node.getbestblockhash() == tip and node.getmempoolinfo()["loaded"], timeout=600)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], stats)
        for block_hash in accepted_hashes:
            assert_equal(node.getblock(block_hash, 0), peer.getblock(block_hash, 0))


if __name__ == "__main__":
    MercaturaBlockCapacityTest(__file__).main()
