#!/usr/bin/env python3
"""Reproduce WO03 local measurements; no node, network, or production build.

Run after building the standalone Release target. Each case has a fresh process;
fixtures/signing are outside its timed section. Writes CSV plus provenance JSON.
"""
import argparse
import csv
import datetime
import hashlib
import io
import json
import pathlib
import platform
import subprocess

BLOCKS = "small consolidate53 consolidate100 independent100 max_groups max_members max_inputs mixed early_invalid late_invalid".split()
INVALID = "late_member late_signature duplicate_anchor final_reference altered_output spent_amount spent_script negative_amount witness_final groups_over members_over inputs_over truncated oversized_count".split()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(__file__).resolve().parents[3]
    records = {}
    commands = []
    for label, cases, mode, extra in [
        ("block-benchmarks", BLOCKS, "block", []),
        ("transaction-benchmarks", BLOCKS[:8], "block", ["--single"]),
        ("invalid-workloads", INVALID, "invalid", []),
    ]:
        rows = []
        for case in cases:
            command = [str(binary), mode, case, *extra]
            run = subprocess.run(command, check=True, text=True, capture_output=True, timeout=60)
            parsed = list(csv.DictReader(io.StringIO(run.stdout)))
            if len(parsed) != 1:
                raise RuntimeError(f"expected one row: {command}")
            rows.extend(parsed)
            commands.append(command)
            print(f"{label}: {case} passed", flush=True)
        target = out / f"{label}.csv"
        with target.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        records[label] = rows
    git = lambda *opts: subprocess.check_output(["git", "-C", str(root), *opts], text=True).strip()
    cache = binary.parent / "CMakeCache.txt"
    flags = [line for line in cache.read_text().splitlines() if line.startswith((
        "CMAKE_BUILD_TYPE:", "CMAKE_CXX_COMPILER:", "CMAKE_CXX_FLAGS:", "CMAKE_CXX_FLAGS_RELEASE:", "MERCAWEAVE_SANITIZERS:"))]
    metadata = {
        "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "source_head": git("rev-parse", "HEAD"),
        "source_branch": git("branch", "--show-current"),
        "tracked_diff_at_measurement": git("diff", "--stat"),
        "binary": str(binary), "binary_sha256": sha(binary), "build_cache": flags,
        "platform": platform.platform(),
        "cpu": subprocess.check_output(["lscpu"], text=True),
        "memory": pathlib.Path("/proc/meminfo").read_text().splitlines()[:4],
        "compiler": subprocess.check_output(["g++", "--version"], text=True).splitlines()[0],
        "reference_budgets": {"bytes": 4194304, "witness_items": 16384, "sigops": 80000, "legacy_weight": "measured_only"},
        "method": "one warmup; seven wall samples; median/max; mean per-run user/system CPU; separate untimed ordinary C++ new probe",
        "limitations": [
            "single x86_64 host and compiler; local self-review; no independent audit",
            "synthetic coinbase/header/UTXOs; no PoW, merkle, maturity, fee or full node acceptance claim",
            "RSS is process high-water mark including fixture/signing and parent-process launch floor",
            "allocation count excludes malloc/realloc/aligned new and is requested volume, not live memory",
            "component/manifest columns count payload bytes, excluding fixed hash domain/framing overhead",
            "per-transaction rows include synthetic block envelope bytes; tx_fee_full_bytes excludes envelope",
        ],
        "commands": commands,
        "artifact_sha256": {p.name: sha(p) for p in sorted(out.glob("*.csv"))},
        "source_sha256": {p.name: sha(p) for p in sorted(pathlib.Path(__file__).parent.glob("*"))
                          if p.suffix in (".cpp", ".h", ".py") or p.name == "CMakeLists.txt"},
        "records": records,
    }
    (out / "consensus-readiness-benchmarks.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
