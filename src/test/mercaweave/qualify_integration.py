#!/usr/bin/env python3
"""WO04 measurements and cross-build qualification, without a node or network.

Build Release/Debug/sanitizer standalone targets first. Every benchmark case
runs in a fresh process; old qualification artifacts are never overwritten.
"""
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

CASES = "small consolidate53 consolidate100 independent100 max_groups max_members max_inputs mixed early_invalid late_invalid".split()
ROOT = pathlib.Path(__file__).resolve().parents[3]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", type=pathlib.Path, required=True)
    parser.add_argument("--debug", type=pathlib.Path)
    parser.add_argument("--sanitize", type=pathlib.Path)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    builds = {"release": args.release.resolve(strict=True)}
    for name in ["debug", "sanitize"]:
        if getattr(args, name):
            builds[name] = getattr(args, name).resolve(strict=True)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    commands = []

    def run(command):
        commands.append([str(arg) for arg in command])
        return subprocess.check_output(command, text=True, env=env, timeout=120)

    reference, outputs, records, comparisons, ctest, cache_flags = {}, {}, {}, [], {}, {}
    for mode, build in builds.items():
        log = (build / "Testing/Temporary/LastTest.log").read_text()
        ctest[mode] = {"passed": log.count("Test Passed."), "failed": log.count("Test Failed."), "skipped": log.count("Test did not run"),
                       "last_test_log_sha256": sha(build / "Testing/Temporary/LastTest.log")}
        if ctest[mode]["passed"] != 17 or ctest[mode]["failed"] or ctest[mode]["skipped"]:
            raise RuntimeError("expected complete green 17-target CTest checkpoint: " + mode)
        cache_flags[mode] = [line for line in (build / "CMakeCache.txt").read_text().splitlines()
                             if line.startswith(("CMAKE_BUILD_TYPE:", "CMAKE_CXX_FLAGS:", "CMAKE_CXX_FLAGS_RELEASE:", "CMAKE_CXX_FLAGS_DEBUG:", "CMAKE_CXX_FLAGS_RELWITHDEBINFO:", "MERCAWEAVE_SANITIZERS:"))]
        outputs[mode] = {}
        for target, extra in [("mercaweave_tests", ["--vectors"]), ("mercaweave_integration_tests", []),
                              ("mercaweave_normal_control", []), ("mercaweave_isolated_control", [])]:
            raw = run([build / target, *extra])
            outputs[mode][target] = hashlib.sha256(raw.encode()).hexdigest()
            if mode == "release":
                suffix = ".jsonl" if target == "mercaweave_integration_tests" else ".json"
                (out / (target + suffix)).write_text(raw)
        isolation = json.loads(run(["python3", ROOT / "src/test/mercaweave/check_isolation.py", build]))
        (out / (mode + "-isolation.json")).write_text(json.dumps(isolation, indent=2) + "\n")
        rows = []
        for smoke in ([False, True] if mode == "release" else [True]):
            for case in CASES:
                command = [build / "mercaweave_integration_bench", case, *(["--smoke"] if smoke else [])]
                row = list(csv.DictReader(io.StringIO(run(command))))
                if len(row) != 1:
                    raise RuntimeError("expected one measurement row")
                row = row[0]
                rows.append(row)
                if smoke:
                    if mode == "release":
                        reference[case] = row
                    else:
                        for key, value in row.items():
                            if key.endswith("_ms") or key in {"peak_rss_kib", "cpp_new_calls", "cpp_requested_bytes", "cpp_largest_request"}:
                                continue
                            if value != reference[case][key]:
                                raise RuntimeError(f"cross-build divergence: {mode} {case} {key}: {reference[case][key]} != {value}")
                        comparisons.append({"build": mode, "case": case, "equal_verdict_and_counters": True})
                print(mode, case, "smoke" if smoke else "packed", "PASS", flush=True)
        records[mode] = rows
        with (out / (mode + "-benchmarks.csv")).open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
    for target in outputs["release"]:
        if len({per_build[target] for per_build in outputs.values()}) != 1:
            raise RuntimeError("cross-build output mismatch: " + target)
    # Preserve original evidence, including byte-for-byte line endings.
    evidence = {}
    for name in subprocess.check_output(["git", "-C", str(ROOT), "ls-tree", "-r", "--name-only", "2ff60be", "src/test/mercaweave", "doc/design"], text=True).splitlines():
        path = ROOT / name
        if (name.startswith("doc/design/mercaweave-") or
                (name.startswith("src/test/mercaweave/") and path.suffix in (".csv", ".json", ".jsonl"))):
            old = subprocess.check_output(["git", "-C", str(ROOT), "show", "2ff60be:" + name])
            if name.endswith("mercaweave-v0.1-isolated-consensus-integration.md"):
                continue
            if path.read_bytes() != old:
                raise RuntimeError("old evidence changed: " + name)
            evidence[name] = sha(path)
    git = lambda *opts: subprocess.check_output(["git", "-C", str(ROOT), *opts], text=True).strip()
    leak = {"status": "not-run", "reason": "sanitizer build not provided"}
    if "sanitize" in builds:
        probe = subprocess.run([builds["sanitize"] / "mercaweave_coin_probe"], capture_output=True, text=True,
            env=dict(env, ASAN_OPTIONS="detect_leaks=1"), timeout=30)
        diagnostic = probe.stdout + probe.stderr
        (out / "leak-sanitizer.txt").write_text(diagnostic)
        leak = {"exit_code": probe.returncode, "status": "passed" if probe.returncode == 0 else "unavailable" if "ptrace" in diagnostic else "failed"}
        if leak["status"] == "failed":
            raise RuntimeError("unexpected leak-sanitizer failure; inspect leak-sanitizer.txt")
    metadata = {
        "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "source_head": git("rev-parse", "HEAD"), "branch": git("branch", "--show-current"),
        "tracked_diff": git("diff", "--stat"), "platform": platform.platform(),
        "compiler": subprocess.check_output(["g++", "--version"], text=True).splitlines()[0],
        "cpu": next(line.split(":", 1)[1].strip() for line in pathlib.Path("/proc/cpuinfo").read_text().splitlines() if line.startswith("model name")),
        "memory": pathlib.Path("/proc/meminfo").read_text().splitlines()[:4],
        "method": "one warmup; seven wall samples per phase; median/max; mean per-run user/system CPU; separate untimed ordinary C++ new probe for Candidate",
        "scope": "real VerifyScript, CheckTransaction, CheckTxInputs, CCoinsViewCache and policy helpers; no daemon, CheckInputScripts, CScriptCheck, ConnectBlock or network",
        "native_v2": "complete WO03 Prepare verifies native v2 once, then genuine Script dispatch verifies it again; both counted; no signature-cache shortcut",
        "limits": {"bytes": 4194304, "witness_items": 16384, "sigops": 80000, "standard_tx_weight": 400000, "legacy_weight": "measured separately"},
        "limitations": ["single cloud EPYC/GCC/x86_64 host; no independent audit", "RSS includes fixtures, simultaneously retained phase results and Python launch floor", "ordinary new requested volume excludes malloc/realloc/aligned new and is not live memory", "dispatch -1 for invalid sample means unavailable, never counted as a passed dispatch", "early-invalid Candidate stops at first failure; baseline VerifyWorkload measures all submitted requests"],
        "commands": commands, "output_sha256": outputs, "cross_build_comparisons": comparisons,
        "ctest": ctest, "build_cache_flags": cache_flags, "leak_sanitizer": leak,
        "available_optional_tools": {name: shutil.which(name) for name in ["clang++", "clang", "afl-fuzz", "honggfuzz", "valgrind"]},
        "binary_sha256": {mode: {name: sha(build / name) for name in ["mercaweave_integration_tests", "mercaweave_integration_bench", "mercaweave_normal_control", "mercaweave_isolated_control"]} for mode, build in builds.items()},
        "source_sha256": {str(p.relative_to(ROOT)): sha(p) for p in sorted((ROOT / "src/test/mercaweave").iterdir()) if p.is_file() and p.suffix in (".cpp", ".h", ".py", ".txt")},
        "production_hook_sha256": {name: sha(ROOT / name) for name in ["src/script/interpreter.h", "src/script/interpreter.cpp"]},
        "prior_evidence_sha256": evidence,
        "artifact_sha256": {p.name: sha(p) for p in sorted(out.iterdir()) if p.is_file() and p.name != "qualification.json"},
        "records": records,
    }
    (out / "qualification.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print("PASS", len(comparisons), "cross-build counter comparisons; identical vectors/integration/control output; prior evidence preserved")


if __name__ == "__main__":
    main()
