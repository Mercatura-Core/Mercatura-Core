#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""M4: native M2 workers, verified TLS, M3 shares and real multi-PQ blocks."""
from decimal import Decimal
import json
from pathlib import Path
import socket
import ssl
import subprocess
import threading
import time
from urllib.parse import urlsplit

from test_framework.messages import hash256, ser_string
from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal, get_auth_cookie


class TLSClient:
    def __init__(self, port, certificate):
        self.socket = ssl.create_default_context(cafile=str(certificate)).wrap_socket(
            socket.create_connection(("127.0.0.1", port), timeout=60), server_hostname="127.0.0.1")
        self.file = self.socket.makefile("rwb")
        self.identifier = 0

    def request(self, method, params=None):
        self.identifier += 1
        self.file.write(json.dumps({"id": self.identifier, "method": method, "params": params or {}}).encode() + b"\n")
        self.file.flush()
        while True:
            reply = json.loads(self.file.readline())
            if reply.get("id") == self.identifier:
                return reply

    def close(self):
        self.file.close()
        self.socket.close()


def free_port():
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        return reserve.getsockname()[1]


class FaultPool:
    """Actual TLS with deliberate v1 faults; receipts are synthetic test data."""
    def __init__(self, certificate, key, genesis, job, manifest, scenario):
        self.scenario = scenario
        self.port = free_port()
        self.resume = False
        self.submits = []
        self.jobs = 0
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", self.port))
        self.listener.listen(4)
        self.listener.settimeout(0.2)
        self.stop = threading.Event()
        self.errors = []
        self.active = None
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(certificate), str(key))

        def serve():
            while not self.stop.is_set():
                try:
                    connection, _ = self.listener.accept()
                except socket.timeout:
                    continue
                except OSError:
                    break
                try:
                    with context.wrap_socket(connection, server_side=True) as secured:
                        self.active = secured
                        secured.settimeout(10)
                        file = secured.makefile("rwb")
                        while not self.stop.is_set():
                            try:
                                raw = file.readline()
                            except (TimeoutError, OSError):
                                break
                            if not raw:
                                break
                            request = json.loads(raw)
                            method, params = request["method"], request["params"]
                            identifier = request["id"]
                            result, error, receipt = {}, None, None
                            if method == "hello":
                                self.resume |= "resume_token" in params
                                result = {"version": 1, "algorithm": "MercaHash-V1", "network": "regtest", "genesis": genesis,
                                          "session": job["extranonce_namespace"], "extranonce_namespace": job["extranonce_namespace"],
                                          "resume_token": "12" * 32, "pool_name": "Fixture", "fee_base_units": 0, "heartbeat_seconds": 30}
                                if scenario == "version":
                                    result["version"] = 2
                                if scenario == "network":
                                    result["genesis"] = "00" * 32
                                if scenario == "resume_rejected" and self.resume:
                                    error = {"code": 104, "message": "unknown resume token"}
                                if scenario == "out_of_order":
                                    identifier += 1
                                if scenario == "malformed":
                                    secured.sendall(b'{"id":1,"id":2}\n')
                                    break
                                if scenario == "oversized":
                                    secured.sendall(b"x" * (8 * 1024 * 1024 + 1))
                                    break
                                if scenario == "unknown_notification":
                                    secured.sendall(b'{"method":"unsupported","params":{}}\n')
                                    break
                                if scenario == "excessive_rate":
                                    notification = json.dumps({"method": "parent", "params": {"parent": job["parent"], "clean_jobs": True}}).encode() + b"\n"
                                    secured.sendall(notification * 65)
                                    break
                            elif method == "ping":
                                result = {"time": 2**63 - 1 if scenario == "clock" else int(time.time())}
                            elif method == "getjob":
                                self.jobs += 1
                                result = job
                                if scenario == "invalid_job":
                                    result = dict(job, network_target="00" * 32)
                                if scenario == "expired_job":
                                    result = dict(job, expires=int(time.time()) - 1)
                                if scenario == "reused_namespace" and self.jobs > 1:
                                    result = dict(job, job_id="88" * 32)
                            elif method == "manifest":
                                result = manifest
                            elif method == "status":
                                result = {"healthy": True, "fee_base_units": 0, "accepted_shares": len(self.submits), "connected_sessions": 1}
                            elif method == "submit":
                                nonce = params["nonce"]
                                header = bytes.fromhex(job["header"])
                                work = hash256(header[:76] + nonce.to_bytes(4, "little"))[::-1].hex()
                                duplicate = nonce in self.submits
                                if not duplicate:
                                    self.submits.append(nonce)
                                sequence = str(self.submits.index(nonce) + 1)
                                receipt_id = hash256(ser_string(b"MCA-PPLNS/1/receipt") + ser_string(f"{genesis}:{work}:{sequence}".encode()))[::-1].hex()
                                receipt = {"sequence": sequence, "receipt_id": receipt_id, "work_id": work,
                                           "job_id": job["job_id"], "snapshot_id": job["snapshot_id"], "score": "1", "accepted": True, "network_candidate": False}
                                if scenario in ("resume", "resume_rejected") and not self.resume:
                                    break  # Persist acceptance, deliberately lose the ACK.
                                if scenario == "duplicate" or duplicate:
                                    error = {"code": 108, "message": "duplicate"}
                                elif scenario in ("stale", "rejected"):
                                    error = {"code": 107 if scenario == "stale" else 109, "message": "fixture refusal"}
                                    receipt = None
                                else:
                                    result, receipt = receipt, None
                            reply = {"id": identifier, "result": None if error else result, "error": error}
                            if receipt:
                                reply["receipt"] = receipt
                            encoded = json.dumps(reply).encode() + b"\n"
                            if scenario == "truncated":
                                secured.sendall(encoded[:len(encoded) // 2])
                                break
                            if scenario == "fragmented":
                                for offset in range(0, len(encoded), 64):
                                    secured.sendall(encoded[offset:offset + 64])
                                    time.sleep(0.002)
                            else:
                                file.write(encoded)
                                file.flush()
                        file.close()
                        self.active = None
                except (ssl.SSLError, ConnectionError, BrokenPipeError, OSError):
                    connection.close()
                except Exception as exc:
                    self.errors.append(type(exc).__name__)
        self.thread = threading.Thread(target=serve)
        self.thread.start()

    def close(self):
        self.stop.set()
        self.listener.close()
        if self.active is not None:
            try:
                self.active.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self.thread.join(timeout=3)
        assert not self.thread.is_alive()
        assert not self.errors, self.errors


class MercaturaPoolNativeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.wallet_names = []
        self.rpc_timeout = 120

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        binaries = Path(self.config["environment"]["BUILDDIR"]) / "bin"
        self.pool = binaries / "mercatura-pool"
        self.miner = binaries / "pool-native-test-client"
        self.hash_worker = binaries / "pool-hash-test-client"
        if not all(path.is_file() for path in (self.pool, self.miner, self.hash_worker)):
            raise SkipTest("M4 Qt test client and optional coordinator not built")

    def run_test(self):
        node = self.nodes[0]
        wallets, addresses = [], []
        for i in range(3):
            node.createwallet(wallet_name=f"native-pool-{i}")
            wallet = node.get_wallet_rpc(f"native-pool-{i}")
            address = wallet.getnewaddress("M4 native direct payout")
            assert address.startswith("mcrt1z")
            assert wallet.getaddressinfo(address)["ismine"]
            wallets.append(wallet)
            addresses.append(address)
        root = Path(self.options.tmpdir)
        # The driver must reject partial, signed and overflowing numeric
        # arguments before creating a mining session or connecting a socket.
        for index, value in ((1, "1junk"), (1, "+1"), (1, " 1"), (1, "0"), (1, "65536"),
                             (4, "1junk"), (4, "-1"), (4, "4294967296"),
                             (5, "1junk"), (5, "-1"), (5, "4294967296")):
            arguments = ["127.0.0.1", "1", "", addresses[0], "1", "1"]
            arguments[index] = value
            invalid = subprocess.run([str(self.miner), *arguments], capture_output=True, timeout=5)
            assert_equal(invalid.returncode, 2)
        certificate, key = root / "pool-ca.crt", root / "pool-tls.key"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1",
                        "-keyout", str(key), "-out", str(certificate)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        key.chmod(0o600)
        url = urlsplit(node.url)
        user, password = get_auth_cookie(node.datadir_path, self.chain)
        port = free_port()
        genesis = node.getblockhash(0)
        config = {"network": "regtest", "genesis": genesis, "rpc_url": f"http://127.0.0.1:{url.port}/",
                  "rpc_user": user, "rpc_password": password, "database": str(root / "m4.sqlite"), "port": port,
                  "tls_certificate": str(certificate), "tls_key": str(key), "warmup": True,
                  "share_target": "ff" * 32, "refresh_seconds": 1, "lifetime_seconds": 120}
        configuration = root / "pool.json"
        configuration.write_text(json.dumps(config))
        configuration.chmod(0o600)
        log = (root / "coordinator.log").open("wb")
        process = subprocess.Popen([str(self.pool), str(configuration)], stdout=log, stderr=log)
        clients, miners = [], []

        def listening():
            assert process.poll() is None, "coordinator exited"
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                    return True
            except OSError:
                return False

        def native(endpoint_port, ca=certificate, seconds=4, host="127.0.0.1", restart=False):
            arguments = [str(self.miner), host, str(endpoint_port), str(ca), addresses[0], "1", str(seconds)]
            if restart:
                arguments.append("restart-after-submit")
            return subprocess.Popen(arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        def result(miner):
            stdout, stderr = miner.communicate(timeout=60)
            assert "12" * 32 not in stdout + stderr  # Synthetic bearer token must stay private.
            value = json.loads(stdout)
            assert_equal(value["active_workers"], 0)
            return value

        try:
            self.wait_until(listening)
            # Seed three real ordinary shares using the independently tested M3 worker.
            # This guarantees the first native candidate has a shared PQ snapshot.
            for address in addresses:
                client = TLSClient(port, certificate)
                clients.append(client)
                hello = client.request("hello", {"version": 1, "network": "regtest", "genesis": genesis, "algorithm": "MercaHash-V1", "payout": address})
                assert_equal(hello["error"], None)
                job = client.request("getjob")["result"]
                nonce = json.loads(subprocess.run([str(self.hash_worker), job["header"], job["share_target"], job["network_target"], "ordinary"], check=True, capture_output=True, text=True, timeout=120).stdout)["nonce"]
                receipt = client.request("submit", {"job_id": job["job_id"], "nonce": nonce})
                assert_equal(receipt["error"], None)
                assert_equal(receipt["result"]["network_candidate"], False)
            time.sleep(1.1)
            job = clients[0].request("getjob")["result"]
            manifest = clients[0].request("manifest", {"job_id": job["job_id"]})["result"]
            assert_equal(len(manifest["allocations"]), 3)
            for client in clients:
                client.close()
            clients.clear()

            invalid_scenarios = ("version", "network", "clock", "out_of_order", "malformed", "oversized", "unknown_notification", "excessive_rate", "invalid_job", "expired_job")
            for scenario in (*invalid_scenarios, "reused_namespace", "duplicate", "stale", "rejected", "resume", "resume_rejected", "fragmented", "truncated"):
                fixture = FaultPool(certificate, key, genesis, job, manifest, scenario)
                try:
                    observed = result(native(fixture.port, seconds=5 if scenario.startswith("resume") else 3))
                    if scenario in invalid_scenarios:
                        assert_equal(observed["failed"], True)
                        assert_equal(observed["hashes"], 0)
                    elif scenario == "reused_namespace":
                        assert_equal(observed["failed"], True)
                        assert observed["hashes"] > 0
                    elif scenario == "duplicate":
                        assert observed["accepted"] > 0
                    elif scenario == "stale":
                        assert observed["stale"] > 0
                    elif scenario == "rejected":
                        assert observed["rejected"] > 0
                    elif scenario == "resume":
                        assert fixture.resume
                        assert observed["accepted"] > 0
                        assert observed["connections"] >= 2
                        assert_equal(len(fixture.submits), 1)  # No cursor restart after reconnect.
                    elif scenario == "fragmented":
                        assert observed["accepted"] > 0
                        assert observed["hashes"] > 0
                        assert_equal(observed["failed"], False)
                    elif scenario == "truncated":
                        assert_equal(observed["hashes"], 0)
                        assert_equal(observed["failed"], False)
                        assert_equal(fixture.jobs, 0)
                    else:
                        assert fixture.resume
                        assert_equal(len(fixture.submits), 1)
                        assert_equal(observed["accepted"], 0)
                        assert_equal(observed["failed"], False)
                finally:
                    fixture.close()

            fixture = FaultPool(certificate, key, genesis, job, manifest, "resume")
            try:
                observed = result(native(fixture.port, seconds=5, restart=True))
                assert observed["restarted"]
                assert fixture.resume
                assert observed["accepted"] > 0, observed
                assert_equal(len(fixture.submits), 1)
            finally:
                fixture.close()

            wrong_ca = result(native(port, ca=root / "missing.crt"))
            assert_equal(wrong_ca["failed"], True)
            assert_equal(wrong_ca["hashes"], 0)
            untrusted = result(native(port, ca=""))
            assert_equal(untrusted["failed"], True)
            assert_equal(untrusted["hashes"], 0)
            wrong_hostname = result(native(port, host="localhost"))
            assert_equal(wrong_hostname["failed"], True)
            assert_equal(wrong_hostname["hashes"], 0)
            unavailable = result(native(free_port(), seconds=2))
            assert_equal(unavailable["hashes"], 0)

            # Freeze each first native job long enough to find a network hit.
            # With one-second refresh and concurrent miners, ordinary shares
            # could legitimately clip the third seed out before height 1.
            process.terminate()
            assert_equal(process.wait(timeout=30), 0)
            config.update(refresh_seconds=120, lifetime_seconds=180)
            configuration.write_text(json.dumps(config))
            process = subprocess.Popen([str(self.pool), str(configuration)], stdout=log, stderr=log)
            self.wait_until(listening)

            for address in addresses[:2]:
                miners.append(subprocess.Popen([str(self.miner), "127.0.0.1", str(port), str(certificate), address, "1", "25"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
            self.wait_until(lambda: node.getblockcount() >= 1, timeout=120)
            coinbase = node.getblock(node.getblockhash(1), 2)["tx"][0]
            outputs = {out["scriptPubKey"]["hex"]: out["value"] for out in coinbase["vout"] if out["value"]}
            assert_equal(set(outputs), {wallet.getaddressinfo(address)["scriptPubKey"] for wallet, address in zip(wallets, addresses)})
            assert_equal(sum(outputs.values()), Decimal(manifest["reward"]) / 100)
            for wallet, address in zip(wallets, addresses):
                transaction = wallet.gettransaction(coinbase["txid"])
                assert transaction["confirmations"] >= 1
                assert_equal(sum(entry["amount"] for entry in transaction["details"]), outputs[wallet.getaddressinfo(address)["scriptPubKey"]])
            process.terminate()
            assert_equal(process.wait(timeout=30), 0)
            # End the initial snapshot freeze. Resume deliberately skips a
            # previously handed-out header, so recovery needs normal fresh jobs.
            config.update(refresh_seconds=1, lifetime_seconds=120)
            configuration.write_text(json.dumps(config))
            process = subprocess.Popen([str(self.pool), str(configuration)], stdout=log, stderr=log)
            self.wait_until(listening)
            for miner in miners:
                stats = result(miner)
                assert_equal(stats["failed"], False)
                assert stats["accepted"] > 0, stats
                assert stats["hashes"] > 0, stats
                assert stats["connections"] >= 2, stats
            miners.clear()
            audit = TLSClient(port, certificate)
            clients.append(audit)
            assert_equal(audit.request("hello", {"version": 1, "network": "regtest", "genesis": genesis, "algorithm": "MercaHash-V1", "payout": addresses[0]})["error"], None)
            status = audit.request("status")["result"]
            assert status["accepted_shares"] > 3
            assert_equal(status["fee_base_units"], 0)
            self.log.info("M4 real TLS, receipt faults/resume, real native shares and zero-fee multi-PQ blocks passed")
        finally:
            for miner in miners:
                if miner.poll() is None:
                    miner.terminate()
                miner.communicate(timeout=30)
            for client in clients:
                client.close()
            process.terminate()
            process.wait(timeout=30)
            log.close()
            configuration.unlink(missing_ok=True)
            key.unlink(missing_ok=True)


if __name__ == "__main__":
    MercaturaPoolNativeTest(__file__).main()
