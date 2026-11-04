#!/usr/bin/env python3
"""Profile a single actual-component test child; bounded wall time, no RSS inflation."""

import argparse
import hashlib
import json
import os
import pathlib
import signal
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[4]
WORK = ROOT / "work/wo08"
BUILDS = {
    "release": "work/wo07-native-release",
    "debug": "work/wo07c/native-debug-build",
    "sanitize": "work/wo07c/native-sanitize-build",
    "tsan": "work/wo08/native-tsan-build",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=BUILDS, required=True)
    parser.add_argument("--label", default="final")
    parser.add_argument("--suite")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    mode = args.mode
    label = args.label + "-" + mode
    out = WORK / ("native-" + mode)
    if not args.no_build:
        build = [
            "python3",
            str(ROOT / "src/test/mercaweave/security_audit/build_native.py"),
            "--build",
            str(ROOT / BUILDS[mode]),
            "--output",
            str(out),
        ]
        with (WORK / (label + "-build.log")).open("w") as stream:
            subprocess.run(
                build, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, check=True
            )
    binary = out / "wo08_native_tests"
    command = [
        str(binary),
        "--log_level=test_suite",
        "--report_level=detailed",
        "--color_output=no",
    ]
    if args.suite:
        command += ["--run_test=" + args.suite]
    command += ["--", "-testdatadir=" + str(WORK / ("runtime-" + label))]
    env = os.environ.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
    env["UBSAN_OPTIONS"] = "halt_on_error=1"
    env["TSAN_OPTIONS"] = "halt_on_error=1:exitcode=66"
    started = time.monotonic()
    timeout = False
    with (WORK / (label + ".log")).open("w") as stream:
        child = subprocess.Popen(
            command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT
        )
        while True:
            pid, status, usage = os.wait4(child.pid, os.WNOHANG)
            if pid:
                break
            if time.monotonic() - started > args.timeout:
                timeout = True
                os.kill(child.pid, signal.SIGTERM)
                pid, status, usage = os.wait4(child.pid, 0)
                break
            time.sleep(0.2)
        child.returncode = os.waitstatus_to_exitcode(status)
    summary = dict(
        mode=mode,
        command=command,
        exit_code=child.returncode,
        timeout=timeout,
        wall_seconds=time.monotonic() - started,
        user_seconds=usage.ru_utime,
        system_seconds=usage.ru_stime,
        max_rss_kib=usage.ru_maxrss,
        major_faults=usage.ru_majflt,
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        source_sha256={
            name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
            for name in [
                "src/wallet/pqgroup.cpp",
                "src/wallet/sqlite.cpp",
                "src/wallet/wallet.cpp",
                "src/crypto/hkdf_sha3_384.cpp",
                "src/script/pqgroup.cpp",
                "src/test/mercaweave/security_audit/native_tests.cpp",
                "src/test/mercaweave/security_audit/allocation.cpp",
            ]
        },
    )
    (WORK / (label + ".json")).write_text(json.dumps(summary, indent=2) + "\n")
    raise SystemExit(child.returncode)


if __name__ == "__main__":
    main()
