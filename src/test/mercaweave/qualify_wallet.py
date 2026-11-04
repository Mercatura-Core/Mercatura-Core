#!/usr/bin/env python3
"""WO05 post-build qualification. Never replaces WO01–04 evidence or runs a node."""

import argparse
import csv
import datetime
import hashlib
import io
import json
import os
import pathlib
import platform
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[3]
BASE = "93b356af34bd1ea60fd7c58a99533f3d0e93bc31"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for mode in ("release", "debug", "sanitize"):
        parser.add_argument("--" + mode, type=pathlib.Path, required=True)
    args = parser.parse_args()
    out = ROOT / "src/test/mercaweave/wallet-artifacts"
    out.mkdir(exist_ok=True)
    builds = {
        mode: getattr(args, mode).resolve(strict=True)
        for mode in ("release", "debug", "sanitize")
    }
    env = dict(
        os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1"
    )
    commands, tests, controls, digests, cache, rows, comparisons = (
        [],
        {},
        {},
        {},
        {},
        {},
        [],
    )

    def run(cmd, timeout=180):
        commands.append(list(map(str, cmd)))
        return subprocess.check_output(cmd, text=True, env=env, timeout=timeout)

    for mode, build in builds.items():
        log = (build / "Testing/Temporary/LastTest.log").read_text()
        tests[mode] = {
            "passed": log.count("Test Passed."),
            "failed": log.count("Test Failed."),
            "skipped": log.count("Test did not run"),
        }
        if tests[mode] != {"passed": 22, "failed": 0, "skipped": 0}:
            raise RuntimeError("complete 22-target CTest checkpoint required: " + mode)
        (out / (mode + "-ctest.txt")).write_text(log)
        cache[mode] = [
            line
            for line in (build / "CMakeCache.txt").read_text().splitlines()
            if line.startswith(
                (
                    "CMAKE_BUILD_TYPE:",
                    "CMAKE_CXX_FLAGS:",
                    "CMAKE_CXX_FLAGS_RELEASE:",
                    "CMAKE_CXX_FLAGS_DEBUG:",
                    "CMAKE_CXX_FLAGS_RELWITHDEBINFO:",
                    "MERCAWEAVE_SANITIZERS:",
                )
            )
        ]
        digests[mode], controls[mode] = {}, {}
        for target, opts in (
            ("mercaweave_tests", ["--vectors"]),
            ("mercaweave_wallet_tests", []),
            ("mercaweave_psbt_tests", []),
            ("mercaweave_integration_tests", []),
            ("mercaweave_normal_control", []),
            ("mercaweave_isolated_control", []),
        ):
            raw = run([build / target, *opts])
            digests[mode][target] = hashlib.sha256(raw.encode()).hexdigest()
            if mode == "release":
                (
                    out
                    / (
                        target
                        + (
                            ".jsonl"
                            if target == "mercaweave_integration_tests"
                            else ".json"
                        )
                    )
                ).write_text(raw)
            if target.endswith("_control"):
                controls[mode][target] = json.loads(raw)
        for script in ("check_isolation.py", "check_wallet_isolation.py"):
            result = json.loads(
                run(["python3", ROOT / "src/test/mercaweave" / script, build])
            )
            if not result["passed"]:
                raise RuntimeError("isolation failure")
            (out / (mode + "-" + script.replace(".py", ".json"))).write_text(
                json.dumps(result, indent=2) + "\n"
            )
        raw = run([build / "mercaweave_wallet_bench"])
        (out / (mode + "-workloads.csv")).write_text(raw)
        rows[mode] = list(csv.DictReader(io.StringIO(raw)))
        if len(rows[mode]) != 24:
            raise RuntimeError(
                "twelve workloads with two fair ownership comparisons required"
            )
        for row in rows[mode]:
            if (
                row["verified"] != "true"
                or row["estimated_fee_units"] != row["actual_fee_units"]
                or int(row["max_standard_weight"]) > 400000
                or int(row["signatures"])
                != int(row["groups"]) + int(row["native_inputs"])
            ):
                raise RuntimeError("workload invariant: " + str(row))
            if mode != "release":
                ref = next(
                    r
                    for r in rows["release"]
                    if (r["workload"], r["scheme"]) == (row["workload"], row["scheme"])
                )
                if {k: v for k, v in row.items() if not k.endswith("_ms")} != {
                    k: v for k, v in ref.items() if not k.endswith("_ms")
                }:
                    raise RuntimeError("cross-build divergence: " + row["workload"])
                comparisons.append(
                    {
                        "build": mode,
                        "workload": row["workload"],
                        "scheme": row["scheme"],
                        "equal_sizes_fees_signatures_and_public_hash": True,
                    }
                )
        print(mode + ": 22 CTest passes, 24 validated workload rows", flush=True)
    for target in digests["release"]:
        if len({value[target] for value in digests.values()}) != 1:
            raise RuntimeError("deterministic test-output divergence: " + target)
    prior = {}
    for name in subprocess.check_output(
        [
            "git",
            "-C",
            str(ROOT),
            "ls-tree",
            "-r",
            "--name-only",
            BASE,
            "src/test/mercaweave",
            "doc/design",
        ],
        text=True,
    ).splitlines():
        path = ROOT / name
        if (
            name.startswith("doc/design/mercaweave-")
            or path.suffix in (".json", ".csv", ".jsonl")
            or "artifacts/" in name
        ):
            old = subprocess.check_output(
                ["git", "-C", str(ROOT), "show", BASE + ":" + name]
            )
            if path.read_bytes() != old:
                raise RuntimeError("prior evidence changed: " + name)
            prior[name] = sha(path)
    source = {
        str(p.relative_to(ROOT)): sha(p)
        for p in sorted((ROOT / "src/test/mercaweave").iterdir())
        if p.is_file() and p.suffix in (".cpp", ".h", ".py", ".txt")
    }
    original = pathlib.Path("/workspace/Mercatura-Core")
    git = lambda root, *opts: subprocess.check_output(
        ["git", "-C", str(root), *opts], text=True
    ).strip()
    if (
        git(original, "status", "--porcelain")
        or git(original, "rev-parse", "HEAD")
        != "4b88b619fb1b08c7f30112a38eb471880b2a6af4"
    ):
        raise RuntimeError("original worktree changed")
    metadata = {
        "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "start_head": BASE,
        "source_head": git(ROOT, "rev-parse", "HEAD"),
        "branch": git(ROOT, "branch", "--show-current"),
        "source_diff": git(ROOT, "diff", "--stat"),
        "original_head": git(original, "rev-parse", "HEAD"),
        "original_status": git(original, "status", "--porcelain"),
        "platform": platform.platform(),
        "compiler": subprocess.check_output(
            ["g++", "--version"], text=True
        ).splitlines()[0],
        "ctest": tests,
        "build_cache_flags": cache,
        "commands": commands,
        "deterministic_test_output_sha256": digests,
        "cross_build_comparisons": comparisons,
        "source_sha256": source,
        "prior_evidence_sha256": prior,
        "controls": controls,
        "method": "One complete real PSBT workflow per scenario per build; wall timings include repeated inspection/verification. Public fixture seeds and explicitly named zero signing entropy; production API defaults to native strong entropy. Encryption ciphertexts intentionally random and not compared or retained.",
        "scope": "research wallet facade using native seed envelopes, derivation, PSBT parser/Merge, COutput/OutputGroup and BnB/CoinGrinder/Knapsack; actual isolated WO04 validation; no CWallet database, RPC, node, mempool, activation or live data",
        "sanitizers": {
            "ASAN_OPTIONS": "detect_leaks=0",
            "UBSAN_OPTIONS": "halt_on_error=1",
            "leak_sanitizer": "unavailable in prior ptrace probe; not passed or rerun as a supported tool",
            "research_checks": "all address and undefined behavior checks enabled",
        },
        "optional_tools": {
            name: shutil.which(name)
            for name in ("clang++", "clang", "afl-fuzz", "honggfuzz", "valgrind")
        },
        "production_files": {
            name: sha(ROOT / name)
            for name in (
                "src/wallet/wallet.cpp",
                "src/script/interpreter.h",
                "src/script/interpreter.cpp",
                "src/psbt.h",
                "src/psbt.cpp",
                "src/wallet/coinselection.cpp",
            )
        },
        "artifact_sha256": {
            p.name: sha(p)
            for p in sorted(out.iterdir())
            if p.is_file() and p.name != "qualification.json"
        },
    }
    (out / "qualification.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(
        "PASS: 48 cross-build workload comparisons; deterministic test output; prior evidence unchanged; original worktree clean",
        flush=True,
    )


if __name__ == "__main__":
    main()
