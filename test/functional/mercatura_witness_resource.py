#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license.

"""Mainnet witness ingress: malformed alternate bodies must not poison headers.

Uses two isolated mainnet nodes, a public-testnet inspection node, the frozen
genesis and native MercaHash mining.
No regtest activation override, external peers, future-height history or PoW stub.
"""

from copy import deepcopy
from contextlib import ExitStack
import struct

from test_framework.messages import (
    CBlock, CBlockHeader, CTxInWitness, HeaderAndShortIDs,
    from_hex, msg_block, msg_blocktxn, msg_cmpctblock, msg_headers, ser_uint256,
)
from test_framework.p2p import P2PInterface, p2p_lock
from test_framework.pq_test_peer import PQP2PBridge
from test_framework.pq_lab_proxy import run_owned_test
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, initialize_datadir


class BlockMeta:
    msgtype = b"blkmeta"

    def __init__(self, block_hash):
        self.block_hash = block_hash

    def serialize(self):
        # Structurally valid five-chunk announcement above the launch capacity
        # and monolithic-message threshold. No large payload allocation.
        return ser_uint256(self.block_hash) + struct.pack("<QII", 4_194_305, 1_048_576, 5)


class MercaturaWitnessResourceTest(BitcoinTestFramework):
    def set_test_params(self):
        # An empty chain subdirectory is mainnet's actual on-disk layout.
        # write_config supports this explicitly; -chain=main selects consensus.
        self.chain = ""
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.wallet_names = [False, False, False]
        self.extra_args = [["-chain=main", "-listen=1"] for _ in range(2)] + [[]]
        self.extra_init = [{}, {}, {"chain": "testnet"}]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_chain(self):
        super().setup_chain()
        initialize_datadir(self.options.tmpdir, 2, "testnet", self.disable_autoconnect)

    def setup_network(self):
        self.setup_nodes()  # Deliberately do not connect the real nodes.

    def connect_peer(self, node):
        bridge = self.bridges.enter_context(PQP2PBridge(self, node, 'main'))
        peer = P2PInterface()
        connect = peer.peer_connect(dstaddr="127.0.0.1", dstport=bridge.port,
                                    net="mainnet", timeout_factor=self.options.timeout_factor,
                                    supports_v2_p2p=False, send_version=True)
        # The inherited Python default predates Mercatura's chunk protocol.
        peer.on_connection_send_msg.nVersion = node.getnetworkinfo()["protocolversion"]
        connect()
        node.p2ps.append(peer)
        peer.wait_until(lambda: peer.is_connected, check_connected=False)
        peer.wait_for_verack()
        peer.sync_with_ping()
        peers = node.getpeerinfo()
        assert_equal(len(peers), 1)
        assert peers[0]['pq_encrypted'] and peers[0]['pq_authenticated']
        assert_equal(peers[0]['transport_protocol_type'], 'pq-v1')
        return peer

    @staticmethod
    def malformed(block):
        alternate = deepcopy(block)
        # Preserve stripped transactions, merkle root, PoW and block identifier.
        alternate.vtx[0].wit.vtxinwit = [CTxInWitness()]
        alternate.vtx[0].wit.vtxinwit[0].scriptWitness.stack = [b""] * 1004
        assert_equal(CBlockHeader(alternate).serialize(), CBlockHeader(block).serialize())
        return alternate

    @staticmethod
    def compact(block, prefill):
        compact = HeaderAndShortIDs()
        compact.initialize_from_block(block, prefill_list=prefill, use_witness=True)
        return msg_cmpctblock(compact.to_p2p())

    def run_test(self):
        with ExitStack() as self.bridges:
            self.run_scenarios()

    def run_scenarios(self):
        miner, receiver, testnet = self.nodes
        for node in (miner, receiver):
            assert_equal(node.getblockchaininfo()["chain"], "main")
            assert_equal(node.getbestblockhash(), "cd797c78731d68a82b664b3e359a2e69508ea873fe5747b686488589cc7d6f15")
        miner.createwallet("pq")
        address = miner.get_wallet_rpc("pq").getnewaddress()
        assert address.startswith("mca1z")
        peer = self.connect_peer(receiver)

        def mine():
            block_hash = self.generatetoaddress(miner, 1, address, sync_fun=self.no_op)[0]
            return from_hex(CBlock(), miner.getblock(block_hash, 0))

        first = mine()
        bad = self.malformed(first)
        self.log.info("Confirm public-testnet inspection retains legacy witness decoding")
        assert_equal(testnet.getblockchaininfo()["chain"], "test")
        raw_tx = bad.vtx[0].serialize_with_witness().hex()
        assert_raises_rpc_error(-22, "TX decode failed", miner.decoderawtransaction, raw_tx)
        assert_equal(len(testnet.decoderawtransaction(raw_tx)["vin"][0]["txinwitness"]), 1004)
        receiver.submitheader(CBlockHeader(first).serialize().hex())
        assert_raises_rpc_error(-22, "Block decode failed", receiver.submitblock, bad.serialize().hex())
        with receiver.assert_debug_log(["bad-witness-stack-count"]):
            peer.send_and_ping(msg_block(bad))
        assert_equal(receiver.getblockcount(), 0)
        assert_equal(receiver.getchaintips()[0]["status"], "headers-only")
        peer.send_and_ping(msg_block(first))
        self.wait_until(lambda: receiver.getbestblockhash() == first.hash_hex)

        self.log.info("Exercise compact prefills and retry a malformed missing-transaction response")
        second = mine()
        bad = self.malformed(second)
        with receiver.assert_debug_log(["bad-witness-stack-count"]):
            peer.send_and_ping(self.compact(bad, [0]))
        peer.send_and_ping(self.compact(second, []))
        peer.wait_until(lambda: "getblocktxn" in peer.last_message)
        with p2p_lock:
            request = peer.last_message["getblocktxn"].block_txn_request
            assert_equal(request.blockhash, second.hash_int)
            assert_equal(request.to_absolute(), [0])
        response = msg_blocktxn()
        response.block_transactions.blockhash = second.hash_int
        response.block_transactions.transactions = bad.vtx
        with receiver.assert_debug_log(["bad-witness-stack-count"]):
            peer.send_and_ping(response)
        assert_equal(receiver.getblockcount(), 1)
        response.block_transactions.transactions = second.vtx
        peer.send_and_ping(response)
        self.wait_until(lambda: receiver.getbestblockhash() == second.hash_hex)

        self.log.info("Reject a requested chunk announcement above launch capacity")
        third = mine()
        peer.send_and_ping(msg_headers([CBlockHeader(third)]))
        peer.wait_until(lambda: "getdata" in peer.last_message and
                        any(inv.hash == third.hash_int for inv in peer.last_message["getdata"].inv))
        with receiver.assert_debug_log(["above Mercatura capacity"]):
            peer.send_without_ping(BlockMeta(third.hash_int))
            peer.wait_for_disconnect()
        assert_equal(receiver.getblockcount(), 2)
        receiver.disconnect_p2ps()
        peer = self.connect_peer(receiver)
        peer.send_and_ping(msg_block(third))
        self.wait_until(lambda: receiver.getbestblockhash() == third.hash_hex)

        receiver.disconnect_p2ps()
        for option in ("-reindex-chainstate", "-reindex"):
            self.restart_node(1, extra_args=[option])
            self.wait_until(lambda: receiver.getbestblockhash() == third.hash_hex)
            assert_equal(receiver.verifychain(4, 0), True)
        self.log.info("Mainnet P2P/compact recovery and both reconstruction modes passed")


if __name__ == "__main__":
    run_owned_test(MercaturaWitnessResourceTest(__file__))
