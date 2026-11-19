#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Bound real memory-hard work on rejected headers without disabling later sync."""

from contextlib import ExitStack

from test_framework.messages import CBlockHeader, from_hex, msg_headers
from test_framework.p2p import P2PInterface
from test_framework.pq_lab_proxy import run_owned_test
from test_framework.pq_test_peer import PQP2PBridge
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class LowWorkHeadersTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument("--mercahash-v2-height", type=int, default=-1)
        parser.add_argument("--pq", action="store_true")

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.uses_wallet = False
        self.noban_tx_relay = False
        common = [f"-mercahashv2height={self.options.mercahash_v2_height}", "-debug=net"]
        if self.options.pq:
            common += ["-pqp2p=required"]
        self.extra_args = [common + ["-minimumchainwork=0"], common + ["-minimumchainwork=0x10"]]

    def setup_network(self):
        self.setup_nodes()  # Only owned loopback P2P test connections.

    def connect_peer(self):
        node = self.nodes[1]
        before = {info["id"] for info in node.getpeerinfo()}
        if self.options.pq:
            bridge = self.bridges.enter_context(PQP2PBridge(
                self, node, "regtest", mercahash_v2_height=self.options.mercahash_v2_height))
            peer = P2PInterface()
            connect = peer.peer_connect(dstaddr="127.0.0.1", dstport=bridge.port, net="regtest",
                                        timeout_factor=self.options.timeout_factor, supports_v2_p2p=False,
                                        send_version=True)
            peer.on_connection_send_msg.nVersion = node.getnetworkinfo()["protocolversion"]
            connect()
            node.p2ps.append(peer)
            peer.wait_until(lambda: peer.is_connected, check_connected=False)
            peer.wait_for_verack()
            peer.sync_with_ping()
        else:
            peer = node.add_p2p_connection(P2PInterface())
        peer_id, = {info["id"] for info in node.getpeerinfo()} - before
        if self.options.pq:
            info, = [info for info in node.getpeerinfo() if info["id"] == peer_id]
            assert info["pq_encrypted"] and info["pq_authenticated"]
            assert "noban" not in info["permissions"]
        return peer, peer_id

    def announce(self, connection, headers, checks):
        peer, peer_id = connection
        with self.nodes[1].assert_debug_log([
            f"headers proof-of-work: checked={checks} total={len(headers)} peer={peer_id}",
        ]):
            peer.send_and_ping(msg_headers(headers))

    def run_test(self):
        with ExitStack() as self.bridges:
            self.check_headers()

    def check_headers(self):
        solver, node = self.nodes
        hashes = self.generatetodescriptor(solver, 7, "raw(51)", maxtries=64, sync_fun=self.no_op)
        headers = [from_hex(CBlockHeader(), solver.getblockheader(block_hash, False)) for block_hash in hashes]
        attacker = self.connect_peer()
        honest = self.connect_peer()

        self.log.info("Unknown-parent replays never reach MercaHash, including with V1 only")
        for _ in range(3):
            with node.assert_debug_log(["received: headers"], unexpected_msgs=["headers proof-of-work:"]):
                attacker[0].send_and_ping(msg_headers([headers[1]]))
            honest[0].sync_with_ping()
        assert_raises_rpc_error(-5, "Block not found", node.getblockheader, hashes[1])

        self.log.info("Low-work headers remain unindexed; replays and rebatching reuse only verified PoW")
        self.announce(attacker, headers[:2], 2)
        assert_raises_rpc_error(-5, "Block not found", node.getblockheader, hashes[1])
        for _ in range(3):
            self.announce(attacker, headers[:2], 0)
            honest[0].sync_with_ping()
        self.announce(honest, headers[:1], 0)
        self.announce(honest, headers[:2], 0)
        attacker[0].send_and_ping(msg_headers([]))
        self.announce(attacker, headers[:2], 0)

        self.log.info("The same headers become admissible once their chain meets the work threshold")
        self.announce(honest, headers, 5)
        assert_equal(node.getblockheader(hashes[-1])["height"], 7)
        # Headers alone cannot activate blocks. Supply the bodies through the
        # ordinary validation path, then verify synchronization and chainstate.
        for block_hash in hashes:
            assert_equal(node.submitblock(solver.getblock(block_hash, 0)), None)
        assert_equal(node.getbestblockhash(), hashes[-1])
        assert node.verifychain(4, 0)
        self.announce(attacker, headers, 0)


if __name__ == "__main__":
    run_owned_test(LowWorkHeadersTest(__file__))
