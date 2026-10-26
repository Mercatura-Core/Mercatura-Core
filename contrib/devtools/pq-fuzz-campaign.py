#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Bounded genuine libFuzzer campaigns, retained corpora and machine-readable evidence.

All seeds are public synthetic protocol/vector material. No sockets, node
datadirs, secret operational keys or external services are used by the targets.
Use a new output directory to preserve earlier evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
TARGETS = ['pq_handshake', 'pq_packet', 'pq_packet_stream', 'pq_identity', 'pq_headers', 'pq_command']


def seeds(target):
    if target == 'pq_headers':
        sizes = [0, 3265, 4353, 4409, 3369, 60]
        return [bytes([n, 0 if n % 2 else 1, 1, 0, 1, 0, 0, 0]) +
                (sizes[n]-12).to_bytes(4, 'little') + bytes([n]) for n in range(1, 6)]
    if target == 'pq_command':
        return [bytes([n]) for n in range(256)] + [b'\0ping'+bytes(8), b'\0blockchunk'+bytes(2)]
    if target == 'pq_identity':
        fp = b'mcapq1:' + b'a'*96
        return [b'\0\1', fp, b'localhost:18444='+fp, b'[::1]:18444='+fp]
    if target == 'pq_handshake':
        return [bytes(16)+b'\1'+bytes([n]) for n in range(5)]
    if target == 'pq_packet_stream':
        return [b'\x12'+bytes([n, role]) for role in range(2) for n in range(8)]
    return [bytes(n) for n in [0, 3, 20, 64, 512]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds-per-target', type=int, default=60)
    parser.add_argument('--targets', nargs='+', choices=TARGETS, default=TARGETS)
    parser.add_argument('--disable-leak-check', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.seconds_per_target <= 3600:
        parser.error('seconds-per-target must be 1..3600')
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for number, target in enumerate(args.targets):
        corpus = args.output / target
        corpus.mkdir()
        for value in seeds(target):
            (corpus / hashlib.sha256(value).hexdigest()).write_bytes(value)
        artifacts = args.output / (target+'-artifacts')
        artifacts.mkdir()
        command = [str(args.binary.resolve()), str(corpus.resolve()),
                   '-max_total_time='+str(args.seconds_per_target), '-max_len=16384',
                   '-timeout=10', '-rss_limit_mb=1024', '-seed='+str(1000+number),
                   '-print_final_stats=1', '-artifact_prefix='+str(artifacts.resolve())+'/']
        environment = os.environ.copy()
        environment['FUZZ'] = target
        environment['ASAN_OPTIONS'] = 'detect_leaks=' + ('0' if args.disable_leak_check else '1') + ':halt_on_error=1'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        start = time.monotonic()
        log = args.output / (target+'.log')
        with log.open('w') as stream:
            completed = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                       env=environment, timeout=args.seconds_per_target+45)
        text = log.read_text(errors='replace')
        executed = re.findall(r'stat::number_of_executed_units:\s*(\d+)', text)
        coverage = re.findall(r'cov:\s*(\d+)\s+ft:\s*(\d+)', text)
        record = {'target': target, 'command': command, 'wall_seconds': time.monotonic()-start,
                  'returncode': completed.returncode, 'executions': int(executed[-1]) if executed else None,
                  'edge_coverage_counter': int(coverage[-1][0]) if coverage else None,
                  'feature_counter': int(coverage[-1][1]) if coverage else None,
                  'corpus_files': sum(1 for p in corpus.iterdir() if p.is_file()),
                  'corpus_bytes': sum(p.stat().st_size for p in corpus.iterdir() if p.is_file()),
                  'crash_artifacts': sorted(p.name for p in artifacts.iterdir()),
                  'leak_check_requested': not args.disable_leak_check}
        results.append(record)
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2)+'\n')
        print(json.dumps(record), flush=True)
        if completed.returncode != 0:
            raise SystemExit('Fuzzer failed; preserve '+str(log)+' and artifacts, diagnose before continuing')


if __name__ == '__main__':
    main()
