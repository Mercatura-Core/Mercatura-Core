#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Isolated height activation, wrong-version PoW, P2P, reorg and disk history."""
from copy import deepcopy

from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CBlockHeader, from_hex, msg_headers
from test_framework.p2p import P2PInterface
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class MercaHashV2Test(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.uses_wallet = False
        self.noban_tx_relay = False
        self.activation_args = ["-mercahashv2height=3", "-debug=net", "-debug=validation"]
        self.extra_args = [self.activation_args, []]

    def setup_network(self):
        # Different isolated regtest consensus schedules; no inter-node peering.
        self.setup_nodes()

    def block(self, node, parent, height, tag):
        coinbase = create_coinbase(height, script_pubkey=CScript([OP_TRUE, tag]), nValue=0)
        block = create_block(parent.hash_int, coinbase, parent.nTime + 1)
        block.nBits = parent.nBits
        block.nNonce = node.solvemercaturablock(block.serialize().hex())["nonce"]
        return block

    def run_test(self):
        node, v1_node = self.nodes
        # Generate at H-1, H and H+1 through the normal RPC mining path.
        hashes = self.generatetodescriptor(node, 4, "raw(51)", sync_fun=self.no_op)
        for height, block_hash in enumerate(hashes, 1):
            header = node.getblockheader(block_hash)
            assert_equal(header["height"], height)
            assert_equal(header["bits"], "207fffff")
            assert_equal(int(header["chainwork"], 16), (height + 1) * 2)
            if height < 3:
                assert_equal(v1_node.submitblock(node.getblock(block_hash, 0)), None)
        parent = from_hex(CBlockHeader(), node.getblockheader(hashes[1], False))
        # Solve using the independently configured V1 node, then require V2 to
        # reject at least one V1-only proof at H. A dual-valid nonce is possible.
        for tag in range(1, 65):
            wrong = self.block(v1_node, parent, 3, tag)
            try:
                node.submitheader(CBlockHeader(wrong).serialize().hex())
            except JSONRPCException as exc:
                assert "high-hash" in exc.error["message"]
                break
        else:
            raise AssertionError("No V1-only proof found in 64 deterministic candidates")
        assert_equal(node.getbestblockhash(), hashes[-1])

        self.log.info("Unconnected headers request ancestry without admission or PoW caching")
        peer = node.add_p2p_connection(P2PInterface())
        unknown = deepcopy(wrong)
        unknown.hashPrevBlock = 12345
        # A getheaders request may already be outstanding; rate limiting can
        # suppress another request. Unknown ancestry must never reach the PoW
        # accounting log or admission, regardless of that scheduling detail.
        with node.assert_debug_log(["received: headers"], unexpected_msgs=["headers proof-of-work:"]):
            peer.send_and_ping(msg_headers([CBlockHeader(unknown)]))
        assert_raises_rpc_error(-5, "Block not found", node.getblockheader, unknown.hash_hex)
        assert_equal(node.getbestblockhash(), hashes[-1])
        replay_peer = node.add_p2p_connection(P2PInterface())
        self.log.info("Malformed batches are rejected before memory-hard work")
        with node.assert_debug_log(["non-continuous headers sequence"]):
            peer.send_without_ping(msg_headers([CBlockHeader(unknown), CBlockHeader(unknown)]))
            peer.wait_for_disconnect()

        self.log.info("Mine a side branch from H-2 while the active tip is above H")
        fork_parent = from_hex(CBlockHeader(), node.getblockheader(hashes[0], False))
        fork = []
        for height in range(2, 6):
            block = self.block(node, fork_parent, height, 100 + height)
            node.submitheader(CBlockHeader(block).serialize().hex())
            fork.append(block)
            fork_parent = block
        with node.assert_debug_log(["headers proof-of-work: checked=0 total=4"]):
            replay_peer.send_and_ping(msg_headers([CBlockHeader(block) for block in fork]))
        for block in fork:
            result = node.submitblock(block.serialize().hex())
            assert result in (None, "inconclusive")
        fork_tip = fork[-1].hash_hex
        assert_equal(node.getbestblockhash(), fork_tip)
        assert_equal(node.getblockcount(), 5)
        assert_equal(node.getblockheader(hashes[-1])["confirmations"], -1)

        # Disconnect all post-activation blocks, then replay them with their own
        # historical height rather than the temporary active tip's algorithm.
        node.invalidateblock(fork[0].hash_hex)
        node.invalidateblock(hashes[1])
        assert_equal(node.getblockcount(), 1)
        node.reconsiderblock(fork[0].hash_hex)
        assert_equal(node.getbestblockhash(), fork_tip)
        node.reconsiderblock(hashes[1])
        assert_equal(node.getbestblockhash(), fork_tip)
        assert node.verifychain(4, 0)
        utxos = node.gettxoutsetinfo()
        chainwork = node.getblockheader(fork_tip)["chainwork"]
        for extra in ([], ["-reindex"]):
            self.restart_node(0, extra_args=self.activation_args + extra)
            assert_equal(node.getbestblockhash(), fork_tip)
            assert_equal(node.getblockheader(fork_tip)["chainwork"], chainwork)
            assert_equal(node.gettxoutsetinfo()["total_amount"], utxos["total_amount"])
            assert_equal(node.gettxoutsetinfo()["txouts"], utxos["txouts"])
            assert node.verifychain(4, 0)
        replay_peer = node.add_p2p_connection(P2PInterface())
        with node.assert_debug_log(["headers proof-of-work: checked=0 total=4"]):
            replay_peer.send_and_ping(msg_headers([CBlockHeader(block) for block in fork]))


if __name__ == "__main__":
    MercaHashV2Test(__file__).main()
