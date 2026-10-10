#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Coordinator process tests with an explicitly synthetic RPC fixture.

Hashes, sockets, TLS and SQLite are real. No fixture block is chain-validated;
submitblock deliberately rejects every block. This is NOT the live regtest test.
"""
import argparse
import base64
import hashlib
import http.server
import json
import pathlib
import socket
import sqlite3
import ssl
import subprocess
import tempfile
import threading
import time


GENESIS = "8e2308efb3a16b126e69444329cc0ed81bea0596e99db1032ccd750e7028f685"


def sha256d(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Fixture:
    def __init__(self):
        self.parent = "11" * 32
        self.height = 1
        self.submitted = []
        self.headers = {}
        self.active = {}
        self.submit_entered = threading.Event()
        self.submit_release = threading.Event()
        self.submit_release.set()
        self.delay_method = None
        self.delay_seconds = 0
        fixture = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                if self.headers.get("Authorization") != "Basic " + base64.b64encode(b"fixture:fixture-secret").decode():
                    self.send_error(401)
                    return
                request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                method, args = request["method"], request["params"]
                if method == fixture.delay_method:
                    time.sleep(fixture.delay_seconds)
                result, error = None, None
                if method == "getblockchaininfo":
                    result = {"chain": "regtest", "initialblockdownload": False}
                elif method == "getblockhash":
                    result = GENESIS if args[0] == 0 else fixture.active.get(args[0], "22" * 32)
                elif method == "getbestblockhash":
                    result = fixture.parent
                elif method == "fixture_advance_parent":
                    fixture.parent = "55" * 32
                    fixture.height += 1
                    # Ensure the service's one-second parent poll is due when
                    # the deterministic helper next drains completed hashes.
                    time.sleep(1.1)
                    result = fixture.parent
                elif method == "getblocktemplate":
                    if args[0].get("mode") == "proposal":
                        result = None  # Schema fixture only; never real validation.
                    else:
                        result = {"version": 0x20000000, "height": fixture.height,
                                  "previousblockhash": fixture.parent, "bits": "207fffff",
                                  "target": f"{0x7fffff << 232:064x}",
                                  "curtime": int(time.time()), "mintime": int(time.time()) - 1,
                                  "coinbasevalue": 100000, "coinbaseaux": {}, "transactions": [],
                                  "sizelimit": 1048576, "sigoplimit": 80000,
                                  "default_witness_commitment": "6a24aa21a9ed" + sha256d(bytes(64)).hex()}
                elif method == "validateaddress":
                    address = args[0]
                    if address in {"fixture-pq-1", "fixture-pq-2", "fixture-pq-3", "fixture-pq-4"}:
                        result = {"isvalid": True, "scriptPubKey": "5220" + int(address[-1]).to_bytes(32, "little").hex()}
                    else:
                        result = {"isvalid": False}
                elif method == "submitblock":
                    fixture.submitted.append(args[0])
                    fixture.submit_entered.set()
                    fixture.submit_release.wait(timeout=15)
                    result = "fixture-rejected"
                elif method == "getblockheader":
                    if args[0] in fixture.headers:
                        result = fixture.headers[args[0]]
                    else:
                        error = {"code": -5, "message": "fixture has no chain"}
                else:
                    error = {"code": -32601, "message": "unsupported fixture RPC"}
                data = json.dumps({"id": request["id"], "result": result, "error": error}).encode()
                try:
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                except (BrokenPipeError, ConnectionResetError):
                    pass  # The crash test intentionally kills the RPC caller.

            def log_message(self, *_args):
                pass

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()


class Client:
    def __init__(self, listen_port, tls=None, receive_buffer=None, source=None):
        source_address = (source, 0) if source else None
        self.sock = socket.create_connection(("127.0.0.1", listen_port), timeout=15, source_address=source_address)
        if receive_buffer:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
        if tls:
            self.sock = tls.wrap_socket(self.sock, server_hostname="127.0.0.1")
        self.file = self.sock.makefile("rwb")
        self.identifier = 0

    def request(self, method, params=None):
        self.identifier += 1
        data = {"id": self.identifier, "method": method, "params": {} if params is None else params}
        self.file.write(json.dumps(data).encode() + b"\n")
        self.file.flush()
        while True:
            raw = self.file.readline()
            assert raw, "connection closed while awaiting reply"
            reply = json.loads(raw)
            if reply.get("id") == self.identifier:
                return reply

    def hello(self, payout=None, token=None, **extra):
        params = {"version": 1, "network": "regtest", "genesis": GENESIS, "algorithm": "MercaHash-V1"}
        params["resume_token" if token else "payout"] = token or payout
        params.update(extra)
        return self.request("hello", params)

    def close(self):
        self.file.close()
        self.sock.close()


def wait_for(predicate, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.1)
    raise AssertionError("condition timed out")


def run(binary):
    fixture = Fixture()
    with tempfile.TemporaryDirectory(prefix="mercatura-pool-service-") as temporary:
        root = pathlib.Path(temporary)
        config = {"network": "regtest", "genesis": GENESIS,
                  "rpc_url": f"http://127.0.0.1:{fixture.server.server_port}/",
                  "rpc_user": "fixture", "rpc_password": "fixture-secret",
                  "database": str(root / "pool.sqlite"), "port": port(), "plaintext_regtest": True,
                  "share_target": "ff" * 32, "refresh_seconds": 1, "lifetime_seconds": 30,
                  "payout_cap": 3, "reconcile_seconds": 1, "messages_per_session": 1000,
                  "messages_per_ip": 10000, "warmup": False}
        path = root / "config.json"
        process = None
        clients = []
        output = (root / "service.log").open("wb")

        def start():
            nonlocal process
            path.write_text(json.dumps(config))
            process = subprocess.Popen([binary, str(path)], stdout=output, stderr=output)

            def listening():
                assert process.poll() is None, (root / "service.log").read_text()
                try:
                    with socket.create_connection(("127.0.0.1", config["port"]), timeout=0.2):
                        return True
                except OSError:
                    return False
            wait_for(listening)

        def stop():
            nonlocal process
            for client in clients:
                client.close()
            clients.clear()
            process.terminate()
            assert process.wait(timeout=30) == 0
            process = None

        try:
            original_config = dict(config)
            config.update(database=str(root / "ip-cache.sqlite"), max_sessions=1, max_sessions_per_ip=1)
            start()
            time.sleep(0.1)  # Drain the startup probe from the single socket slot.
            for last in range(2, 5):
                probe = Client(config["port"], source=f"127.0.0.{last}")
                assert probe.request("ping")["error"]["code"] == 104
                probe.close()
                time.sleep(0.1)
            probe = Client(config["port"], source="127.0.0.5")
            try:
                assert not probe.file.readline(), "new IP exceeded bounded rate-cache capacity"
            except ConnectionResetError:
                pass
            finally:
                probe.close()
            probe = Client(config["port"], source="127.0.0.2")
            assert probe.request("ping")["error"]["code"] == 104
            probe.close()
            stop()
            config.clear()
            config.update(original_config)
            print("PASS actual distinct-IP flood hits rate-cache bound while existing IPs remain usable")
            lifecycle = dict(config, database=str(root / "lifecycle.sqlite"), warmup=True)
            lifecycle_file = root / "lifecycle.json"
            lifecycle_file.write_text(json.dumps(lifecycle))
            unit = pathlib.Path(binary).resolve().with_name("test_mercatura_pool")
            result = subprocess.run([str(unit), "--service-config", str(lifecycle_file)],
                                    text=True, capture_output=True, timeout=30)
            assert result.returncode == 0, result.stderr + result.stdout
            print(result.stdout.strip())
            fixture.parent = "11" * 32
            fixture.height = 1
            original_config = dict(config)
            config.update(database=str(root / "rate.sqlite"), payout_cap=10,
                          new_identities_per_ip=1, new_identities_global=2,
                          new_sessions_per_ip=2, new_sessions_global=4)
            start()
            first = Client(config["port"])
            second = Client(config["port"])
            clients.extend((first, second))
            rate_token = first.hello("fixture-pq-1")["result"]["resume_token"]
            assert second.hello("fixture-pq-2")["error"]["code"] == 111
            stop()
            start()
            first = Client(config["port"])
            second = Client(config["port"])
            clients.extend((first, second))
            assert first.hello(token=rate_token)["error"] is None
            assert second.hello("fixture-pq-2")["error"]["code"] == 111
            assert second.hello("fixture-pq-1")["error"] is None
            third = Client(config["port"])
            clients.append(third)
            assert third.hello("fixture-pq-1")["error"]["code"] == 111
            stop()
            config.clear()
            config.update(original_config)
            print("PASS actual durable identity/session registration limits; known payouts avoid identity quota and resume avoids namespace quota")
            start()
            invalid = Client(config["port"])
            clients.append(invalid)
            assert invalid.hello("fixture-pq-1", genesis="00" * 32)["error"]["code"] == 102
            assert invalid.hello("invalid")["error"]["code"] == 103
            credentials = []
            for index in range(1, 4):
                client = Client(config["port"])
                clients.append(client)
                hello = client.hello(f"fixture-pq-{index}")
                assert hello["error"] is None
                credentials.append(hello["result"])
                assert client.request("getjob")["error"]["code"] == 106
            assert len({item["extranonce_namespace"] for item in credentials}) == 3
            assert invalid.hello("fixture-pq-4")["error"]["code"] == 105
            print("PASS empty-window refusal, PQ/network checks, admission and unique namespaces")
            stop()
            config["warmup"] = True
            start()
            miners = []
            for credential in credentials:
                client = Client(config["port"])
                clients.append(client)
                assert client.hello(token=credential["resume_token"])["error"] is None
                miners.append(client)
            jobs = [client.request("getjob")["result"] for client in miners]
            warmup_audits = [client.request("manifest", {"job_id": job["job_id"]})["result"]
                             for client, job in zip(miners, jobs)]
            for index, (job, audit) in enumerate(zip(jobs, warmup_audits), 1):
                assert job["warmup"] and job["fee_base_units"] == 0
                assert audit["warmup"] and audit["cutoff"] == 0 and len(audit["allocations"]) == 1
                assert audit["allocations"][0]["script"] == "5220" + index.to_bytes(32, "little").hex()
                assert audit["allocations"][0]["amount"] == audit["reward"]
            stop()
            start()
            miners = []
            for credential in credentials:
                client = Client(config["port"])
                clients.append(client)
                assert client.hello(token=credential["resume_token"])["error"] is None
                miners.append(client)
            first_share = miners[0].request("submit", {"job_id": jobs[0]["job_id"], "nonce": 0})
            assert first_share["error"] is None
            time.sleep(1.1)
            transition = miners[0].request("getjob")["result"]
            transition_audit = miners[0].request("manifest", {"job_id": transition["job_id"]})["result"]
            assert not transition["warmup"] and transition_audit["cutoff"] == 1
            stop()
            start()
            miners = []
            for credential in credentials:
                client = Client(config["port"])
                clients.append(client)
                assert client.hello(token=credential["resume_token"])["error"] is None
                miners.append(client)
            for client, job, original in zip(miners, jobs, warmup_audits):
                restored = client.request("manifest", {"job_id": job["job_id"]})["result"]
                assert restored["warmup"] and restored["allocations"] == original["allocations"]
            for client, job in zip(miners, jobs):
                assert len(job["header"]) == 160
                for nonce in (0, 1):
                    if client is miners[0] and nonce == 0:
                        continue
                    receipt = client.request("submit", {"job_id": job["job_id"], "nonce": nonce})
                    assert receipt["error"] is None, receipt
            assert miners[0].request("manifest", {"job_id": transition["job_id"]})["result"]["allocations"] == transition_audit["allocations"]
            assert not miners[0].request("getjob")["result"]["warmup"]
            print("PASS simultaneous individual warmup jobs, disconnected/restarted empty and first-share transitions, frozen earlier payouts")
            pending = set()
            for _ in range(32):
                miners[0].identifier += 1
                pending.add(miners[0].identifier)
                miners[0].file.write(json.dumps({"id": miners[0].identifier, "method": "ping", "params": {}}).encode() + b"\n")
            miners[0].file.flush()
            while pending:
                reply = json.loads(miners[0].file.readline())
                if reply.get("id") in pending:
                    assert reply["error"] is None
                    pending.remove(reply["id"])
            print("PASS 32 buffered requests drain without requiring additional socket input")
            assert miners[1].request("submit", {"job_id": jobs[0]["job_id"], "nonce": 10})["error"]["code"] == 104
            assert miners[0].request("submit", {"job_id": jobs[0]["job_id"], "nonce": 10, "difficulty": 99})["error"]["code"] == 100
            assert miners[0].request("submit", {"job_id": jobs[0]["job_id"], "nonce": -1})["error"]["code"] == 100
            assert miners[0].request("submit", {"job_id": jobs[0]["job_id"], "nonce": 10, "ntime": 0})["error"]["code"] == 100
            duplicate = miners[0].request("submit", {"job_id": jobs[0]["job_id"], "nonce": 0})
            assert duplicate["error"]["code"] == 108 and duplicate["receipt"]["accepted"]
            print("PASS real native hash shares; forged difficulty, cross-session, duplicate and header checks")
            time.sleep(1.1)
            shared = miners[0].request("getjob")["result"]
            manifest = miners[0].request("manifest", {"job_id": shared["job_id"]})["result"]
            # Latest four shares above are from identities 2 and 3. Historical
            # identity 1 has not been evicted from admission or ledger.
            assert manifest["warmup"] is False and manifest["cutoff"] == 6
            assert sum(p["amount"] for p in manifest["allocations"]) == manifest["reward"]
            assert int(manifest["total_work"]) == 4
            assert miners[0].request("status")["result"]["accepted_shares"] == 6
            statistics = miners[0].request("status")["result"]
            assert statistics["cached_jobs"] <= statistics["connected_sessions"]
            old_snapshot = shared["snapshot_id"]
            receipt = miners[0].request("submit", {"job_id": shared["job_id"], "nonce": 0})
            assert receipt["error"] is None
            assert miners[0].request("manifest", {"job_id": shared["job_id"]})["result"]["snapshot_id"] == old_snapshot
            print("PASS frozen cutoff, exact payout conservation and future-share isolation")
            saved_receipt = receipt["result"]["receipt_id"]
            stop()
            start()
            client = Client(config["port"])
            clients.append(client)
            assert client.hello(token=credentials[0]["resume_token"])["error"] is None
            duplicate = client.request("submit", {"job_id": shared["job_id"], "nonce": 0})
            assert duplicate["error"]["code"] == 108 and duplicate["receipt"]["receipt_id"] == saved_receipt
            assert client.request("status")["result"]["accepted_shares"] == 7
            with sqlite3.connect(config["database"]) as db:
                assert db.execute("SELECT COUNT(*) FROM shares").fetchone()[0] == 7
                rows = db.execute("SELECT hash FROM candidates LIMIT 1").fetchall()
            if rows:
                # Explicit simulated chain observations test reconciliation,
                # maturity and reorg behavior; never a real accepted block.
                candidate = rows[0][0]
                fixture.headers[candidate] = {"height": 1, "confirmations": 1}
                fixture.active[1] = candidate
                wait_for(lambda: client.request("status")["result"]["blocks"].get("active", 0) > 0)
                fixture.headers[candidate]["confirmations"] = 101
                wait_for(lambda: client.request("status")["result"]["blocks"].get("matured", 0) > 0)
                fixture.headers[candidate]["confirmations"] = -1
                fixture.active[1] = "33" * 32
                wait_for(lambda: client.request("status")["result"]["blocks"].get("orphaned", 0) > 0)
                print("PASS simulated active-chain, maturity and orphan reconciliation (RPC fixture)")
            fixture.parent = "44" * 32
            fixture.height = 2
            wait_for(lambda: client.request("status")["result"]["parent"] == fixture.parent)
            assert client.request("submit", {"job_id": shared["job_id"], "nonce": 20})["error"]["code"] == 107
            print("PASS persist-before-receipt, restart recovery and stale-parent invalidation")
            crash_job = client.request("getjob")["result"]
            worker = pathlib.Path(binary).resolve().with_name("pool-hash-test-client")
            solved = subprocess.run([str(worker), crash_job["header"], crash_job["share_target"],
                                     crash_job["network_target"], "network"], check=True,
                                    text=True, capture_output=True, timeout=120)
            crash_nonce = json.loads(solved.stdout)["nonce"]
            submitted_before = len(fixture.submitted)
            fixture.submit_entered.clear()
            fixture.submit_release.clear()
            client.identifier += 1
            frame = {"id": client.identifier, "method": "submit", "params": {
                "job_id": crash_job["job_id"], "nonce": crash_nonce}}
            client.file.write(json.dumps(frame).encode() + b"\n")
            client.file.flush()
            assert fixture.submit_entered.wait(timeout=15), "network candidate did not reach RPC"
            process.kill()
            assert process.wait(timeout=30) != 0
            process = None
            for connection in clients:
                connection.close()
            clients.clear()
            fixture.submit_release.set()
            with sqlite3.connect(config["database"]) as db:
                assert db.execute("SELECT COUNT(*) FROM shares").fetchone()[0] == 8
                assert db.execute("SELECT state FROM candidates WHERE job=?", (crash_job["job_id"],)).fetchone()[0] == "discovered"
            start()
            client = Client(config["port"])
            clients.append(client)
            assert client.hello(token=credentials[0]["resume_token"])["error"] is None
            replay = client.request("submit", {"job_id": crash_job["job_id"], "nonce": crash_nonce})
            assert replay["error"]["code"] == 108 and replay["receipt"]["network_candidate"]
            assert client.request("status")["result"]["accepted_shares"] == 8
            assert len(fixture.submitted) >= submitted_before + 2
            print("PASS actual SIGKILL during submitblock; atomic share/candidate recovery and RPC retry")
            stop()
            config.update(rpc_timeout_seconds=1, rpc_connect_timeout_seconds=1)
            start()
            client = Client(config["port"])
            clients.append(client)
            assert client.hello(token=credentials[0]["resume_token"])["error"] is None
            paused_job = client.request("getjob")["result"]
            fixture.delay_method = "getbestblockhash"
            fixture.delay_seconds = 2
            wait_for(lambda: not client.request("status")["result"]["healthy"])
            assert client.request("getjob")["error"]["code"] == 112
            assert client.request("submit", {"job_id": paused_job["job_id"], "nonce": 999})["error"]["code"] == 107
            assert client.request("status")["result"]["accepted_shares"] == 8
            fixture.delay_method = None
            wait_for(lambda: client.request("status")["result"]["healthy"])
            assert client.request("manifest", {"job_id": paused_job["job_id"]})["result"]["snapshot_id"] == paused_job["snapshot_id"]
            print("PASS delayed RPC exceeds configured deadline, pauses work without lost shares and recovers")
            stop()
            config.update(rpc_timeout_seconds=10, rpc_connect_timeout_seconds=10)
            certificate, key = root / "tls.crt", root / "tls.key"
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                            "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1",
                            "-keyout", str(key), "-out", str(certificate)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            config.update(plaintext_regtest=False, tls_certificate=str(certificate), tls_key=str(key), port=port())
            start()
            result = subprocess.run([str(unit), "--tls-config", str(path)],
                                    text=True, capture_output=True, timeout=30)
            assert result.returncode == 0, result.stderr + result.stdout
            print(result.stdout.strip())
            context = ssl.create_default_context(cafile=str(certificate))
            client = Client(config["port"], context)
            clients.append(client)
            assert client.hello(token=credentials[0]["resume_token"])["error"] is None
            job = client.request("getjob")["result"]
            # Pipelined requests reach the explicit per-session outstanding
            # bound without needing a timing-dependent slow mock hash.
            pending_ids = []
            for nonce in range(100, 104):
                client.identifier += 1
                pending_ids.append(client.identifier)
                frame = {"id": client.identifier, "method": "submit", "params": {"job_id": job["job_id"], "nonce": nonce}}
                client.file.write(json.dumps(frame).encode() + b"\n")
            client.file.flush()
            results = []
            while len(results) < len(pending_ids):
                response = json.loads(client.file.readline())
                if response.get("id") in pending_ids:
                    results.append(response)
            assert any(result.get("error", {}) and result["error"]["code"] == 110 for result in results)
            print("PASS actual pipelined verification overload refusal")
            with socket.create_connection(("127.0.0.1", config["port"]), timeout=3) as plain:
                plain.sendall(b'{"id":1,"method":"ping","params":{}}\n')
                try:
                    assert not plain.recv(128), "TLS listener accepted plaintext"
                except ConnectionResetError:
                    pass
            print("PASS actual TLS handshake, authenticated certificate and no plaintext fallback")
            slow = Client(config["port"], context, receive_buffer=16384)
            clients.append(slow)
            assert slow.hello(token=credentials[1]["resume_token"])["error"] is None
            slow_job = slow.request("getjob")["result"]
            sample = slow.request("manifest", {"job_id": slow_job["job_id"]})
            count = (4 * 1024 * 1024 + len(json.dumps(sample)) - 1) // len(json.dumps(sample))
            identifiers = set()
            for offset in range(0, count, 16):
                for _ in range(min(16, count - offset)):
                    slow.identifier += 1
                    identifiers.add(slow.identifier)
                    slow.file.write(json.dumps({"id": slow.identifier, "method": "manifest",
                                                "params": {"job_id": slow_job["job_id"]}}).encode() + b"\n")
                slow.file.flush()
                time.sleep(0.06)
            while identifiers:
                reply = json.loads(slow.file.readline())
                if reply.get("id") in identifiers:
                    assert reply["error"] is None
                    assert reply["result"]["snapshot_id"] == slow_job["snapshot_id"]
                    identifiers.remove(reply["id"])
            print("PASS actual TCP/TLS slow reader preserves every queued response (WAN qualification remains separate)")
            stop()
        finally:
            fixture.submit_release.set()
            fixture.delay_method = None
            for client in clients:
                client.close()
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=30)
            output.close()
            fixture.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pool-binary", required=True)
    run(parser.parse_args().pool_binary)
