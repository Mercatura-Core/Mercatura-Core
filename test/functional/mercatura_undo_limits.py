#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.
"""Exact undo budgets, activation, atomic rejection, reorg and disk replay.

Funds less than 9 MiB of actual UTXOs. No huge undo object or chain is generated.
"""

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import COutPoint, CTransaction, CTxIn, CTxOut, ser_compact_size, ser_varint
from test_framework.script import CScript, OP_DROP, OP_NOP, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


UNDO_LIMIT = 8 << 20


def large_script(size):
    script = b""
    chunk = bytes(CScript([bytes(520), OP_DROP]))
    while len(script) + len(chunk) + 1 <= size:
        script += chunk
    remaining = size - len(script)
    if remaining > 5:
        script += bytes(CScript([bytes(remaining - 5), OP_DROP]))
    script += bytes([OP_NOP]) * (size - len(script) - 1) + bytes([OP_TRUE])
    assert_equal(len(script), size)
    return CScript(script)


class UndoLimitsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.uses_wallet = False
        self.rpc_timeout = 240
        self.extra_args = [["-checkblockindex=1"], ["-checkblockindex=1", "-testactivationheight=undo@113"]]

    def setup_network(self):
        self.setup_nodes()  # Exchange only explicit test blocks by RPC.

    def block(self, node, transactions=(), outputs=None):
        template = node.getblocktemplate({"rules": ["segwit"]})
        coinbase = create_coinbase(template["height"], script_pubkey=CScript([OP_TRUE]), nValue=0)
        if outputs is not None:
            coinbase.vout = [CTxOut(0, script) for script in outputs]
        block = create_block(coinbase=coinbase, tmpl=template, txlist=list(transactions))
        self.solve_mercatura_block(node, block)
        return block

    @staticmethod
    def spend(points):
        tx = CTransaction()
        tx.vin = [CTxIn(point) for point in points]
        tx.vout = [CTxOut(0, CScript([OP_TRUE]))]
        return tx

    def run_test(self):
        active, delayed = self.nodes
        # All funding heights are below 64: each undo coin has one height byte,
        # one legacy dummy, one amount byte, and a script-code VARINT.
        def coin_size(script_size, height=1, coinbase=True):
            return len(ser_varint(2 * height + int(coinbase))) + 2 + len(ser_varint(script_size + 6)) + script_size

        # Two spends: a tiny coin, then its intra-block output + 838 large coins
        # and a tail adjusted by exactly one byte. Count all vector prefixes.
        fixed = len(ser_compact_size(2)) + len(ser_compact_size(1)) + len(ser_compact_size(840))
        fixed += coin_size(1) + coin_size(1, height=112, coinbase=False) + coin_size(10_000) * 838
        tail = UNDO_LIMIT - fixed - 5  # Large tail coin has five metadata/prefix bytes.
        scripts = [CScript([OP_TRUE])] + [large_script(10_000)] * 838
        scripts += [large_script(tail + delta) for delta in (-1, 0, 1)]
        funded = []
        for start in range(0, len(scripts), 80):
            block = self.block(active, outputs=scripts[start:start + 80])
            assert len(block.vtx[0].serialize_without_witness()) < 1_000_000
            assert_equal(active.submitblock(block.serialize().hex()), None)
            assert_equal(delayed.submitblock(block.serialize().hex()), None)
            for index in range(len(block.vtx[0].vout)):
                funded.append(COutPoint(block.vtx[0].txid_int, index))
        assert_equal(active.getblockcount(), 11)
        hashes = self.generatetodescriptor(active, 100, "raw(51)", sync_fun=self.no_op)
        for block_hash in hashes:
            assert_equal(delayed.submitblock(active.getblock(block_hash, 0)), None)
        assert_equal(active.getblockcount(), 111)
        first = self.spend([funded[0]])
        transactions = []
        for delta in (-1, 0, 1):
            tx = self.spend([COutPoint(first.txid_int, 0), *funded[1:839], funded[840 + delta]])
            assert_equal(len(tx.vin), 840)
            actual = fixed + coin_size(tail + delta)
            assert_equal(actual, UNDO_LIMIT + delta)
            transactions.append(tx)
        original_stats = active.gettxoutsetinfo()["hash_serialized_3"]
        before = self.block(active, [first, transactions[2]])
        assert_equal(active.submitblock(before.serialize().hex()), "bad-blk-undo-size")
        assert_equal(active.getblockcount(), 111)
        assert_equal(active.gettxoutsetinfo()["hash_serialized_3"], original_stats)
        assert_equal(delayed.submitblock(before.serialize().hex()), None)
        assert_equal(delayed.getblockcount(), 112)  # Prior consensus behavior.
        delayed.invalidateblock(before.hash_hex)

        empty = self.block(active)
        for node in self.nodes:
            assert_equal(node.submitblock(empty.serialize().hex()), None)
        assert_equal(active.getblockcount(), 112)
        blocks = [self.block(active, [first, tx]) for tx in transactions]
        for node in self.nodes:
            for variant, block in enumerate(blocks):
                result = node.getblocktemplate({"mode": "proposal", "data": block.serialize().hex(), "rules": ["segwit"]})
                assert_equal(result, "bad-blk-undo-size" if variant == 2 else None)
            tip = node.getbestblockhash()
            stats = node.gettxoutsetinfo()["hash_serialized_3"]
            assert_equal(node.submitblock(blocks[2].serialize().hex()), "bad-blk-undo-size")
            assert_equal(node.getbestblockhash(), tip)
            assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], stats)
            assert node.gettxout(f"{funded[0].hash:064x}", funded[0].n) is not None
            assert_equal(node.submitblock(blocks[1].serialize().hex()), None)  # Exactly 8 MiB.
            assert_equal(node.verifychain(4, 1), True)

        # Real work-based selection disconnects the exact-limit block and connects
        # the below-limit competing block, restoring and spending different tails.
        delayed.invalidateblock(blocks[1].hash_hex)
        assert_equal(delayed.submitblock(blocks[0].serialize().hex()), None)
        longer = self.block(delayed)
        assert_equal(delayed.submitblock(longer.serialize().hex()), None)
        # Equal-work side branches may be stored without being connected yet.
        assert active.submitblock(blocks[0].serialize().hex()) in (None, "inconclusive")
        assert_equal(active.submitblock(longer.serialize().hex()), None)
        assert_equal(active.getbestblockhash(), longer.hash_hex)
        assert_equal(active.gettxoutsetinfo()["hash_serialized_3"], delayed.gettxoutsetinfo()["hash_serialized_3"])
        # Unconfirmed transactions from invalidation are irrelevant to disk replay.
        tip = active.getbestblockhash()
        stats = active.gettxoutsetinfo()["hash_serialized_3"]
        for args in ([], ["-reindex-chainstate"], ["-reindex"]):
            self.restart_node(0, self.extra_args[0] + args)
            self.wait_until(lambda: active.getbestblockhash() == tip and active.getmempoolinfo()["loaded"], timeout=600)
            assert_equal(active.gettxoutsetinfo()["hash_serialized_3"], stats)
            assert_equal(active.verifychain(4, 2), True)
        self.log.info("Accepted undo payloads 8 MiB - 1 and exactly 8 MiB; rejected +1; replay and reorg passed")


if __name__ == "__main__":
    UndoLimitsTest(__file__).main()
