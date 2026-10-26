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
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/functional'))
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, get_rpc_proxy


def process_metrics(node):
    pid = node.process.pid
    values = {}
    for line in Path(f'/proc/{pid}/status').read_text().splitlines():
        key, _, value = line.partition(':')
        if key in ['VmRSS', 'VmHWM', 'VmSize']:
            values[key + '_kib'] = int(value.split()[0])
    stat = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    values['cpu_seconds'] = (int(stat[11]) + int(stat[12])) / os.sysconf('SC_CLK_TCK')
    return values


class PQNodeBenchmark(BitcoinTestFramework):
    def set_test_params(self):
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

    def run_test(self):
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
