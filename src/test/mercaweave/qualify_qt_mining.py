#!/usr/bin/env python3
"""WO06 post-build qualification; summaries go only to ignored work storage.

Requires the three complete 28-target CTest checkpoints. No node, wallet DB,
network, signing service or broadcast is started. Earlier evidence is read-only.
"""
import argparse
import csv
import hashlib
import io
import json
import os
import pathlib
import platform
import shlex
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[3]
BASE = "259ef51316245a84348a1f6cdffaff9787bbcd8e"
KEYS = {"qt_checks", "reward_checks", "qt_wallet_checks", "native_qr_checks", "wallet_checks", "psbt_checks"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for mode in ("release", "debug", "sanitize"):
        parser.add_argument("--" + mode, type=pathlib.Path, required=True)
    args = parser.parse_args()
    builds = {mode: getattr(args, mode).resolve(strict=True) for mode in ("release", "debug", "sanitize")}
    git = lambda *opts: subprocess.check_output(["git", "-C", str(ROOT), *opts], text=True).strip()
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1", XDG_CACHE_HOME=str(ROOT / "work/wo06-qt-cache"))
    pathlib.Path(env["XDG_CACHE_HOME"]).mkdir(exist_ok=True)
    baseline = list(csv.DictReader((ROOT / "src/test/mercaweave/wallet-artifacts/release-workloads.csv").open()))
    records, commands, comparisons, reference_tests = {}, [], [], None
    for mode, build in builds.items():
        log = (build / "Testing/Temporary/LastTest.log").read_text()
        counts = {"passed": log.count("Test Passed."), "failed": log.count("Test Failed."), "skipped": log.count("Test did not run")}
        if counts != {"passed": 28, "failed": 0, "skipped": 0}:
            raise RuntimeError("complete 28-target CTest checkpoint required: " + mode)
        tests = []
        for line in log.splitlines():
            try:
                value = json.loads(line)
            except (ValueError, TypeError):
                continue
            if isinstance(value, dict) and KEYS.intersection(value):
                tests.append(value)
        if len(tests) != 7 or not all(v.get("passed") for v in tests):
            raise RuntimeError("missing deterministic unit summaries: " + mode)
        if reference_tests is None:
            reference_tests = tests
        elif tests != reference_tests:
            raise RuntimeError("cross-build unit summary divergence: " + mode)
        def run(cmd, extra=None):
            commands.append(list(map(str, cmd)))
            return subprocess.check_output(cmd, text=True, env=dict(env, **(extra or {})), timeout=300)
        vector = run([build / "mercaweave_tests", "--vectors"])
        expected = json.loads((ROOT / "src/test/mercaweave/wallet-artifacts/qualification.json").read_text())["deterministic_test_output_sha256"]["release"]["mercaweave_tests"]
        if hashlib.sha256(vector.encode()).hexdigest() != expected:
            raise RuntimeError("locked vector divergence")
        workloads = list(csv.DictReader(io.StringIO(run([build / "mercaweave_wallet_bench"]))))
        if len(workloads) != 24:
            raise RuntimeError("missing WO05 workload comparisons")
        for old, current in zip(baseline, workloads):
            stable = lambda r: {k: v for k, v in r.items() if not k.endswith("_ms")}
            if stable(old) != stable(current):
                raise RuntimeError("WO05 workload changed: " + current["workload"])
            comparisons.append({"build": mode, "workload": current["workload"], "scheme": current["scheme"], "matches_wo05_sizes_fees_signatures_and_fixture_hash": True})
        dpi = []
        for target in ("mercaweave_qt_normal_tests", "mercaweave_qt_model_tests"):
            value = json.loads(run([build / target], {"QT_SCALE_FACTOR": "2"}))
            if value not in tests:
                raise RuntimeError("high-DPI regression: " + target)
            dpi.append(value)
        objects = {}
        for entry in json.loads((build / "compile_commands.json").read_text()):
            if "CMakeFiles/mercaweave_qt_app_compile.dir/" in entry["command"]:
                argv = shlex.split(entry["command"]);path = build / argv[argv.index("-o") + 1]
                objects[pathlib.Path(entry["file"]).name] = sha(path)
        if len(objects) != 4:
            raise RuntimeError("affected normal application objects were not built")
        records[mode] = {"ctest": counts, "unit_summaries": tests, "high_dpi_summaries": dpi,
            "locked_vector_stdout_sha256": expected, "normal_application_object_sha256": objects,
            "build_flags": [line for line in (build / "CMakeCache.txt").read_text().splitlines() if line.startswith(("CMAKE_BUILD_TYPE:", "CMAKE_CXX_FLAGS:", "MERCAWEAVE_SANITIZERS:", "MERCAWEAVE_QT_TESTS:", "Qt6_DIR:"))]}
        print(mode + ": 28 CTest passes; 24 unchanged WO05 workload rows; 2 high-DPI runs", flush=True)
    prior = {}
    for name in git("ls-tree", "-r", "--name-only", BASE, "doc/design", "src/test/mercaweave").splitlines():
        path = ROOT / name
        if name.startswith("doc/design/mercaweave-") or path.suffix in (".json", ".csv", ".jsonl") or "artifacts/" in name:
            old = subprocess.check_output(["git", "-C", str(ROOT), "show", BASE + ":" + name])
            if path.read_bytes() != old:
                raise RuntimeError("prior evidence changed: " + name)
            prior[name] = sha(path)
    original = pathlib.Path("/workspace/Mercatura-Core")
    if subprocess.check_output(["git", "-C", str(original), "status", "--porcelain"], text=True).strip():
        raise RuntimeError("original worktree is dirty")
    summary = {"starting_head": BASE, "source_head": git("rev-parse", "HEAD"), "branch": git("branch", "--show-current"),
        "platform": platform.platform(), "compiler": subprocess.check_output(["g++", "--version"], text=True).splitlines()[0],
        "builds": records, "workload_comparisons": comparisons, "prior_evidence_sha256": prior, "commands": commands,
        "limitations": ["headless Qt components and normal application object compilation; no linked node/Qt wallet runtime", "no live chain, wallet DB, miner, mempool, restart or broadcast", "LeakSanitizer unavailable in prior ptrace probe; leak detection not passed", "Linux/GCC only; independent audit, TSan and cross-platform runtime not performed"]}
    destination = ROOT / "work/wo06-qualification.json"
    destination.write_text(json.dumps(summary, indent=2) + "\n")
    print("PASS: 84 CTest results; 72 comparisons against WO05 (including 48 cross-build comparisons); 6 high-DPI runs; " + str(len(prior)) + " prior evidence files unchanged", flush=True)
    print("Summary: " + str(destination), flush=True)


if __name__ == "__main__":
    main()
