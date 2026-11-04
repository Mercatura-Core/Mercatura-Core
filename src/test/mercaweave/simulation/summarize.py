#!/usr/bin/env python3
"""Summarize actual completed stages, never planned workloads or RPC submission alone."""

import argparse
import collections
import csv
import hashlib
import json
import pathlib
import statistics
import subprocess
import sys

sys.path.insert(
    0, str(pathlib.Path(__file__).resolve().parents[4] / "src/test/mercaweave")
)
from simulation.core import write_json

ROOT = pathlib.Path(__file__).resolve().parents[4]

BASE = "e014a10efa17fa0a8ac053650cbede9960da055f"


def median(rows, key):
    values = [r[key] for r in rows if key in r]
    return (
        dict(median=statistics.median(values), minimum=min(values), maximum=max(values))
        if values
        else None
    )


def stage(path):
    log = (path / "run.log").read_text()
    if "WO07B_NATIVE_SIMULATION_PASS" not in log or "Tests successful" not in log:
        raise ValueError("stage did not complete: " + str(path))
    rows = json.loads((path / "transactions.json").read_text())
    events = json.loads((path / "events.json").read_text())
    resources = json.loads((path / "resources.json").read_text())
    if not rows or not all(r["accepted"] and r["confirmed"] for r in rows):
        raise ValueError("unconfirmed/unaccepted rows")
    if len({r["txid"] for r in rows}) != len(rows):
        raise ValueError("duplicate successful transaction identity")
    manifest = json.loads((path / "manifest.json").read_text())
    phases = collections.Counter(r["phase"] for r in rows)
    blocks = [r for r in events if r["kind"] == "block"]
    active = [r for r in blocks if r["transactions"]]
    byphase = {
        p: dict(
            transactions=sum(r["phase"] == p for r in rows),
            bytes=sum(r["bytes"] for r in rows if r["phase"] == p),
            fees=sum(r["fee_units"] for r in rows if r["phase"] == p),
            inputs=sum(r["inputs"] for r in rows if r["phase"] == p),
            outputs=sum(r["outputs"] for r in rows if r["phase"] == p),
        )
        for p in phases
    }
    peak = max(
        n.get("high_water_kib", n["rss_kib"]) for s in resources for n in s["nodes"]
    )
    cpu = {}
    for sample in resources:
        for n in sample["nodes"]:
            cpu[n["pid"]] = max(cpu.get(n["pid"], 0), n["cpu_seconds"])
    result = dict(
        stage=path.name,
        manifest=manifest,
        actual_transactions=len(rows),
        confirmed=len(rows),
        witness_signatures=sum(r["native_signatures"] for r in rows),
        serialized_bytes=sum(r["bytes"] for r in rows),
        weight=sum(r["weight"] for r in rows),
        fees_units=sum(r["fee_units"] for r in rows),
        phases=byphase,
        input_distribution=dict(collections.Counter(str(r["inputs"]) for r in rows)),
        output_distribution=dict(collections.Counter(str(r["outputs"]) for r in rows)),
        timings_ms={
            k: median(rows, k)
            for k in [
                "construction_ms",
                "signing_ms",
                "admission_ms",
                "propagation_ms",
                "confirmation_ms",
            ]
        },
        blocks=active,
        observed_block_capacity_bytes=4194304,
        peak_node_rss_kib=peak,
        observed_process_cpu_seconds=sum(cpu.values()),
        peak_logical_disk_bytes=max(s["disk_bytes"] for s in resources),
        sampled_disk_growth=max(s["disk_bytes"] for s in resources)
        - resources[0]["disk_bytes"],
        events=[r for r in events if r["kind"] != "block"],
        source_hashes=json.loads((path / "source-hashes.json").read_text())
        if (path / "source-hashes.json").exists()
        else None,
        transaction_table_sha256=hashlib.sha256(
            (path / "transactions.json").read_bytes()
        ).hexdigest(),
    )
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--campaign", action="append", type=pathlib.Path, required=True)
    p.add_argument("--research", type=pathlib.Path, required=True)
    p.add_argument(
        "--output",
        type=pathlib.Path,
        default=ROOT / "src/test/mercaweave/simulation-artifacts",
    )
    args = p.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    stages = []
    failed = []
    for campaign in args.campaign:
        records = json.loads((campaign / "stages.json").read_text())
        for record in records:
            if record["returncode"] != 0:
                log = (
                    campaign
                    / record.get("stage", f"{record['transactions']}-{record['seed']}")
                    / "run.log"
                )
                lines = log.read_text().splitlines()
                errors = [
                    i
                    for i, line in enumerate(lines)
                    if any(
                        kind in line
                        for kind in (
                            "JSONRPCException:",
                            "ValueError:",
                            "AssertionError:",
                        )
                    )
                ]
                trace = (
                    lines[max(0, errors[-1] - 2) : errors[-1] + 1]
                    if errors
                    else lines[-5:]
                )
                failed.append(
                    dict(
                        campaign=campaign.name,
                        **record,
                        failure_excerpt=trace,
                        run_log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),
                    )
                )
                continue
            current = stage(
                campaign
                / record.get("stage", f"{record['transactions']}-{record['seed']}")
            )
            current["campaign"] = campaign.name
            current["duration_seconds"] = record["duration_seconds"]
            stages.append(current)
    prior = {}
    names = subprocess.check_output(
        [
            "git",
            "ls-tree",
            "-r",
            "--name-only",
            BASE,
            "doc/design",
            "src/test/mercaweave",
        ],
        text=True,
        cwd=ROOT,
    ).splitlines()
    for name in names:
        path = ROOT / name
        if (
            name.startswith("doc/design/mercaweave-")
            or path.suffix in [".csv", ".json", ".jsonl"]
            or "artifacts/" in name
        ):
            original = subprocess.check_output(
                ["git", "show", BASE + ":" + name], cwd=ROOT
            )
            if path.read_bytes() != original:
                raise ValueError("immutable evidence changed: " + name)
            prior[name] = hashlib.sha256(original).hexdigest()
    research = []
    for mode in ["release", "debug", "sanitize"]:
        path = args.research / f"research-{mode}.csv"
        rows = list(csv.DictReader(path.open()))
        if len(rows) != 11:
            raise ValueError("research coverage incomplete")
        stable = lambda r: {
            k: v for k, v in r.items() if not k.endswith("_ms") and k != "maxrss_kib"
        }
        if research and [stable(r) for r in rows] != [
            stable(r) for r in research[0]["rows"]
        ]:
            raise ValueError("research cross-build disagreement")
        research.append(dict(build=mode, rows=rows))
        (out / path.name).write_bytes(path.read_bytes())
    write_json(out / "failed-attempts.json", failed)
    write_json(out / "native-campaigns.json", stages)
    write_json(out / "research-campaigns.json", research)
    write_json(out / "preserved-evidence.json", prior)
    with (out / "native-summary.csv").open("w") as f:
        writer = csv.DictWriter(
            f,
            lineterminator="\n",
            fieldnames=[
                "campaign",
                "stage",
                "actual_transactions",
                "confirmed",
                "witness_signatures",
                "serialized_bytes",
                "weight",
                "fees_units",
                "peak_node_rss_kib",
                "observed_process_cpu_seconds",
                "peak_logical_disk_bytes",
                "duration_seconds",
            ],
        )
        writer.writeheader()
        for item in stages:
            writer.writerow({k: item[k] for k in writer.fieldnames})
    print(
        json.dumps(
            dict(
                real_native_transactions=sum(r["actual_transactions"] for r in stages),
                completed_stages=len(stages),
                preserved_files=len(prior),
                research_shapes=33,
            )
        )
    )


if __name__ == "__main__":
    main()
