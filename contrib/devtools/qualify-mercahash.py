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
    data = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method,
                       "params": params if isinstance(params, dict) else list(params)}).encode()
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


def process_metrics(process):
    """Sample the owned Linux process; virtual reservations are not RSS."""
    path = Path('/proc') / str(process.pid)
    values = {'pid': process.pid}
    for line in (path / 'status').read_text().splitlines():
        key, _, value = line.partition(':')
        if key in ('VmRSS', 'VmHWM', 'VmSize', 'Threads'):
            values[key] = int(value.split()[0])
    if 'VmRSS' not in values:
        raise ProcessLookupError('Owned process exited during resource sampling')
    values['file_descriptors'] = len(list((path / 'fd').iterdir()))
    fields = (path / 'stat').read_text().rsplit(')', 1)[1].split()
    values['cpu_seconds'] = (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')
    return values


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
    parser.add_argument("--pq", action="store_true", help="Require isolated regtest PQ transport on both nodes")
    parser.add_argument("--relay-transaction", action="store_true", help="Require a mature native-mining wallet payment to propagate over PQ and confirm")
    parser.add_argument("--memory-budget-mib", type=int, default=0, help="Optional aggregate Linux RSS watchdog; 0 preserves the original qualification")
    parser.add_argument("--validation-threads", type=int, default=0, help="0 preserves automatic script validation; 1..16 sets the same -par budget on both nodes")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Run this process-group watchdog under Linux or WSL")
    if args.harder and args.schedule != "v2":
        parser.error("--harder requires --schedule v2")
    if args.relay_transaction and (not args.pq or args.seconds < 60):
        parser.error("--relay-transaction requires --pq and at least 60 seconds for native coinbase maturity")
    if not 0 <= args.memory_budget_mib <= 8192 or (args.memory_budget_mib and not Path('/proc/self/status').is_file()):
        parser.error("Use a 0..8192MiB RSS budget; nonzero budgets require Linux /proc")
    if not 0 <= args.validation_threads <= 16:
        parser.error("Use automatic validation threads (0) or an explicit 1..16 thread budget")
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
    peer_dir.mkdir()
    app_dir.mkdir()
    if args.validation_threads:
        # The Qt test reads its fresh datadir configuration before node init.
        # Match the verifier's normal -par setting without changing consensus.
        (app_dir / 'mercatura.conf').write_text(f'par={args.validation_threads}\n')
    activation = {"v1": -1, "v2": 0, "transition": 20}[args.schedule]
    peer_rpc, app_rpc, p2p = port(), port(), port()
    while len({peer_rpc, app_rpc, p2p}) != 3:
        peer_rpc, app_rpc, p2p = port(), port(), port()
    common = ["-regtest", f"-mercahashv2height={activation}", "-listenonion=0", "-dnsseed=0",
              "-fixedseeds=0", "-discover=0", "-natpmp=0", "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1"]
    if args.validation_threads:
        common.append(f'-par={args.validation_threads}')
    if args.pq:
        common.append("-pqp2p=required")
    if args.harder:
        common.append("-regtestminingstress")
    env = dict(os.environ)
    # Isolate Qt and test scratch state as well as both chain datadirs.
    for name, subdir in (("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache"),
                         ("XDG_DATA_HOME", "data"), ("TMPDIR", "tmp")):
        location = root / subdir
        location.mkdir()
        env[name] = str(location)
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
    env.pop("MERCATURA_TEST_PQ", None)
    if args.pq:
        env["MERCATURA_TEST_PQ"] = "1"
    if args.device:
        env["MERCATURA_TEST_GPU_DEVICE"] = args.device
    if args.harder:
        env["MERCATURA_TEST_HARDER"] = "1"
    peer = app = restarted_client = None
    began = time.monotonic()
    last_client_height = 0
    last_client_hash = None
    pq_observed = set()
    relay_txid = relay_block = None
    reconnect_began = old_mining_session = None
    resource_samples = []
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
                if args.memory_budget_mib:
                    measurements = []
                    for process in (peer, app):
                        if process.poll() is None:
                            try:
                                measurements.append(process_metrics(process))
                            except (FileNotFoundError, ProcessLookupError):
                                if process.poll() is None:
                                    raise
                    resource_samples.append(measurements)
                    observation['process_resources'] = measurements
                    if sum(item['VmRSS'] for item in measurements) > args.memory_budget_mib * 1024:
                        raise RuntimeError("Aggregate native qualification RSS budget exceeded")
                    if any(item['Threads'] > 256 or item['file_descriptors'] > 512 for item in measurements):
                        raise RuntimeError("Native qualification thread/descriptor budget exceeded")
                for name, directory, rpc_port in (("client", app_dir, app_rpc), ("peer", peer_dir, peer_rpc)):
                    try:
                        info = rpc(directory, rpc_port, "getblockchaininfo")
                        observation[name] = {k: info[k] for k in ("blocks", "headers", "bestblockhash", "chainwork")}
                        if args.pq:
                            peers = rpc(directory, rpc_port, "getpeerinfo")
                            established = [peer for peer in peers if peer.get("session_id")]
                            for peer_info in established:
                                if not (peer_info['transport_protocol_type'] == 'pq-v1' and
                                        peer_info['pq_encrypted'] and peer_info['pq_authenticated']):
                                    raise ValueError("Mining peer lacks authenticated PQ encryption")
                            if established:
                                pq_observed.add(name)
                                observation[name]['pq_sessions'] = [peer['session_id'] for peer in established]
                        if name == "client":
                            last_client_height = info["blocks"]
                            last_client_hash = info["bestblockhash"]
                    except (OSError, RuntimeError, urllib.error.URLError):
                        observation[name] = "starting or shutting down"
                observations.write(json.dumps(observation) + "\n")
                observations.flush()
                current_sessions = [set(observation[name].get('pq_sessions', [])) if isinstance(observation.get(name), dict) else set()
                                    for name in ('client', 'peer')]
                if args.relay_transaction and current_sessions[0] & current_sessions[1]:
                    if relay_txid is None and isinstance(observation.get('client'), dict) and last_client_height >= 101:
                        # The Qt test owns exactly one wallet. Funding comes
                        # from its native mined blocks, not RPC generation.
                        destination = rpc(app_dir, app_rpc, 'getnewaddress')
                        if not destination.startswith('mcrt1z'):
                            raise RuntimeError("Mining payment destination is not native PQ")
                        relay_txid = rpc(app_dir, app_rpc, 'sendtoaddress',
                                         {'address': destination, 'amount': 1, 'fee_rate': 1})
                        metadata['relay_transaction_id'] = relay_txid
                        metadata['relay_pq_session'] = sorted(current_sessions[0] & current_sessions[1])[0]
                    if relay_txid is not None and relay_block is None:
                        payment = rpc(app_dir, app_rpc, 'gettransaction', (relay_txid,))
                        if payment.get('confirmations', 0) > 0:
                            candidate = payment['blockhash']
                            # A peer block at the same hash must contain the
                            # exact transaction, even if the mempool interval
                            # was shorter than the sampling interval.
                            if rpc(peer_dir, peer_rpc, 'getblockcount') >= payment['blockheight']:
                                block = rpc(peer_dir, peer_rpc, 'getblock', (candidate, 1))
                                if relay_txid not in block['tx']:
                                    raise RuntimeError("Mining peer block omitted the payment")
                                relay_block = candidate
                                metadata['relay_confirmed_block'] = candidate
                    if relay_block is not None and not metadata.get('reconnected_while_mining'):
                        qt_samples = [json.loads(line) for line in records.read_text().splitlines()]
                        mining_sample = next(row for row in reversed(qt_samples) if row['event'] == 'sample')
                        if mining_sample['busy'] and int(mining_sample['cpu_hashes']) > 0:
                            if reconnect_began is None:
                                old_mining_session = sorted(current_sessions[0] & current_sessions[1])[0]
                                endpoint = next(p for p in rpc(peer_dir, peer_rpc, 'getpeerinfo')
                                                if p.get('session_id') == old_mining_session)
                                reconnect_began = time.monotonic()
                                rpc(peer_dir, peer_rpc, 'disconnectnode', {'nodeid': endpoint['id']})
                                metadata['mining_reconnect_start_sample'] = mining_sample
                            elif fresh_sessions := (current_sessions[0] & current_sessions[1]) - {old_mining_session}:
                                metadata['reconnected_while_mining'] = True
                                metadata['mining_reconnect_seconds'] = time.monotonic() - reconnect_began
                                metadata['mining_reconnect_session'] = sorted(fresh_sessions)[0]
                                metadata['mining_reconnect_end_sample'] = mining_sample
                time.sleep(0.5)
            if app.returncode:
                if app.returncode == 77:
                    metadata["status"] = "DEFERRED — NOT TESTED"
                    metadata["reason"] = "Requested physical CPU worker budget unavailable"
                    return 77
                raise RuntimeError(f"Native Qt qualification failed with exit code {app.returncode}")
            if args.pq and pq_observed != {"client", "peer"}:
                raise RuntimeError("Did not observe authenticated PQ transport on both mining nodes")
            if args.relay_transaction and relay_block is None:
                raise RuntimeError("Native-funded PQ payment did not confirm on the independent peer")
            if args.relay_transaction and not metadata.get('reconnected_while_mining'):
                raise RuntimeError("Did not qualify a fresh matching PQ session while native CPU mining remained active")
            metadata["pq_observed_endpoints"] = sorted(pq_observed)
            if args.pq:
                # Close deliberately stops the producer while mining. A peer
                # may still need blocks which were not sent before disconnect.
                # Reopen the retained chain without a miner and qualify normal
                # PQ reconnect/recovery, rather than assuming relay has drained.
                with (root / "client-restart.log").open("w") as restart_log:
                    restarted_client = subprocess.Popen([str(binaries / "mercaturad"),
                        f"-datadir={app_dir}", *common, f"-connect=127.0.0.1:{p2p}",
                        "-listen=0", f"-rpcport={app_rpc}", "-server=1", "-disablewallet"],
                        env=env, stdout=restart_log, stderr=subprocess.STDOUT, start_new_session=True)
                startup_deadline = min(began + args.deadline, time.monotonic() + 45)
                while True:
                    if restarted_client.poll() is not None:
                        raise RuntimeError("Native mining chain failed to reopen after Qt shutdown")
                    try:
                        info = rpc(app_dir, app_rpc, "getblockchaininfo")
                        last_client_height = info["blocks"]
                        last_client_hash = info["bestblockhash"]
                        break
                    except (OSError, RuntimeError, urllib.error.URLError):
                        if time.monotonic() > startup_deadline:
                            raise RuntimeError("Native mining chain restart timed out")
                        time.sleep(0.1)
                metadata["mining_node_restarted"] = True
                handshake_deadline = min(began + args.deadline, time.monotonic() + 10)
                while True:
                    endpoints = [rpc(directory, rpc_port, "getpeerinfo") for directory, rpc_port in
                                 ((app_dir, app_rpc), (peer_dir, peer_rpc))]
                    sessions = [{p['session_id'] for p in peers if p.get('pq_encrypted') and
                                 p.get('pq_authenticated') and p['transport_protocol_type'] == 'pq-v1'}
                                for peers in endpoints]
                    if sessions[0] & sessions[1]:
                        metadata["restart_pq_session"] = sorted(sessions[0] & sessions[1])[0]
                        break
                    if time.monotonic() > handshake_deadline:
                        raise RuntimeError("Restarted mining node did not establish matching PQ sessions")
                    time.sleep(0.1)
            deadline = min(began + args.deadline, time.monotonic() + (60 if args.pq else 20))
            while rpc(peer_dir, peer_rpc, "getblockcount") < max(1, last_client_height):
                if time.monotonic() > deadline:
                    raise RuntimeError("Independent peer did not synchronize the mined chain")
                time.sleep(0.2)
            if args.pq and last_client_height < 1:
                raise RuntimeError("Did not observe a mined block through the native client's RPC")
            if last_client_height:
                if rpc(peer_dir, peer_rpc, "getblockhash", (last_client_height,)) != last_client_hash:
                    raise RuntimeError("Independent peer disagrees with the observed native mining chain")
                metadata["observed_mining_height"] = last_client_height
                metadata["observed_mining_hash"] = last_client_hash
                metadata["peer_prefix_matches"] = True
            if args.pq:
                peer_info = rpc(peer_dir, peer_rpc, "getblockchaininfo")
                if (peer_info["blocks"] != last_client_height or peer_info["headers"] != last_client_height or
                        peer_info["bestblockhash"] != last_client_hash):
                    raise RuntimeError("Restarted mining nodes disagree on their exact block/header tip")
                if not rpc(app_dir, app_rpc, "verifychain", (4, 0), timeout=min(120, max(2, args.deadline - (time.monotonic() - began)))):
                    raise RuntimeError("Restarted mining node verification failed")
                metadata["restart_tip_matches"] = True
                metadata["restarted_client_verifychain"] = True
            if not rpc(peer_dir, peer_rpc, "verifychain", (4, 0), timeout=min(120, max(2, args.deadline - (time.monotonic() - began)))):
                raise RuntimeError("Independent peer verification failed")
            metadata["peer_height"] = rpc(peer_dir, peer_rpc, "getblockcount")
            metadata["peer_verifychain"] = True
            results = [json.loads(line) for line in (root / "qt-statistics.jsonl").read_text().splitlines()]
            metadata["qt_result"] = next(row for row in reversed(results) if row["event"] == "result")
            metadata["status"] = "PASSED"
            if restarted_client is not None:
                rpc(app_dir, app_rpc, "stop")
                restarted_client.wait(timeout=20)
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
        stop_owned(restarted_client)
        stop_owned(app)
        stop_owned(peer)
        metadata["elapsed_seconds"] = round(time.monotonic() - began, 3)
        metadata['sampling_interval_seconds'] = 0.5
        if resource_samples:
            measurements = [item for sample in resource_samples for item in sample]
            metadata['resource_summary'] = {
                'samples': len(resource_samples),
                'aggregate_peak_sampled_rss_kib': max(sum(item['VmRSS'] for item in sample) for sample in resource_samples),
                'peak_threads_per_process': max(item['Threads'] for item in measurements),
                'peak_fds_per_process': max(item['file_descriptors'] for item in measurements),
                'per_pid': {str(pid): {'peak_rss_kib': max(item['VmRSS'] for item in measurements if item['pid'] == pid),
                                     'peak_virtual_kib': max(item['VmSize'] for item in measurements if item['pid'] == pid),
                                     'observed_cpu_seconds': max(item['cpu_seconds'] for item in measurements if item['pid'] == pid) -
                                                             min(item['cpu_seconds'] for item in measurements if item['pid'] == pid)}
                            for pid in {item['pid'] for item in measurements}}}
        metadata['all_children_exited'] = all(process is None or process.poll() is not None for process in (peer, app, restarted_client))
        summary_path.write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    sys.exit(main())
