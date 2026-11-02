#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Run each workload in a fresh process; preserve per-workload RSS provenance."""
import csv
import hashlib
import io
import json
from pathlib import Path
import platform
import subprocess
import sys
from datetime import datetime
from zoneinfo import ZoneInfo

MATRIX = [(1,1),(10,1),(53,1),(100,1),(500,1),(1000,1),
          (100,10),(100,100),(256,256),(1024,1),(4096,4)]


def run(executable, output):
    rows = []
    for n, g in MATRIX + [(1,1)]:
        command = [str(executable), str(n), str(g)]
        if len(rows) == len(MATRIX):
            command.append('--large-output')
        result = subprocess.check_output(command, text=True)
        row, = list(csv.DictReader(io.StringIO(result)))
        assert int(row['verification_calls']) == g
        assert int(row['ownership_checks']) == n
        assert int(row['cost']) == 4*g+n
        assert float(row['worst_verify_ms']) >= float(row['median_verify_ms'])
        rows.append(row)
    with output.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    metadata = {
        'recorded_at': datetime.now(ZoneInfo('America/Los_Angeles')).isoformat(),
        'engine_head': subprocess.check_output(['git','rev-parse','HEAD'], text=True).strip(),
        'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
        'csv_sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
        'system': platform.platform(),
        'cpu': 'AMD EPYC 9V74, three visible cloud VM CPUs; not Ryzen/WSL2',
        'warmups_per_stage': 3,
        'measured_runs_per_stage': 11,
        'timing': 'steady clock milliseconds; median and worst observed; no cold-cache claims',
        'rss': 'Linux ru_maxrss KiB, fresh child process per workload; includes fixture construction and attack copies',
        'allocation': 'separate untimed pass; ordinary C++ new/new[] requests only; excludes C malloc/realloc and aligned new',
        'validity': 'real keys/signatures and research authorization; synthetic UTXOs; no production-chain, maturity or relay validity claim',
        'large_output': 'one input/group, additional 1 MiB OP_RETURN output script diagnostic; exceeds standard weight',
        'flags': 'assertions enabled with -UNDEBUG; C++ release flags from accompanying build provenance',
        'build_provenance': (executable.parent / 'CMakeFiles/mercaweave.dir/flags.make').read_text(),
    }
    output.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'PASS resource workloads={len(rows)} csv={output}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: resource_qualification.py EXECUTABLE OUTPUT.csv')
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]))
