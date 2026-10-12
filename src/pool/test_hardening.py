#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Real socket/native-hash/SQLite VarDiff and fail-closed regressions.

Uses the M3 synthetic RPC fixture; this does not validate a chain or hardware.
Wall-clock windows are deliberately 30 seconds, with bounded native hash load.
"""
import argparse
import json
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import time

from test_service import Client, Fixture, GENESIS, port, sha256d, wait_for


def run(binary):
    fixture = Fixture()
    fixture.bits = 0x1d00ffff  # Network floor is harder than test share targets.
    bench = str(Path(binary).resolve().with_name("pool-verifier-bench"))
    with tempfile.TemporaryDirectory(prefix="mercatura-hardening-") as temporary:
        root = Path(temporary)
        config = {"network": "regtest", "genesis": GENESIS,
                  "rpc_url": f"http://127.0.0.1:{fixture.server.server_port}/",
                  "rpc_user": "fixture", "rpc_password": "fixture-secret",
                  "database": str(root / "pool.sqlite"), "port": port(), "plaintext_regtest": True,
                  "warmup": True, "vardiff": True, "share_target": "7" + "f" * 63,
                  "share_max_target": "ff" * 32, "share_interval_seconds": 30,
                  "vardiff_min_seconds": 30, "vardiff_window_seconds": 30,
                  "refresh_seconds": 1, "lifetime_seconds": 180, "reconcile_seconds": 1, "stats_seconds": 0}
        file = root / "config.json"
        clients = []
        process = None
        log = (root / "service.log").open("wb")

        def start():
            nonlocal process
            file.write_text(json.dumps(config))
            process = subprocess.Popen([binary, str(file)], stdout=log, stderr=log)

            def listening():
                assert process.poll() is None, (root / "service.log").read_text()
                try:
                    probe = Client(config["port"])
                    probe.close()
                    return True
                except OSError:
                    return False
            wait_for(listening)

        def stop():
            nonlocal process
            for client in clients:
                client.close()
            clients.clear()
            if process and process.poll() is None:
                process.terminate()
                assert process.wait(timeout=30) == 0
            process = None

        def connect(token=None):
            client = Client(config["port"])
            clients.append(client)
            reply = client.hello(token=token) if token else client.hello(payout="fixture-pq-1")
            assert reply["error"] is None, reply
            return client, reply["result"]["resume_token"]

        def getjob(client):
            reply = client.request("getjob")
            assert reply["error"] is None, reply
            return reply["result"]

        def nonces(job, count=24):
            output = subprocess.run([bench, "--samples", job["header"], str(count)],
                                    check=True, text=True, capture_output=True, timeout=60)
            return [row["nonce"] for row in json.loads(output.stdout)
                    if int(job["network_target"], 16) < int(row["hash"], 16) <= int(job["share_target"], 16)]

        def submit(client, job, nonce):
            return client.request("submit", {"job_id": job["job_id"], "nonce": nonce})

        def keep_jobs(client, seconds):
            deadline = time.monotonic() + seconds
            last = None
            while time.monotonic() < deadline:
                last = getjob(client)
                time.sleep(1.05)
            return last

        try:
            start()
            client, token = connect()
            original = getjob(client)
            samples = nonces(original)
            assert len(samples) >= 6, samples
            for nonce in samples[:4]:
                reply = submit(client, original, nonce)
                assert reply["error"] is None and reply["result"]["score"] == "2", reply
            harder = keep_jobs(client, 31)
            assert (1 << 256) // (int(harder["share_target"], 16) + 1) == 4, harder
            old = submit(client, original, samples[4])
            assert old["error"] is None and old["result"]["score"] == "2", old
            fresh = nonces(harder)
            assert fresh
            reply = submit(client, harder, fresh[0])
            assert reply["error"] is None and reply["result"]["score"] == "4", reply
            print("PASS real VarDiff increase; old jobs keep score 2, new jobs score 4", flush=True)
            # This observation contains less than one target interval of work;
            # the subsequent zero-work window safely eases it again.
            easier = keep_jobs(client, 62)
            assert int(easier["share_target"], 16) > int(harder["share_target"], 16), easier
            assigned = easier["share_target"]
            with sqlite3.connect(config["database"]) as db:
                assert db.execute("SELECT target FROM sessions").fetchone()[0] == assigned
                assert [r[0] for r in db.execute("SELECT score FROM shares ORDER BY sequence")] == ["2"] * 5 + ["4"]
                frozen = db.execute("SELECT target FROM jobs WHERE id=?", (original["job_id"],)).fetchone()[0]
                assert frozen == original["share_target"]
            stop()
            start()
            client, resumed = connect(token)
            assert resumed == token
            replay = submit(client, original, samples[4])
            assert replay["error"]["code"] == 108 and replay["receipt"] == old["result"], replay
            assert getjob(client)["share_target"] == assigned
            print("PASS downward adjustment, durable target/restart, exact scores and idempotent old receipt", flush=True)
            # Inject a real SQLite transaction abort, then remove the cause.
            # A healthy RPC refresh must not clear the storage failure latch.
            job = getjob(client)
            matching = nonces(job)
            assert matching
            with sqlite3.connect(config["database"]) as db:
                db.execute("CREATE TRIGGER test_abort BEFORE INSERT ON shares BEGIN SELECT RAISE(ABORT,'test interruption'); END;")
            failed = submit(client, job, matching[0])
            assert failed["error"]["code"] == 112 and failed["result"] is None, failed
            with sqlite3.connect(config["database"]) as db:
                assert db.execute("SELECT COUNT(*) FROM shares").fetchone()[0] == 6
                db.execute("DROP TRIGGER test_abort")
            time.sleep(1.2)
            status = client.request("status")["result"]
            assert status["accounting_fault"] and not status["healthy"]
            assert client.request("getjob")["error"]["code"] == 112
            assert submit(client, job, matching[0])["error"]["code"] == 112
            stop()
            start()
            client, _ = connect(token)
            recovered = submit(client, job, matching[0])
            assert recovered["error"] is None, recovered
            assert client.request("status")["result"]["accepted_shares"] == 7
            print("PASS SQLite rollback, latched work pause, restart and interrupted-share recovery", flush=True)
            # An easier network target always wins over configured share bounds.
            fixture.bits = 0x207fffff
            time.sleep(1.2)
            floor = getjob(client)
            assert int(floor["share_target"], 16) >= int(floor["network_target"], 16)
            print("PASS changed network target cannot exclude valid block candidates", flush=True)
            stop()
            # Two concurrent public block copies exceed an explicit 1 MiB
            # pending budget. Hash workers/queue remain bounded and usable after.
            script = b"\x6a" + bytes(600000)
            raw = (bytes.fromhex("0200000001") + b"\x01" * 32 + bytes(4) + b"\x00" + b"\xff" * 4
                   + b"\x01" + (1).to_bytes(8, "little") + b"\xfe" + len(script).to_bytes(4, "little")
                   + script + bytes(4))
            txid = sha256d(raw)
            fixture.transactions = [{"data": raw.hex(), "txid": txid[::-1].hex(), "hash": txid[::-1].hex()}]
            fixture.witness_commitment = "6a24aa21a9ed" + sha256d(sha256d(bytes(32) + txid) + bytes(32)).hex()
            config.update(database=str(root / "memory.sqlite"), vardiff=False,
                          share_target="ff" * 32, pending_block_memory_mib=1)
            start()
            client, _ = connect()
            job = getjob(client)
            identifiers = (client.identifier + 1, client.identifier + 2)
            frames = b"".join(json.dumps({"id": identifier, "method": "submit",
                                          "params": {"job_id": job["job_id"], "nonce": nonce}}).encode() + b"\n"
                              for nonce, identifier in enumerate(identifiers))
            client.file.write(frames)
            client.file.flush()
            received = {}
            while len(received) < 2:
                reply = json.loads(client.file.readline())
                if reply.get("id") in identifiers:
                    received[reply["id"]] = reply
            assert received[identifiers[0]]["error"] is None, received
            assert received[identifiers[1]]["error"]["code"] == 110, received
            client.identifier = identifiers[1]
            assert submit(client, job, 1)["error"] is None
            status = client.request("status")["result"]
            assert status["pending_block_bytes"] == 0 and status["overload_share_requests_since_start"] >= 1
            print("PASS actual pending-block budget overload, explicit refusal and recovery without lost ACK", flush=True)
            fixture.fail_submit = True
            time.sleep(1.1)
            job = getjob(client)
            hash_worker = str(Path(binary).resolve().with_name("pool-hash-test-client"))
            solved = subprocess.run([hash_worker, job["header"], job["share_target"], job["network_target"], "network"],
                                    check=True, capture_output=True, text=True, timeout=30)
            network_nonce = json.loads(solved.stdout)["nonce"]
            receipt = submit(client, job, network_nonce)
            assert receipt["error"] is None or receipt.get("receipt", {}).get("accepted"), receipt
            wait_for(lambda: client.request("status")["result"]["reconciliation_fault"])
            time.sleep(1.2)
            assert client.request("getjob")["error"]["code"] == 112
            fixture.fail_submit = False
            wait_for(lambda: client.request("status")["result"]["healthy"], timeout=30)
            print("PASS candidate RPC failure pauses issuance until actual reconciliation recovers", flush=True)
            with sqlite3.connect(config["database"]) as db:
                db.execute("UPDATE jobs SET target=? WHERE id=?", ("00" * 32, job["job_id"]))
            corrupt = client.request("manifest", {"job_id": job["job_id"]})
            assert corrupt["error"]["code"] == 112 and corrupt["result"] is None
            with sqlite3.connect(config["database"]) as db:
                db.execute("UPDATE jobs SET target=? WHERE id=?", (job["share_target"], job["job_id"]))
            time.sleep(1.2)
            assert client.request("status")["result"]["accounting_fault"]
            assert client.request("getjob")["error"]["code"] == 112
            print("PASS corrupted durable job commitment latches issuance pause after RPC refresh", flush=True)
            with sqlite3.connect(config["database"]) as db:
                db.execute("CREATE TRIGGER test_shutdown_fault BEFORE UPDATE ON sessions BEGIN SELECT RAISE(ABORT,'test shutdown fault'); END;")
            stop()  # Workers must join and exit cleanly despite unavailable metadata writes.
            print("PASS shutdown after accounting fault needs no successful metadata writes", flush=True)
        finally:
            stop()
            log.close()
            fixture.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pool-binary", required=True)
    run(parser.parse_args().pool_binary)
