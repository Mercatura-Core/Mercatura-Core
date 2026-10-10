#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Live M3: real Core, native client hashes, direct PQ payouts and restart.

Ordinary-share seeding deliberately skips network hits so all three identities
can enter the first shared snapshot without a warm-up block changing the parent.
The final network solution is actually submitted by the standalone coordinator.
"""
from copy import deepcopy
from decimal import Decimal
import json
from pathlib import Path
import socket
import sqlite3
import subprocess
import time
from urllib.parse import urlsplit

from test_framework.messages import CBlock, from_hex
from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal, get_auth_cookie


class PoolClient:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=120)
        self.file = self.socket.makefile("rwb")
        self.identifier = 0

    def request(self, method, params=None):
        self.identifier += 1
        message = {"id": self.identifier, "method": method, "params": params or {}}
        self.file.write(json.dumps(message).encode() + b"\n")
        self.file.flush()
        while True:
            raw = self.file.readline()
            assert raw
            reply = json.loads(raw)
            if reply.get("id") == self.identifier:
                return reply

    def close(self):
        self.file.close()
        self.socket.close()


class MercaturaPoolCoordinatorTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.wallet_names = []
        self.rpc_timeout = 120

    def add_options(self, parser):
        parser.add_argument("--pool-binary")
        parser.add_argument("--pool-worker")

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        binaries = Path(self.config["environment"]["BUILDDIR"]) / "bin"
        self.pool_binary = self.options.pool_binary or str(binaries / "mercatura-pool")
        self.pool_worker = self.options.pool_worker or str(binaries / "pool-hash-test-client")
        if not Path(self.pool_binary).is_file() or not Path(self.pool_worker).is_file():
            raise SkipTest("optional M3 coordinator and native test worker were not built")

    def run_test(self):
        node = self.nodes[0]
        wallets, addresses = [], []
        for i in range(3):
            node.createwallet(wallet_name=f"pool-recipient-{i}")
            wallet = node.get_wallet_rpc(f"pool-recipient-{i}")
            address = wallet.getnewaddress("M3 direct coinbase")
            assert address.startswith("mcrt1z")
            wallets.append(wallet)
            addresses.append(address)
        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            port = reserve.getsockname()[1]
        root = Path(self.options.tmpdir)
        url = urlsplit(node.url)
        user, password = get_auth_cookie(node.datadir_path, self.chain)
        database = root / "m3-pool.sqlite"
        config = {"network": "regtest", "genesis": node.getblockhash(0),
                  "rpc_url": f"http://127.0.0.1:{url.port}/", "rpc_user": user, "rpc_password": password,
                  "database": str(database), "port": port, "plaintext_regtest": True, "warmup": True,
                  "share_target": "ff" * 32, "refresh_seconds": 1, "lifetime_seconds": 120,
                  "reconcile_seconds": 1}
        configuration = root / "m3-pool.json"
        configuration.write_text(json.dumps(config))
        configuration.chmod(0o600)
        log = (root / "m3-pool.log").open("wb")
        process = None
        clients = []

        def start():
            nonlocal process
            process = subprocess.Popen([self.pool_binary, str(configuration)], stdout=log, stderr=log)

            def listening():
                assert process.poll() is None, (root / "m3-pool.log").read_text()
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                        return True
                except OSError:
                    return False
            self.wait_until(listening)

        def hello(client, address=None, token=None):
            params = {"version": 1, "network": "regtest", "genesis": config["genesis"], "algorithm": "MercaHash-V1"}
            params["resume_token" if token else "payout"] = token or address
            result = client.request("hello", params)
            assert_equal(result["error"], None)
            return result["result"]

        def native_nonce(job, mode):
            result = subprocess.run([self.pool_worker, job["header"], job["share_target"], job["network_target"], mode], check=True, text=True, capture_output=True, timeout=120)
            return json.loads(result.stdout)["nonce"]

        try:
            start()
            credentials, jobs = [], []
            for address in addresses:
                client = PoolClient(port)
                clients.append(client)
                credentials.append(hello(client, address=address))
                response = client.request("getjob")
                assert_equal(response["error"], None)
                jobs.append(response["result"])
            assert_equal(len({c["extranonce_namespace"] for c in credentials}), 3)
            for client, job in zip(clients, jobs):
                nonce = native_nonce(job, "ordinary")
                receipt = client.request("submit", {"job_id": job["job_id"], "nonce": nonce})
                assert_equal(receipt["error"], None)
                assert_equal(receipt["result"]["network_candidate"], False)
            assert_equal(node.getblockcount(), 0)
            time.sleep(1.1)
            shared_reply = clients[0].request("getjob")
            assert_equal(shared_reply["error"], None)
            shared = shared_reply["result"]
            manifest = clients[0].request("manifest", {"job_id": shared["job_id"]})["result"]
            assert_equal(manifest["warmup"], False)
            assert_equal(manifest["cutoff"], 3)
            assert_equal(len(manifest["allocations"]), 3)
            assert_equal(sum(p["amount"] for p in manifest["allocations"]), manifest["reward"])
            with sqlite3.connect(database) as db:
                serialized = db.execute("SELECT block FROM jobs WHERE id=?", (shared["job_id"],)).fetchone()[0]
            block = from_hex(CBlock(), serialized)
            overclaim = deepcopy(block)
            overclaim.vtx[0].vout[0].nValue += 1
            overclaim.hashMerkleRoot = overclaim.calc_merkle_root()
            assert_equal(node.getblocktemplate({"mode": "proposal", "data": overclaim.serialize().hex()}), "bad-cb-amount")
            nonce = native_nonce(shared, "network")
            receipt = clients[0].request("submit", {"job_id": shared["job_id"], "nonce": nonce})
            assert_equal(receipt["error"], None)
            assert_equal(receipt["result"]["network_candidate"], True)
            block.nNonce = nonce
            self.wait_until(lambda: node.getbestblockhash() == block.hash_hex)
            assert_equal(node.getblockheader(block.hash_hex)["confirmations"], 1)
            actual = node.getblock(block.hash_hex, 2)["tx"][0]
            assert_equal(actual["txid"], manifest["coinbase_txid"])
            payouts = {p["script"]: p["amount"] for p in manifest["allocations"] if p["amount"]}
            for output in actual["vout"]:
                if output["value"]:
                    assert_equal(output["value"] * 100, payouts[output["scriptPubKey"]["hex"]])
            for wallet, address in zip(wallets, addresses):
                script = wallet.getaddressinfo(address)["scriptPubKey"]
                transaction = wallet.gettransaction(actual["txid"])
                assert_equal(transaction["confirmations"], 1)
                assert_equal(wallet.getbalances()["mine"]["immature"], Decimal(payouts[script]) / 100)
            self.wait_until(lambda: clients[0].request("status")["result"]["blocks"].get("active", 0) == 1)
            saved = receipt["result"]["receipt_id"]
            for client in clients:
                client.close()
            clients.clear()
            process.terminate()
            assert_equal(process.wait(timeout=30), 0)
            process = None
            start()
            client = PoolClient(port)
            clients.append(client)
            hello(client, token=credentials[0]["resume_token"])
            duplicate = client.request("submit", {"job_id": shared["job_id"], "nonce": nonce})
            assert_equal(duplicate["error"]["code"], 108)
            assert_equal(duplicate["receipt"]["receipt_id"], saved)
            assert_equal(client.request("status")["result"]["accepted_shares"], 4)
            assert_equal(client.request("manifest", {"job_id": shared["job_id"]})["result"]["snapshot_id"], shared["snapshot_id"])
            # Exercise the production coinbase builder against live Core at
            # larger output counts. These use synthetic equal work and native
            # PQ commitment scripts; the three-wallet coordinator case above
            # is the actual PPLNS/payout-ownership integration.
            for count in (100, 1000):
                template = node.getblocktemplate({"rules": ["segwit"]})
                template_file = root / f"template-{count}.json"
                template_file.write_text(json.dumps(template))
                result = subprocess.run([self.pool_worker, "--coinbase", str(template_file), str(count), config["genesis"]], check=True, text=True, capture_output=True, timeout=120)
                built = json.loads(result.stdout)
                large = from_hex(CBlock(), built["block"])
                assert_equal(len(large.vtx[0].vout), count + 1)
                assert_equal(sum(output.nValue for output in large.vtx[0].vout), template["coinbasevalue"])
                assert_equal(len(large.serialize()), built["manifest"]["serialized_bytes"])
                assert_equal(node.getblocktemplate({"mode": "proposal", "data": large.serialize().hex()}), None)
                self.solve_mercatura_block(node, large)
                assert_equal(node.submitblock(large.serialize().hex()), None)
                assert_equal(node.getbestblockhash(), large.hash_hex)
                self.log.info("Live Core accepted %d native PQ recipients in %d serialized block bytes", count, len(large.serialize()))
            for _ in range(10):
                # Native MercaHash maturity batches can outlast the session's
                # idle timeout on constrained hosts; keep v1's heartbeat alive.
                assert_equal(client.request("ping")["error"], None)
                self.generatetodescriptor(node, 10, "raw(51)")
            self.wait_until(lambda: client.request("status")["result"]["blocks"].get("matured", 0) == 1)
            for wallet, address in zip(wallets, addresses):
                script = wallet.getaddressinfo(address)["scriptPubKey"]
                assert_equal(wallet.getbalances()["mine"]["trusted"], Decimal(payouts[script]) / 100)
            node.invalidateblock(block.hash_hex)
            self.wait_until(lambda: client.request("status")["result"]["blocks"].get("orphaned", 0) == 1)
            assert_equal(client.request("status")["result"]["currently_matured_base_units"], "0")
            node.reconsiderblock(block.hash_hex)
            self.wait_until(lambda: client.request("status")["result"]["blocks"].get("matured", 0) == 1)
            self.log.info("Live Core maturity, invalidation/orphaning and reconsideration reconciliation passed")
            self.log.info("M3 live native shares, frozen three-wallet PQ payouts, chain acceptance and restart passed")
        finally:
            for client in clients:
                client.close()
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=30)
            log.close()


if __name__ == "__main__":
    MercaturaPoolCoordinatorTest(__file__).main()
