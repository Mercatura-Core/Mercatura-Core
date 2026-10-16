#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license.

"""Progressive real ML-DSA blocks, template assembly, chunk relay and recovery.

No synthetic padding: each non-coinbase input has the native two-item PQ
witness and each output is a wallet-owned PQ script. Timings are observations,
not pass/fail thresholds or production stale-block-rate estimates.
"""

from decimal import Decimal
import json
import re
import time
from pathlib import Path

from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.messages import COIN, COutPoint, CTransaction, CTxIn, CTxOut, from_hex
from test_framework.script import CScript
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

CAPACITY = 4_194_304


class MercaturaPQCapacityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.rpc_timeout = 240
        # Block-only receiver verifies without warming its mempool/script caches.
        self.extra_args = [["-debug=bench", "-debug=net", "-maxsigcachesize=0", "-assumevalid=0"] for _ in range(2)]
        self.extra_args[1].append("-blocksonly")

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    @staticmethod
    def peak_rss_kib(node):
        # Linux daemon high-water mark; unavailable platforms report null.
        try:
            status = Path(f"/proc/{node.process.pid}/status").read_text(encoding="utf8")
        except OSError:
            return None
        match = re.search(r"^VmHWM:\s+(\d+) kB", status, re.MULTILINE)
        return int(match[1]) if match else None

    @staticmethod
    def sign(wallet, tx):
        signed = wallet.signrawtransactionwithwallet(tx.serialize().hex())
        assert_equal(signed["complete"], True)
        result = from_hex(CTransaction(), signed["hex"])
        for witness in result.wit.vtxinwit:
            assert_equal([len(item) for item in witness.scriptWitness.stack], [3309, 1952])
        assert result.get_weight() <= 400_000  # Preserve inherited relay policy.
        return result

    def run_test(self):
        node, peer = self.nodes
        node.createwallet("sender", load_on_startup=True)
        peer.createwallet("receiver", load_on_startup=True)
        sender = node.get_wallet_rpc("sender")
        receiver = peer.get_wallet_rpc("receiver")
        source_address = sender.getnewaddress()
        destination = receiver.getnewaddress()
        assert source_address.startswith("mcrt1z") and destination.startswith("mcrt1z")
        source_script = CScript(bytes.fromhex(sender.getaddressinfo(source_address)["scriptPubKey"]))
        destination_script = CScript(bytes.fromhex(receiver.getaddressinfo(destination)["scriptPubKey"]))
        for _ in range(10):
            self.generatetoaddress(node, 10, source_address)
        self.generatetoaddress(node, 2, source_address)
        self.sync_blocks()
        coins = sender.listunspent(101)
        counts = [384, 784]
        assert len(coins) >= len(counts)
        funding = []
        for coin, count in zip(coins, counts):
            tx = CTransaction()
            tx.vin = [CTxIn(COutPoint(int(coin["txid"], 16), coin["vout"]))]
            # Fees are Mercatura base units (100 per coin), not satoshis.
            # Preserve default minimum/maximum fee policy without RPC overrides.
            value, remainder = divmod(int(coin["amount"] * COIN) - 100, count)
            tx.vout = [CTxOut(value, source_script) for _ in range(count)]
            tx.vout[-1].nValue += remainder
            signed = self.sign(sender, tx)
            assert_equal(node.sendrawtransaction(signed.serialize().hex()), signed.txid_hex)
            funding.append(signed)
        self.generatetoaddress(node, 1, source_address)
        self.sync_blocks()
        results = []
        received_sats = 0
        for fund, count in zip(funding, counts):
            signing_start = time.perf_counter()
            spends = []
            for start in range(0, count, 16):
                tx = CTransaction()
                tx.vin = [CTxIn(COutPoint(fund.txid_int, index)) for index in range(start, start + 16)]
                amount = sum(output.nValue for output in fund.vout[start:start + 16]) - 200
                tx.vout = [CTxOut(amount, destination_script)]
                signed = self.sign(sender, tx)
                spends.append(signed)
                assert_equal(node.sendrawtransaction(signed.serialize().hex()), signed.txid_hex)
                received_sats += amount
            signing_and_admission = time.perf_counter() - signing_start
            assert_equal(peer.getrawmempool(), [])
            construction_start = time.perf_counter()
            template = node.getblocktemplate({"rules": ["segwit"]})
            assert_equal(template["sizelimit"], CAPACITY)
            assert_equal(template["weightlimit"], 4 * CAPACITY)
            assert_equal(template["sigoplimit"], 80_000)
            assert_equal({tx["txid"] for tx in template["transactions"]}, {tx.txid_hex for tx in spends})
            cb = create_coinbase(template["height"], script_pubkey=destination_script, nValue=0)
            block = create_block(coinbase=cb, tmpl=template, txlist=[tx["data"] for tx in template["transactions"]])
            add_witness_commitment(block)
            raw = block.serialize().hex()
            construction_seconds = time.perf_counter() - construction_start
            size = len(raw) // 2
            assert size < CAPACITY
            if count == counts[-1]:
                assert size > CAPACITY * 99 // 100
                assert block.get_weight() > 4_000_000
            block.nNonce = node.solvemercaturablock(raw)["nonce"]
            raw = block.serialize().hex()
            log_path = peer.chain_path / "debug.log"
            log_offset = log_path.stat().st_size
            start = time.perf_counter()
            assert_equal(node.submitblock(raw), None)
            submission_seconds = time.perf_counter() - start
            self.sync_blocks(timeout=240)
            propagation_seconds = time.perf_counter() - start
            assert_equal(peer.getbestblockhash(), block.hash_hex)
            assert_equal(peer.getblock(block.hash_hex)["size"], size)
            peer.syncwithvalidationinterfacequeue()
            # Exercise the wallet proof-import caller with native PQ ownership;
            # importing an already known receipt must not duplicate its credit.
            proof = peer.gettxoutproof([spends[-1].txid_hex], block.hash_hex)
            receiver.importprunedfunds(spends[-1].serialize().hex(), proof)
            assert_equal(receiver.gettransaction(spends[-1].txid_hex)["confirmations"], 1)
            assert_equal(receiver.getbalance(), Decimal(received_sats) / COIN)
            assert_equal(len(receiver.listunspent()), sum(item["pq_spend_outputs"] for item in results) + len(spends))
            with log_path.open(encoding="utf8") as log:
                log.seek(log_offset)
                text = log.read()
            if size > 4_000_000:
                assert "completed inbound Mercatura chunk transfer" in text
            verification = re.findall(r"- Verify \d+ txins: ([\d.]+)ms", text)
            connection = re.findall(r"- Connect block: ([\d.]+)ms", text)
            assert verification and connection
            result = {
                "pq_inputs": count, "pq_spend_outputs": len(spends), "pq_coinbase_outputs": 1,
                "transactions_including_coinbase": len(block.vtx), "serialized_bytes": size,
                "bip141_weight": block.get_weight(),
                "signing_and_mempool_admission_seconds": signing_and_admission,
                "template_and_block_construction_seconds": construction_seconds,
                "submitblock_seconds": submission_seconds,
                "submit_and_peer_tip_seconds": propagation_seconds,
                # Existing Verify metric includes input checks as well as PQ scripts.
                "receiver_input_and_pq_verification_ms": float(verification[-1]),
                "receiver_connect_block_ms": float(connection[-1]),
                "sender_peak_rss_kib": self.peak_rss_kib(node),
                "receiver_peak_rss_kib": self.peak_rss_kib(peer),
            }
            results.append(result)
            self.log.info("REAL_PQ_CAPACITY %s", json.dumps(result, sort_keys=True))
        assert_equal(len(results), len(counts))
        tip = node.getbestblockhash()
        stats = node.gettxoutsetinfo()["hash_serialized_3"]
        self.restart_node(1)
        receiver = peer.get_wallet_rpc("receiver")
        assert_equal(receiver.getbalance(), Decimal(received_sats) / COIN)
        assert_equal(peer.getbestblockhash(), tip)
        assert_equal(peer.gettxoutsetinfo()["hash_serialized_3"], stats)
        self.stop_node(0)
        reindex_start = time.perf_counter()
        node.start(extra_args=self.extra_args[0] + ["-reindex"])
        node.wait_for_rpc_connection(wait_for_import=False)
        self.wait_until(lambda: node.getbestblockhash() == tip and node.getmempoolinfo()["loaded"], timeout=600)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], stats)
        self.log.info("REAL_PQ_REINDEX seconds=%.3f", time.perf_counter() - reindex_start)
        (Path(self.options.tmpdir) / "pq_capacity_results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    MercaturaPQCapacityTest(__file__).main()
