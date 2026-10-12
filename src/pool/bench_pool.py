#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Bounded coordinator load with real hashes, sockets, SQLite WAL/FULL and RPC.

The RPC endpoint is synthetic and rejects blocks; this is not chain validation.
The deliberately maximal share target measures verification/commit capacity,
not VarDiff production demand or a tested VPS deployment. Run sequentially.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import tempfile
import time

from test_service import Client, Fixture, GENESIS, port, wait_for


def run(options):
    fixture = Fixture()
    fixture.bits = 0x1d00ffff
    process = None
    with tempfile.TemporaryDirectory(prefix="mercatura-pool-bench-") as temporary:
        root = Path(temporary)
        config = {"network": "regtest", "genesis": GENESIS,
                  "rpc_url": f"http://127.0.0.1:{fixture.server.server_port}/",
                  "rpc_user": "fixture", "rpc_password": "fixture-secret",
                  "database": str(root / "pool.sqlite"), "port": port(), "plaintext_regtest": True,
                  "warmup": True, "vardiff": False, "share_target": "ff" * 32,
                  "verifier_workers": options.workers, "verifier_memory_mib": options.workers * 160,
                  "verifier_queue": options.queue, "refresh_seconds": 15, "lifetime_seconds": 300,
                  "messages_per_session": 1000, "messages_per_ip": 10000,
                  "stats_seconds": 0}
        file = root / "config.json"
        file.write_text(json.dumps(config))
        log = (root / "service.log").open("wb")
        clients = []
        try:
            process = subprocess.Popen([options.pool_binary, str(file)], stdout=log, stderr=log)

            def listening():
                assert process.poll() is None, (root / "service.log").read_text()
                try:
                    probe = Client(config["port"])
                    probe.close()
                    return True
                except OSError:
                    return False
            wait_for(listening)
            jobs = []
            for index in range(options.sessions):
                client = Client(config["port"])
                clients.append(client)
                assert client.hello(payout=f"fixture-pq-{index % 4 + 1}")["error"] is None
                jobs.append(client.request("getjob")["result"])

            def mining(index):
                client, job = clients[index], jobs[index]
                accepted = overloaded = failed = 0
                for nonce in range(options.hashes):
                    for attempt in range(30):
                        reply = client.request("submit", {"job_id": job["job_id"], "nonce": nonce})
                        if reply["error"] is None:
                            assert reply["result"]["accepted"] and reply["result"]["score"] == "1"
                            accepted += 1
                            break
                        if reply["error"]["code"] == 110:
                            overloaded += 1
                            time.sleep(0.1)
                        else:
                            failed += 1
                            break
                    else:
                        failed += 1
                    if options.interval:
                        time.sleep(options.interval)
                return accepted, overloaded, failed

            started = time.monotonic()
            with ThreadPoolExecutor(max_workers=options.sessions) as workers:
                outcomes = list(workers.map(mining, range(options.sessions)))
            seconds = time.monotonic() - started
            status = clients[0].request("status")["result"]
            peak = None
            proc_status = Path(f"/proc/{process.pid}/status")
            if proc_status.exists():
                for line in proc_status.read_text().splitlines():
                    if line.startswith("VmHWM:"):
                        peak = int(line.split()[1]) * 1024
            accepted = sum(row[0] for row in outcomes)
            result = {"rpc": "synthetic fixture; not chain validation", "transport": "localhost plaintext regtest",
                      "sessions": options.sessions, "workers": options.workers, "queue": options.queue,
                      "seconds": seconds, "accepted": accepted, "accepted_per_second": accepted / seconds,
                      "overload_responses": sum(row[1] for row in outcomes), "uncredited_requests": sum(row[2] for row in outcomes),
                      "mean_hash_us": status["verification_mean_us"], "pool_peak_rss_bytes": peak,
                      "durability": "SQLite WAL/FULL", "candidate_blocks": status["block_candidates"]}
            result["request_limits"] = "raised only for this private verification-capacity fixture"
            assert status["accepted_shares"] == accepted and status["healthy"]
            print(json.dumps(result))
        finally:
            for client in clients:
                client.close()
            if process and process.poll() is None:
                process.terminate()
                assert process.wait(timeout=30) == 0
            log.close()
            fixture.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pool-binary", required=True)
    parser.add_argument("--workers", type=int, choices=range(1, 9), default=1)
    parser.add_argument("--queue", type=int, choices=range(1, 65), default=16)
    parser.add_argument("--sessions", type=int, choices=range(1, 9), default=4)
    parser.add_argument("--hashes", type=int, choices=range(1, 129), default=16, help="Attempts per session")
    parser.add_argument("--interval", type=float, default=0.0, help="Optional seconds between requests per session")
    args = parser.parse_args()
    if not 0 <= args.interval <= 10:
        parser.error("interval must be 0..10 seconds")
    run(args)
