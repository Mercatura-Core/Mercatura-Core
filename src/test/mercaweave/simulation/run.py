#!/usr/bin/env python3
"""Bounded WO07B stage runner. Failed stages retain nodes/logs and earlier results."""

import argparse
import dataclasses
import json
import pathlib
import os
import signal
import subprocess
import sys
import time

sys.path.insert(
    0, str(pathlib.Path(__file__).resolve().parents[4] / "src/test/mercaweave")
)
from simulation.core import Manifest, write_json

ROOT = pathlib.Path(__file__).resolve().parents[4]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--resume", action="store_true")
    p.add_argument("--counts", nargs="+", type=int, default=[100, 500, 1000])
    p.add_argument("--bootstrap-blocks", type=int, default=101)
    p.add_argument("--nodes", type=int, default=3)
    p.add_argument("--seed", type=int, default=7)
    p.add_argument(
        "--custody",
        choices=["independent", "reused", "centralized"],
        default="independent",
    )
    p.add_argument("--batch-size", type=int, default=20)
    p.add_argument("--epoch-size", type=int, default=1000)
    p.add_argument("--mining-interval", type=int, default=0)
    p.add_argument("--soak-seconds", type=int, default=0)
    p.add_argument("--max-soak-cycles", type=int, default=10)
    p.add_argument("--extended", action="store_true")
    p.add_argument("--capacity-steps", action="store_true")
    p.add_argument("--lifecycle", action="store_true")
    p.add_argument("--no-disruptions", action="store_true")
    p.add_argument(
        "--build", type=pathlib.Path, default=ROOT / "work/wo07-native-release"
    )
    p.add_argument("--work", type=pathlib.Path, required=True)
    p.add_argument("--timeout", type=int, default=1800)
    args = p.parse_args()

    def interrupted(_signal, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupted)
    work = args.work.resolve()
    if work.exists() and not args.resume:
        raise RuntimeError(
            "use a fresh campaign root or explicit --resume; existing evidence is preserved"
        )
    work.mkdir(parents=True, exist_ok=True)
    results = (
        json.loads((work / "stages.json").read_text())
        if args.resume and (work / "stages.json").exists()
        else []
    )
    for count in args.counts:
        m = Manifest(
            seed=args.seed,
            transactions=count,
            nodes=args.nodes,
            epoch_size=args.epoch_size,
            batch_size=args.batch_size,
            custody=args.custody,
            mining_interval=args.mining_interval,
            soak_seconds=args.soak_seconds,
            max_soak_cycles=args.max_soak_cycles,
            disruptions=not args.no_disruptions,
            extended=args.extended,
            lifecycle=args.lifecycle,
            capacity_steps=args.capacity_steps,
            bootstrap_blocks=args.bootstrap_blocks,
        ).validate()
        successful = [
            r
            for r in results
            if r["transactions"] == count
            and r["seed"] == m.seed
            and r["returncode"] == 0
        ]
        if successful:
            prior = (
                work
                / successful[-1].get("stage", f"{count}-{m.seed}")
                / "manifest.json"
            )
            if dataclasses.asdict(
                Manifest(**json.loads(prior.read_text()))
            ) != dataclasses.asdict(m):
                raise ValueError("resume manifest differs from completed stage")
            continue
        stage = work / f"{count}-{args.seed}"
        attempt = 1
        while stage.exists():
            stage = work / f"{count}-{args.seed}-attempt-{attempt}"
            attempt += 1
        manifest = stage / "manifest.json"
        write_json(manifest, dataclasses.asdict(m))
        cmd = [
            sys.executable,
            str(ROOT / "src/test/mercaweave/simulation/native.py"),
            "--configfile=" + str(args.build.resolve() / "test/config.ini"),
            "--manifest=" + str(manifest),
            "--output=" + str(stage),
            "--tmpdir=" + str(stage / "nodes"),
            "--randomseed=" + str(m.seed),
        ]
        start = time.monotonic()
        with (stage / "run.log").open("w") as log:
            process = subprocess.Popen(
                cmd, stdout=log, stderr=subprocess.STDOUT, start_new_session=True
            )
            try:
                code = process.wait(timeout=args.timeout)
            except (subprocess.TimeoutExpired, KeyboardInterrupt):
                os.killpg(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                code = 124
        results.append(
            dict(
                stage=stage.name,
                transactions=count,
                seed=m.seed,
                returncode=code,
                duration_seconds=time.monotonic() - start,
                command=cmd,
            )
        )
        write_json(work / "stages.json", results)
        if code:
            raise SystemExit(code)


if __name__ == "__main__":
    main()
