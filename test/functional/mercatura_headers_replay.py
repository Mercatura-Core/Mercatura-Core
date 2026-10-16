#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

"""Count real MercaHash checks for new, replayed, forked and invalid P2P headers."""

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CBlockHeader, from_hex, msg_headers
from test_framework.p2p import P2PInterface
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class HeadersReplayTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = False
        self.noban_tx_relay = False
        self.extra_args = [["-debug=net", "-debug=validation"]]

    def connect_peer(self):
        node = self.nodes[0]
        before = {peer["id"] for peer in node.getpeerinfo()}
        peer = node.add_p2p_connection(P2PInterface())
        peer_id, = {info["id"] for info in node.getpeerinfo()} - before
        return peer, peer_id

    def block(self, parent, height, time_step=1):
        coinbase = create_coinbase(height, script_pubkey=CScript([OP_TRUE]), nValue=0)
        block = create_block(parent.hash_int, coinbase, parent.nTime + time_step)
        block.nBits = parent.nBits
        block.nNonce = self.nodes[0].solvemercaturablock(block.serialize().hex())["nonce"]
        return block

    def announce(self, connection, blocks, checks):
        peer, peer_id = connection
        message = msg_headers([CBlockHeader(block) for block in blocks])
        expected = f"headers proof-of-work: checked={checks} total={len(blocks)} peer={peer_id}"
        with self.nodes[0].assert_debug_log([expected]):
            peer.send_and_ping(message)

    def run_test(self):
        node = self.nodes[0]
        genesis = from_hex(CBlockHeader(), node.getblockheader(node.getbestblockhash(), False))
        first_peer = self.connect_peer()
        second_peer = self.connect_peer()
        first = self.block(genesis, 1)

        self.log.info("New headers require one real MercaHash check; cross-peer replays require none")
        self.announce(first_peer, [first], 1)
        assert_equal(node.getblockheader(first.hash_hex)["height"], 1)
        assert_equal(node.getblockcount(), 0)
        self.announce(first_peer, [first], 0)
        self.announce(second_peer, [first], 0)

        second = self.block(first, 2)
        self.announce(second_peer, [first, second], 1)
        self.announce(first_peer, [first, second], 0)
        assert_equal(node.getblockheader(second.hash_hex)["height"], 2)
        for block in (first, second):
            assert_equal(node.submitblock(block.serialize().hex()), None)
        assert_equal(node.getbestblockhash(), second.hash_hex)

        self.log.info("Invalid altered headers must be checked and rejected without poisoning valid headers")
        bad_peer, bad_peer_id = self.connect_peer()
        invalid = CBlockHeader(second)
        invalid.nBits = 0
        with node.assert_debug_log([
            f"headers proof-of-work: checked=1 total=2 peer={bad_peer_id}",
            "header with invalid proof of work",
        ]):
            bad_peer.send_without_ping(msg_headers([CBlockHeader(first), invalid]))
            bad_peer.wait_for_disconnect()
        assert_raises_rpc_error(-5, "Block not found", node.getblockheader, invalid.hash_hex)
        assert_equal(node.getbestblockhash(), second.hash_hex)
        self.announce(second_peer, [second], 0)

        self.log.info("A longer competing branch is verified, indexed, and activates normally")
        fork = self.block(genesis, 1, time_step=2)
        fork_next = self.block(fork, 2)
        fork_tip = self.block(fork_next, 3)
        self.announce(first_peer, [fork, fork_next, fork_tip], 3)
        assert_equal(node.getbestblockhash(), second.hash_hex)
        self.announce(second_peer, [fork, fork_next, fork_tip], 0)
        # Bodies on a non-winning branch are stored but not connected yet, so
        # submitblock reports "inconclusive" until the longer tip activates it.
        for block in (fork, fork_next):
            assert_equal(node.submitblock(block.serialize().hex()), "inconclusive")
            assert_equal(node.getblock(block.hash_hex, 0), block.serialize().hex())
            assert_equal(node.getbestblockhash(), second.hash_hex)
        assert_equal(node.submitblock(fork_tip.serialize().hex()), None)
        assert_equal(node.getbestblockhash(), fork_tip.hash_hex)
        assert_equal(node.getblockcount(), 3)
        assert_equal(node.getblockheader(second.hash_hex)["confirmations"], -1)
        self.announce(first_peer, [first, second], 0)
        self.announce(second_peer, [fork_tip], 0)

        self.log.info("Restart preserves replay protection for both the active and stale branches")
        self.restart_node(0)
        first_peer = self.connect_peer()
        second_peer = self.connect_peer()
        self.announce(first_peer, [first, second], 0)
        self.announce(second_peer, [fork, fork_next, fork_tip], 0)
        assert_equal(node.getbestblockhash(), fork_tip.hash_hex)
        assert_equal(node.getblockcount(), 3)


if __name__ == "__main__":
    HeadersReplayTest(__file__).main()
