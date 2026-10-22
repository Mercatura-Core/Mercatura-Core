#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Bounded, isolated native Qt mining with a localhost consensus-verifying peer.

Uses the real MiningPage controls and production hashing/backends. No simulated
GPU or consensus bypass exists here. Keep the resulting directory for review;
it contains a test-only regtest wallet. Never point this tool at existing data.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import signal
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def rpc(datadir, rpc_port, method, params=(), *, timeout=2):
    cookie = (datadir / "regtest" / ".cookie").read_text().strip()
    data = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": list(params)}).encode()
    request = urllib.request.Request(f"http://127.0.0.1:{rpc_port}/", data=data,
        headers={"Authorization": "Basic " + base64.b64encode(cookie.encode()).decode(), "Content-Type": "application/json"})
    # Explicitly bypass environment proxies for localhost RPC.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=timeout) as response:
        result = json.load(response)
    if result.get("error"):
        raise RuntimeError(result["error"]["message"])
    return result["result"]


def stop_owned(process):
    if process is None or process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=5)


def capture_threads(process, destination):
    """Best-effort diagnostics before terminating only our experimental child."""
    if process is None or process.poll() is not None:
        return
    with destination.open("w") as log:
        for task in sorted((Path("/proc") / str(process.pid) / "task").glob("*")):
            log.write(f"Thread {task.name}\n")
            for name in ("comm", "wchan", "stack"):
                try:
                    log.write(name + ": " + (task / name).read_text() + "\n")
                except OSError as error:
                    log.write(f"{name}: unavailable ({error})\n")
        log.flush()
        debugger = shutil.which("gdb")
        if debugger:
            try:
                subprocess.run([debugger, "-batch", "-ex", "set pagination off", "-ex", "thread apply all bt",
                                "-p", str(process.pid)], stdout=log, stderr=subprocess.STDOUT, timeout=15, check=False)
            except (OSError, subprocess.TimeoutExpired) as error:
                log.write(f"Debugger capture unavailable: {error}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--mode", choices=("cpu", "gpu", "combined"), required=True)
    parser.add_argument("--schedule", choices=("v1", "v2", "transition"), default="v2")
    parser.add_argument("--harder", action="store_true")
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--device", default="", help="Optional exact GPU ID, e.g. cuda:0; default all supported devices")
    parser.add_argument("--contexts", type=int, default=0, help="0 automatic; positive exact manual selection")
    parser.add_argument("--seconds", type=int, default=180, help="Minimum mining duration before Stop/restart/Close")
    parser.add_argument("--deadline", type=int, default=600, help="External total watchdog, including startup")
    parser.add_argument("--output-parent", type=Path, default=Path("work/hardware-qualification"))
    parser.add_argument("--offscreen", action="store_true")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Run this process-group watchdog under Linux or WSL")
    if args.harder and args.schedule != "v2":
        parser.error("--harder requires --schedule v2")
    if not 1 <= args.workers <= 2147483647 or not 0 <= args.contexts <= 2147483647:
        parser.error("Invalid worker/context count")
    if not 2 <= args.seconds <= 3600 or not args.seconds + 15 <= args.deadline <= 7200:
        parser.error("Use 2..3600 seconds and a deadline at least 15 seconds longer, at most 7200")
    binaries = args.build_dir.resolve() / "bin"
    for name in ("mercaturad", "test_mining_app-qt"):
        if not (binaries / name).is_file():
            parser.error(f"Build {name} first")
    args.output_parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=f"mercahash-{args.schedule}-{args.mode}-", dir=args.output_parent.resolve()))
    os.chmod(root, 0o700)
    print(f"Qualification directory: {root}", flush=True)
    metadata = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}
    metadata["status"] = "RUNNING"
    summary_path = root / "summary.json"
    summary_path.write_text(json.dumps(metadata, indent=2) + "\n")
    if args.mode != "cpu":
        checker = binaries / "mercatura-gpu-check"
        if not checker.is_file():
            parser.error("Build mercatura-gpu-check with CUDA/OpenCL enabled")
        try:
            with (root / "gpu-discovery.log").open("w") as log:
                discovery = subprocess.run([str(checker), "--list"], stdout=log, stderr=subprocess.STDOUT, timeout=30)
        except (OSError, subprocess.TimeoutExpired) as error:
            metadata.update(status="FAILED", reason=str(error))
            summary_path.write_text(json.dumps(metadata, indent=2) + "\n")
            print(str(error), file=sys.stderr)
            return 1
        if discovery.returncode == 77:
            metadata["status"] = "DEFERRED — NOT TESTED"
            metadata["reason"] = "No supported physical GPU was detected; no simulated device was substituted."
            summary_path.write_text(json.dumps(metadata, indent=2) + "\n")
            print(metadata["reason"], flush=True)
            return 77
        if discovery.returncode:
            metadata.update(status="FAILED", reason="GPU discovery failed; see gpu-discovery.log")
            summary_path.write_text(json.dumps(metadata, indent=2) + "\n")
            print(metadata["reason"], file=sys.stderr)
            return 1
    peer_dir, app_dir = root / "peer", root / "wallet"
    peer_dir.mkdir(); app_dir.mkdir()
    activation = {"v1": -1, "v2": 0, "transition": 20}[args.schedule]
    peer_rpc, app_rpc, p2p = port(), port(), port()
    while len({peer_rpc, app_rpc, p2p}) != 3:
        peer_rpc, app_rpc, p2p = port(), port(), port()
    common = ["-regtest", f"-mercahashv2height={activation}", "-listenonion=0", "-dnsseed=0",
              "-fixedseeds=0", "-discover=0", "-natpmp=0", "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1"]
    if args.harder:
        common.append("-regtestminingstress")
    env = dict(os.environ)
    # Isolate Qt and test scratch state as well as both chain datadirs.
    for name, subdir in (("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache"),
                         ("XDG_DATA_HOME", "data"), ("TMPDIR", "tmp")):
        location = root / subdir; location.mkdir(); env[name] = str(location)
    if args.offscreen:
        env["QT_QPA_PLATFORM"] = "offscreen"
    env.update(MERCATURA_TEST_DATADIR=str(app_dir), MERCATURA_TEST_WORKERS=str(args.workers),
        MERCATURA_TEST_ACTIVATION=str(activation), MERCATURA_TEST_MODE=args.mode,
        MERCATURA_TEST_SECONDS=str(args.seconds), MERCATURA_TEST_STOP_FIRST="1",
        MERCATURA_TEST_RECORDS=str(root / "qt-statistics.jsonl"), MERCATURA_TEST_RPC_PORT=str(app_rpc),
        MERCATURA_TEST_PEER_PORT=str(p2p), MERCATURA_TEST_GPU_CONTEXTS=str(args.contexts))
    env.pop("MERCATURA_TEST_RESTART", None)
    env.pop("MERCATURA_TEST_HARDER", None)
    env.pop("MERCATURA_TEST_GPU_DEVICE", None)
    if args.device:
        env["MERCATURA_TEST_GPU_DEVICE"] = args.device
    if args.harder:
        env["MERCATURA_TEST_HARDER"] = "1"
    peer = app = None
    began = time.monotonic()
    last_client_height = 0
    try:
        with (root / "peer.log").open("w") as peer_log, (root / "qt.log").open("w") as app_log, (root / "rpc-observation.jsonl").open("w") as observations:
            peer = subprocess.Popen([str(binaries / "mercaturad"), f"-datadir={peer_dir}", *common,
                "-connect=0", "-listen=1", f"-bind=127.0.0.1:{p2p}", f"-port={p2p}",
                f"-rpcport={peer_rpc}", "-server=1", "-disablewallet"], env=env, stdout=peer_log,
                stderr=subprocess.STDOUT, start_new_session=True)
            while True:
                if peer.poll() is not None:
                    raise RuntimeError("Local verification peer exited during startup")
                try:
                    rpc(peer_dir, peer_rpc, "getblockcount")
                    break
                except (OSError, RuntimeError, urllib.error.URLError):
                    if time.monotonic() - began > 45:
                        raise RuntimeError("Local peer startup timed out")
                    time.sleep(0.1)
            app = subprocess.Popen([str(binaries / "test_mining_app-qt")], env=env,
                stdout=app_log, stderr=subprocess.STDOUT, start_new_session=True)
            while app.poll() is None:
                elapsed = time.monotonic() - began
                if elapsed > args.deadline:
                    raise RuntimeError("External qualification deadline exceeded")
                records = root / "qt-statistics.jsonl"
                if records.exists() and records.stat().st_size and time.time() - records.stat().st_mtime > 10:
                    raise RuntimeError("Qt statistics heartbeat stopped for over 10 seconds")
                observation = {"elapsed_seconds": round(elapsed, 3)}
                for name, directory, rpc_port in (("client", app_dir, app_rpc), ("peer", peer_dir, peer_rpc)):
                    try:
                        info = rpc(directory, rpc_port, "getblockchaininfo")
                        observation[name] = {k: info[k] for k in ("blocks", "headers", "bestblockhash", "chainwork")}
                        if name == "client":
                            last_client_height = info["blocks"]
                    except (OSError, RuntimeError, urllib.error.URLError):
                        observation[name] = "starting or shutting down"
                observations.write(json.dumps(observation) + "\n"); observations.flush()
                time.sleep(0.5)
            if app.returncode:
                if app.returncode == 77:
                    metadata["status"] = "DEFERRED — NOT TESTED"
                    metadata["reason"] = "Requested physical CPU worker budget unavailable"
                    return 77
                raise RuntimeError(f"Native Qt qualification failed with exit code {app.returncode}")
            deadline = time.monotonic() + 20
            while rpc(peer_dir, peer_rpc, "getblockcount") < max(1, last_client_height):
                if time.monotonic() > deadline:
                    raise RuntimeError("Independent peer did not synchronize the mined chain")
                time.sleep(0.2)
            if not rpc(peer_dir, peer_rpc, "verifychain", (4, 0), timeout=min(120, max(2, args.deadline - (time.monotonic() - began)))):
                raise RuntimeError("Independent peer verification failed")
            metadata["peer_height"] = rpc(peer_dir, peer_rpc, "getblockcount")
            metadata["peer_verifychain"] = True
            results = [json.loads(line) for line in (root / "qt-statistics.jsonl").read_text().splitlines()]
            metadata["qt_result"] = next(row for row in reversed(results) if row["event"] == "result")
            metadata["status"] = "PASSED"
            rpc(peer_dir, peer_rpc, "stop")
            peer.wait(timeout=20)
            print(f"Passed; independent peer height {metadata['peer_height']}. Logs retained in {root}", flush=True)
            return 0
    except Exception as error:
        capture_threads(app, root / "watchdog-threads.log")
        metadata["status"] = "FAILED"
        metadata["reason"] = str(error)
        print(str(error), file=sys.stderr, flush=True)
        return 1
    finally:
        stop_owned(app)
        stop_owned(peer)
        metadata["elapsed_seconds"] = round(time.monotonic() - began, 3)
        summary_path.write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    sys.exit(main())
