#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Optional isolated Linux node diagnostic: BIP324/PQ latency, RSS and RPC load.

Not a normal-operation cadence test, native MercaHash V2 qualification, or
production endurance benchmark. Uses new harness datadirs and loopback peers.
Run with --configfile=<build>/test/config.ini and --nocleanup to retain JSON.
"""
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import platform
import statistics
import sys
import threading
import signal
import subprocess
from decimal import Decimal
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/functional'))
# Direct execution outside test/functional requires the source-tree bootstrap.
from test_framework.test_framework import BitcoinTestFramework  # noqa: E402
from test_framework.util import assert_equal, get_rpc_proxy, p2p_port  # noqa: E402


def process_metrics(node):
    process = node.process
    if process is None:
        raise ProcessLookupError('Node process changed during collection')
    pid = process.pid
    values = {}
    for line in Path(f'/proc/{pid}/status').read_text().splitlines():
        key, _, value = line.partition(':')
        if key in ['VmRSS', 'VmHWM', 'VmSize']:
            values[key + '_kib'] = int(value.split()[0])
    stat = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    values['threads'] = int(next(line.split()[1] for line in Path(f'/proc/{pid}/status').read_text().splitlines() if line.startswith('Threads:')))
    descriptors = list(Path(f'/proc/{pid}/fd').iterdir())
    values['file_descriptors'] = len(descriptors)
    values['sockets'] = 0
    for item in descriptors:
        try:
            values['sockets'] += os.readlink(item).startswith('socket:')
        except FileNotFoundError:
            pass  # A descriptor can close between listing and reading its link.
    values['pid'] = pid
    try:
        for line in Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines():
            key, _, value = line.partition(':')
            if key in ['Rss', 'Pss', 'Private_Clean', 'Private_Dirty']:
                values['smaps_' + key + '_kib'] = int(value.split()[0])
    except PermissionError:
        values['smaps_rollup_available'] = False
    values['cpu_seconds'] = (int(stat[11]) + int(stat[12])) / os.sysconf('SC_CLK_TCK')
    return values


class PQNodeBenchmark(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument('--endurance-seconds', type=int, default=0, help='0 preserves the original diagnostic; otherwise bounded 60..10800s workload')
        parser.add_argument('--node-count', type=int, choices=(2, 3, 4), default=2)
        parser.add_argument('--sample-seconds', type=float, default=1.0)
        parser.add_argument('--memory-budget-mib', type=int, default=3072)
        parser.add_argument('--persistent', action='store_true')
        parser.add_argument('--activation', type=int, choices=(0, 20), default=0)
        parser.add_argument('--restart-seconds', type=int, default=300)

    def set_test_params(self):
        if self.options.endurance_seconds:
            if not 60 <= self.options.endurance_seconds <= 10800 or not 1 <= self.options.sample_seconds <= 10:
                raise ValueError('Use 60..10800s and a 1..10s collection interval')
            if not 1 <= self.options.memory_budget_mib <= 4096 or self.options.restart_seconds < 60:
                raise ValueError('Use a 1..4096MiB aggregate budget and restart interval >=60s')
            self.num_nodes = self.options.node_count
            self.setup_clean_chain = True
            self.uses_wallet = True
            # The framework writes exact per-node loopback bind entries. A
            # second -bind argument would duplicate that multi-value option.
            self.bind_to_localhost_only = True
            args = ['-connect=0', '-dnsseed=0', '-fixedseeds=0', '-discover=0', '-natpmp=0',
                    '-maxconnections=16', '-dbcache=64', '-maxmempool=16',
                    '-pqp2p=required', f'-mercahashv2height={self.options.activation}']
            if self.options.persistent:
                args += ['-pqp2pidentity=b2_identity.dat', '-pqp2pidentitycreate=1']
            self.extra_args = [list(args) for _ in range(self.num_nodes)]
            return
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.uses_wallet = True
        isolated = ['-connect=0', '-dnsseed=0', '-fixedseeds=0', '-discover=0', '-natpmp=0', '-v2transport=1']
        self.extra_args = [isolated, isolated, isolated + ['-pqp2p=required'], isolated + ['-pqp2p=required']]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        if platform.system() != 'Linux':
            raise RuntimeError('This optional /proc diagnostic is Linux-only')

    def setup_network(self):
        self.setup_nodes()

    def endurance(self):
        """Bounded operational workload; no malformed traffic or public peers."""
        output = Path(self.options.tmpdir)
        samples, events, latencies, health_checks = [], [], [], []
        connection_observations = []
        report = {'status': 'RUNNING', 'node_count': self.num_nodes,
                  'duration_requested_seconds': self.options.endurance_seconds,
                  'sampling_seconds': self.options.sample_seconds,
                  'aggregate_memory_budget_mib': self.options.memory_budget_mib,
                  'max_peers_per_node': 16, 'max_topology_endpoints': 2 * self.num_nodes,
                  'crypto_workers_per_node': 2, 'native_mining_workers': 0,
                  'workload': 'valid PQ transactions/blocks, ping, paced reconnect, planned restart and isolated most-work recovery',
                  'mining': 'RPC generation; native CPU mining is separately qualified by qualify-mercahash.py',
                  'platform': platform.platform(), 'activation': self.options.activation,
                  'persistent_identity': self.options.persistent,
                  'start_epoch_unix': time.time(),
                  'initial_pids': [node.process.pid for node in self.nodes],
                  'limitations': ['Sampled process RSS includes chain/wallet/cache growth; not a PQ-only allocation measurement',
                                  'Worker/queue accounting is measured separately by the test-only probe and barrier tests',
                                  'Same-host loopback; cloud results do not predict Ryzen 5 4600G performance']}
        for filename, key in [('/sys/fs/cgroup/cpu.max', 'cgroup_cpu_max'),
                              ('/sys/fs/cgroup/memory.max', 'cgroup_memory_max')]:
            if Path(filename).exists():
                report[key] = Path(filename).read_text().strip()
        stop, critical = threading.Event(), threading.Event()
        planned_down = set()
        phase = ['warmup']
        failures = []
        started = time.monotonic()

        def collect():
            with (output / 'pq-b2-resource-samples.jsonl').open('w') as stream:
                while not stop.is_set():
                    row = {'elapsed_seconds': time.monotonic() - started, 'phase': phase[0], 'nodes': []}
                    try:
                        for index, node in enumerate(self.nodes):
                            if index in planned_down:
                                row['nodes'].append(None)
                                continue
                            if node.process is None or node.process.poll() is not None:
                                raise RuntimeError(f'Unexpected node exit: {index}')
                            row['nodes'].append(process_metrics(node))
                        live = [item for item in row['nodes'] if item]
                        for filename, key in [('/sys/fs/cgroup/memory.current', 'cgroup_memory_current_bytes'),
                                              ('/sys/fs/cgroup/cpu.stat', 'cgroup_cpu_stat')]:
                            if Path(filename).exists():
                                row[key] = Path(filename).read_text().strip()
                        rss = sum(item['VmRSS_kib'] for item in live)
                        if rss > self.options.memory_budget_mib * 1024:
                            raise RuntimeError('Aggregate RSS budget exceeded')
                        if any(item['file_descriptors'] > 256 or item['threads'] > 96 for item in live):
                            raise RuntimeError('Descriptor/thread budget exceeded')
                        if row['elapsed_seconds'] > self.options.endurance_seconds + 1200:
                            raise RuntimeError('Overall watchdog exceeded workload plus startup/validation allowance')
                        samples.append(row)
                        stream.write(json.dumps(row) + '\n')
                        stream.flush()
                    except Exception as error:
                        # A planned restart may race a /proc read; retry only
                        # that expected lifecycle, never hide a live-node fault.
                        if isinstance(error, (FileNotFoundError, ProcessLookupError)) and index in planned_down:
                            stop.wait(self.options.sample_seconds)
                            continue
                        failures.append(str(error))
                        critical.set()
                        for node in self.nodes:
                            if node.process is not None and node.process.poll() is None:
                                node.process.send_signal(signal.SIGTERM)
                        return
                    stop.wait(self.options.sample_seconds)

        sampler = threading.Thread(target=collect, name='pq-b2-process-sampler')
        sampler.start()
        edges = [(index, index - 1) for index in range(1, self.num_nodes)]
        persistent_fingerprints = {}
        if self.num_nodes > 2:
            edges.append((0, self.num_nodes - 1))

        def connect():
            for source, target in edges:
                address = f'127.0.0.1:{p2p_port(target)}'
                if not any(not peer['inbound'] and peer['addr'] == address for peer in self.nodes[source].getpeerinfo()):
                    before = time.monotonic()
                    self.connect_nodes(source, target)
                    measured = 1000 * (time.monotonic() - before)
                    latencies.append(measured)
                    connection_observations.append({'elapsed_seconds': time.monotonic() - started,
                                                    'source': source, 'target': target, 'milliseconds': measured})

        def health():
            assert not critical.is_set(), failures
            assert sampler.is_alive(), 'Resource sampler stopped unexpectedly'
            before = time.monotonic()
            snapshots = [node.getpeerinfo() for node in self.nodes]
            assert sum(map(len, snapshots)) <= 2 * len(edges)
            for peers in snapshots:
                assert peers
                for peer in peers:
                    assert_equal(peer['transport_protocol_type'], 'pq-v1')
                    assert peer['pq_authenticated'] and peer['pq_encrypted']
                    assert not peer['pq_identity_verified']  # This fixture configures no trust pins.
                    assert peer['addr'].startswith('127.0.0.1:')
            for source, target in edges:
                peer = next(peer for peer in snapshots[source]
                            if not peer['inbound'] and peer['addr'] == f'127.0.0.1:{p2p_port(target)}')
                reverse = next(other for other in snapshots[target] if other['inbound'] and other['session_id'] == peer['session_id'])
                if persistent_fingerprints:
                    assert_equal(peer['pq_identity'], persistent_fingerprints[target])
                    assert_equal(reverse['pq_identity'], persistent_fingerprints[source])
            health_checks.append({'elapsed_seconds': time.monotonic() - started,
                                  'rpc_ms': 1000 * (time.monotonic() - before),
                                  'active_endpoints': sum(map(len, snapshots)),
                                  'observed_pending': sum(not peer['pq_authenticated'] for peers in snapshots for peer in peers)})
            return snapshots

        try:
            # Creation is explicit only at initial startup, never on restart.
            self.extra_args = [[arg for arg in args if arg != '-pqp2pidentitycreate=1'] for args in self.extra_args]
            phase[0] = 'idle_disconnected'
            stop.wait(10)
            connect()
            initial_peers = health()
            if self.options.persistent:
                for source, target in edges:
                    peer = next(peer for peer in initial_peers[source]
                                if not peer['inbound'] and peer['addr'] == f'127.0.0.1:{p2p_port(target)}')
                    reverse = next(other for other in initial_peers[target] if other['inbound'] and other['session_id'] == peer['session_id'])
                    persistent_fingerprints[target] = peer['pq_identity']
                    persistent_fingerprints[source] = reverse['pq_identity']
            phase[0] = 'idle_connected'
            stop.wait(10)
            phase[0] = 'warmup'
            for node in self.nodes:
                node.createwallet('b2')
            sender = self.nodes[0].get_wallet_rpc('b2')
            address = sender.getnewaddress()
            assert address.startswith('mcrt1z')
            for offset in range(0, 101, 5):
                self.generatetoaddress(self.nodes[0], min(5, 101 - offset), address, sync_fun=self.no_op)
            self.sync_blocks()
            begun = time.monotonic()
            deadline = begun + self.options.endurance_seconds
            phase[0] = 'steady'
            next_work, next_churn = begun, begun + 30
            next_restart = begun + self.options.restart_seconds
            next_progress = begun
            partition_done = False
            txids, blocks = [], []
            while time.monotonic() < deadline:
                now = time.monotonic()
                health()
                for node in self.nodes:
                    node.ping()
                if now >= next_work:
                    receiver = self.nodes[1].get_wallet_rpc('b2')
                    destination = receiver.getnewaddress()
                    assert destination.startswith('mcrt1z')
                    txid = sender.sendtoaddress(destination, Decimal('1.00'))
                    self.wait_until(lambda: all(txid in node.getrawmempool() for node in self.nodes))
                    block = self.generatetoaddress(self.nodes[0], 1, address, sync_fun=self.no_op)[0]
                    self.sync_blocks()
                    assert all(node.getbestblockhash() == block for node in self.nodes)
                    txids.append(txid)
                    blocks.append(block)
                    next_work = time.monotonic() + 20
                if now >= next_churn:
                    old = self.nodes[1].getpeerinfo()
                    old = next(peer for peer in old if not peer['inbound'] and peer['addr'] == f'127.0.0.1:{p2p_port(0)}')
                    self.disconnect_nodes(1, 0)
                    connect()
                    fresh = next(peer for peer in self.nodes[1].getpeerinfo()
                                 if not peer['inbound'] and peer['addr'] == f'127.0.0.1:{p2p_port(0)}')
                    assert fresh['session_id'] != old['session_id']
                    assert (fresh['pq_identity'] == old['pq_identity']) == self.options.persistent
                    events.append({'event': 'reconnect', 'elapsed_seconds': time.monotonic() - begun})
                    next_churn = time.monotonic() + 30
                if now >= next_restart:
                    index = self.num_nodes - 1
                    old_sessions = {peer['session_id'] for peer in self.nodes[index].getpeerinfo()}
                    old_peer_ids = {other: {peer['id'] for peer in node.getpeerinfo() if peer.get('session_id') in old_sessions}
                                    for other, node in enumerate(self.nodes) if other != index}
                    planned_down.add(index)
                    phase[0] = 'planned_restart'
                    self.restart_node(index, self.extra_args[index])
                    if 'b2' not in self.nodes[index].listwallets():
                        self.nodes[index].loadwallet('b2')
                    planned_down.remove(index)
                    self.wait_until(lambda: all(peer['id'] not in old_peer_ids[other]
                                               for other, node in enumerate(self.nodes) if other != index
                                               for peer in node.getpeerinfo()))
                    connect()
                    self.sync_blocks()
                    health()
                    events.append({'event': 'node_restart', 'node': index, 'elapsed_seconds': time.monotonic() - begun})
                    phase[0] = 'steady'
                    next_restart = time.monotonic() + self.options.restart_seconds
                if not partition_done and self.num_nodes > 2 and now - begun >= self.options.endurance_seconds / 2:
                    phase[0] = 'isolated_recovery'
                    isolated = self.num_nodes - 1
                    for source, target in edges:
                        if isolated in (source, target):
                            self.disconnect_nodes(source, target)
                    shorter = self.generatetoaddress(self.nodes[isolated], 1, address, sync_fun=self.no_op)[0]
                    longer = self.generatetoaddress(self.nodes[0], 2, address, sync_fun=self.no_op)[-1]
                    self.sync_blocks(self.nodes[:-1])
                    connect()
                    self.sync_blocks()
                    assert all(node.getbestblockhash() == longer for node in self.nodes)
                    events.append({'event': 'most_work_recovery', 'shorter_tip': shorter, 'selected_tip': longer})
                    phase[0] = 'steady'
                    partition_done = True
                if now >= next_progress:
                    self.log.info('B2 steady %.0f/%.0fs: %d nodes, %d relayed transactions/blocks, %d lifecycle events',
                                  time.monotonic() - begun, self.options.endurance_seconds, self.num_nodes, len(txids), len(events))
                    next_progress = now + 60
                stop.wait(1)
            report['workload_elapsed_seconds'] = time.monotonic() - begun
            phase[0] = 'final_validation'
            self.sync_blocks()
            verification = []
            for node in self.nodes:
                rpc = get_rpc_proxy(node.url, node.index, timeout=180, coveragedir=node.coverage_dir)
                before = time.monotonic()
                assert rpc.verifychain(4, 0)
                verification.append(time.monotonic() - before)
            final_peers = health()
            report.update(status='PASSED', qualified_seconds=time.monotonic() - begun,
                          final_height=self.nodes[0].getblockcount(), final_tip=self.nodes[0].getbestblockhash(),
                          txids=txids, relayed_blocks=blocks, events=events, verifychain_seconds=verification,
                          initial_endpoint_count=sum(map(len, initial_peers)), final_endpoint_count=sum(map(len, final_peers)),
                          connection_ms=latencies)
            report['connection_observations'] = connection_observations
        finally:
            stop.set()
            sampler.join(timeout=15)
            if critical.is_set() or sampler.is_alive() or report['status'] != 'PASSED':
                report['status'] = 'FAILED'
            report['resource_failures'] = failures
            report['total_observed_seconds'] = time.monotonic() - started
            report['sample_count'] = len(samples)
            report['process_summary'] = []
            for index in range(self.num_nodes):
                rows = [row['nodes'][index] for row in samples if row['nodes'][index]]
                steady = [row['nodes'][index] for row in samples if row['phase'] == 'steady' and row['nodes'][index]]
                disconnected = [row['nodes'][index] for row in samples if row['phase'] == 'idle_disconnected' and row['nodes'][index]]
                connected = [row['nodes'][index] for row in samples if row['phase'] == 'idle_connected' and row['nodes'][index]]
                if not rows:
                    continue
                windows = [steady[n * len(steady) // 4:(n + 1) * len(steady) // 4] for n in range(4)]
                cpu_by_pid = {}
                for row in rows:
                    cpu_by_pid.setdefault(row['pid'], []).append(row['cpu_seconds'])
                cpu_seconds = sum(max(values) - min(values) for values in cpu_by_pid.values())
                report['process_summary'].append({
                    'node': index, 'peak_rss_kib': max(row['VmRSS_kib'] for row in rows),
                    'idle_disconnected_median_rss_kib': statistics.median(row['VmRSS_kib'] for row in disconnected) if disconnected else None,
                    'idle_connected_median_rss_kib': statistics.median(row['VmRSS_kib'] for row in connected) if connected else None,
                    'initial_rss_kib': rows[0]['VmRSS_kib'], 'final_pre_shutdown_rss_kib': rows[-1]['VmRSS_kib'],
                    'kernel_hwm_kib': max(row['VmHWM_kib'] for row in rows),
                    'peak_virtual_kib': max(row['VmSize_kib'] for row in rows),
                    'peak_threads': max(row['threads'] for row in rows),
                    'peak_fds': max(row['file_descriptors'] for row in rows),
                    'peak_sockets': max(row['sockets'] for row in rows),
                    'observed_cpu_seconds': cpu_seconds,
                    'average_cpu_percent_one_core': 100 * cpu_seconds / report['total_observed_seconds'],
                    'steady_window_median_rss_kib': [statistics.median(row['VmRSS_kib'] for row in window) if window else None for window in windows],
                    'distinct_pids': len({row['pid'] for row in rows})})
            if latencies:
                report['connection_latency_summary'] = {'samples': len(latencies), 'median_ms': statistics.median(latencies),
                                                        'max_ms': max(latencies),
                                                        'p95_ms': sorted(latencies)[(95 * len(latencies) + 99) // 100 - 1] if len(latencies) >= 20 else None}
            if health_checks:
                rpc_times = sorted(check['rpc_ms'] for check in health_checks)
                report['health_summary'] = {'samples': len(health_checks), 'rpc_batch_median_ms': statistics.median(rpc_times),
                                            'rpc_batch_max_ms': max(rpc_times),
                                            'max_active_endpoints': max(check['active_endpoints'] for check in health_checks),
                                            'max_pending_observed_after_establishment': max(check['observed_pending'] for check in health_checks)}
            report['logged_pq_rejections'] = []
            for node in self.nodes:
                with (node.chain_path / 'debug.log').open(errors='replace') as stream:
                    report['logged_pq_rejections'].append(sum('Experimental PQ transport rejected:' in line for line in stream))
            try:
                report['final_pids_before_shutdown'] = [node.process.pid if node.process is not None else None for node in self.nodes]
                self.stop_nodes()
                report['clean_shutdown'] = all(node.process is None for node in self.nodes)
                if not report['clean_shutdown']:
                    report['status'] = 'FAILED'
            except Exception as error:
                report['status'] = 'FAILED'
                report['shutdown_error_type'] = type(error).__name__
                for node in self.nodes:
                    process = node.process
                    if process is not None and process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
            report['all_children_exited'] = all(node.process is None or node.process.poll() is not None for node in self.nodes)
            if not report['all_children_exited']:
                report['status'] = 'FAILED'
            (output / 'pq-b2-endurance.json').write_text(json.dumps(report, indent=2) + '\n')
        assert_equal(report['status'], 'PASSED')

    def run_test(self):
        if self.options.endurance_seconds:
            return self.endurance()
        report = {'platform': platform.platform(), 'method': '4 isolated loopback regtest nodes; 4 connection samples/mode including RPC, TCP, transport, version and pong; 50 idle RPC samples; 4 RPC-generated regtest blocks as optional load diagnostic',
                  'limitations': ['Small local diagnostic; not a statistical capacity or endurance qualification', 'RPC block generation is not MercaHash V2 native CPU/GPU branch qualification', 'No realistic multi-host latency, public discovery, partitions or lower-power host qualification']}
        for mode, source, target in [('bip324', 1, 0), ('pq', 3, 2)]:
            nodes = [self.nodes[target], self.nodes[source]]
            before = [process_metrics(node) for node in nodes]
            wall_start = time.monotonic()
            latencies, handshake_bytes = [], []
            for sample in range(4):
                if sample:
                    self.disconnect_nodes(source, target)
                    time.sleep(1.05)  # Same pacing in both modes; respects per-source admission.
                start = time.perf_counter()
                self.connect_nodes(source, target, peer_advertises_v2=(mode == 'bip324'))
                latencies.append(1000 * (time.perf_counter() - start))
                peers = [node.getpeerinfo()[0] for node in nodes]
                for peer in peers:
                    assert_equal(peer['transport_protocol_type'], 'pq-v1' if mode == 'pq' else 'v2')
                    assert_equal(peer['pq_encrypted'], mode == 'pq')
                    assert_equal(peer['pq_authenticated'], mode == 'pq')
                    assert_equal(peer['pq_identity_verified'], False)
                handshake_bytes.append(sum(peer['bytesrecv'] - sum(peer['bytesrecv_per_msg'].values()) for peer in peers))
                if mode == 'pq':
                    assert_equal(handshake_bytes[-1], 15488)
            cpu_after = [process_metrics(node) for node in nodes]
            row = {'connection_ms': latencies, 'connection_median_ms': statistics.median(latencies), 'handshake_bytes': handshake_bytes,
                   'node_metrics_after_connections': cpu_after,
                   'combined_cpu_seconds_for_connections': sum(after['cpu_seconds'] - prior['cpu_seconds'] for after, prior in zip(cpu_after, before)),
                   'connection_window_seconds_including_pacing': time.monotonic() - wall_start}
            idle = []
            for _ in range(50):
                start = time.perf_counter()
                nodes[0].getpeerinfo()
                idle.append(1000 * (time.perf_counter() - start))
            row['idle_getpeerinfo_median_ms'] = statistics.median(idle)
            row['idle_getpeerinfo_max_ms'] = max(idle)
            nodes[0].createwallet('benchmark')
            wallet = nodes[0].get_wallet_rpc('benchmark')
            address = wallet.getnewaddress()
            # get_wallet_rpc() derives a proxy sharing its parent's HTTP
            # connection. Create a new root proxy for the concurrent worker.
            load_rpc = get_rpc_proxy(nodes[0].url, nodes[0].index, timeout=60)
            samples = []
            with ThreadPoolExecutor(max_workers=1) as executor:
                future = executor.submit(load_rpc.generatetoaddress, 4, address)
                while not future.done():
                    start = time.perf_counter()
                    nodes[0].getpeerinfo()
                    samples.append(1000 * (time.perf_counter() - start))
                    time.sleep(0.01)
                assert_equal(len(future.result()), 4)
            self.sync_blocks(nodes)
            row['getpeerinfo_under_rpc_block_generation'] = {'samples': len(samples), 'median_ms': statistics.median(samples) if samples else None, 'max_ms': max(samples) if samples else None}
            row['node_metrics_after_rpc_block_generation'] = [process_metrics(node) for node in nodes]
            report[mode] = row
            self.log.info('%s optional node diagnostic: %s', mode, json.dumps(row))
        target = Path(self.options.tmpdir) / 'pq-node-benchmark.json'
        target.write_text(json.dumps(report, indent=2) + '\n')
        self.log.info('Optional diagnostic complete: %s', target)


if __name__ == '__main__':
    PQNodeBenchmark(__file__).main()
