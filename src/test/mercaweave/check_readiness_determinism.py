#!/usr/bin/env python3
"""Compare three existing local research builds with saved WO03 verdicts.

Run from any directory after building work/build-{release,debug,sanitize}.
Does not rebuild, access a node, or change cryptographic reference vectors.
"""
import csv
import hashlib
import io
import json
import os
import pathlib
import subprocess


def main():
    root = pathlib.Path(__file__).resolve().parents[3]
    out = root / "src/test/mercaweave/readiness-artifacts"
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    results = {"vectors": {}, "malleability": {}, "comparisons": []}
    vectors, proofs = [], []
    for mode in ["release", "debug", "sanitize"]:
        build = root / f"work/build-{mode}"
        raw = subprocess.check_output([str(build / "mercaweave_tests"), "--vectors"], env=env)
        vectors.append(raw)
        results["vectors"][mode] = hashlib.sha256(raw).hexdigest()
        raw = subprocess.check_output([str(build / "mercaweave_malleability_tests")], env=env)
        proofs.append(raw)
        results["malleability"][mode] = hashlib.sha256(raw).hexdigest()
    if not vectors[0] == vectors[1] == vectors[2] or not proofs[0] == proofs[1] == proofs[2]:
        raise RuntimeError("cross-build vector or malleability proof divergence")
    (out / "malleability-proof.jsonl").write_bytes(proofs[0])
    ignored = set("median_ms worst_ms user_ms system_ms peak_rss_kib cpp_new_calls cpp_requested_bytes cpp_largest_request".split())
    for mode in ["debug", "sanitize"]:
        exe = str(root / f"work/build-{mode}/mercaweave_readiness_bench")
        datasets = [("transaction-benchmarks.csv", "block", ["--single"]), ("invalid-workloads.csv", "invalid", [])]
        if mode == "sanitize":
            datasets.append(("block-benchmarks.csv", "block", []))
        for filename, command, extra in datasets:
            with (out / filename).open() as stream:
                references = list(csv.DictReader(stream))
            for ref in references:
                name = ref["scenario"]
                raw = subprocess.check_output([exe, command, name, *extra], env=env, text=True, timeout=60)
                rows = list(csv.DictReader(io.StringIO(raw)))
                if len(rows) != 1:
                    raise RuntimeError(f"unexpected row count: {mode} {name}")
                for key, value in ref.items():
                    if key not in ignored and rows[0][key] != value:
                        raise RuntimeError(f"counter divergence: {mode} {name} {key}: {value} != {rows[0][key]}")
                results["comparisons"].append({"build": mode, "dataset": filename, "scenario": name, "equal_verdict_and_counters": True})
                print(mode, filename, name, "PASS", flush=True)
    results["comparison_count"] = len(results["comparisons"])
    results["source_head"] = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    results["build_cmake_sha256"] = hashlib.sha256((root / "src/test/mercaweave/CMakeLists.txt").read_bytes()).hexdigest()
    results["limitations"] = [
        "single x86_64 GCC 14.2 host; no independent alternate compiler or architecture",
        "production policy.cpp alone excludes UBSan vptr metadata for discarded node-dependent functions; all new research code uses complete ASan/UBSan",
    ]
    (out / "determinism-checks.json").write_text(json.dumps(results, indent=2) + "\n")
    print("PASS deterministic vector/proof byte equality and", len(results["comparisons"]), "counter comparisons")


if __name__ == "__main__":
    main()
