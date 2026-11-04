#!/usr/bin/env python3
"""Run deliberately unsafe and owned-construction controls as separate children."""

import json
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[4]
WORK = ROOT / "work/wo08"
SOURCE = ROOT / "src/test/mercaweave/security_audit"


def main():
    rows = []
    env = os.environ.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
    for mode in ["debug", "sanitize"]:
        for safe in [False, True]:
            name = f"conditional-unwind-{mode}-" + ("safe" if safe else "original")
            command = [
                "g++",
                "-std=c++20",
                *(
                    ["-O0", "-g"]
                    if mode == "debug"
                    else ["-O1", "-g", "-fsanitize=address,undefined"]
                ),
                *(["-DWO08_SAFE_WITNESS_CONTROL=1"] if safe else []),
                str(SOURCE / "conditional_unwind_probe.cpp"),
                str(SOURCE / "allocation.cpp"),
                "-o",
                str(WORK / name),
            ]
            subprocess.run(command, check=True)
            with (WORK / (name + ".log")).open("w") as stream:
                result = subprocess.run(
                    [str(WORK / name)],
                    env=env,
                    stdout=stream,
                    stderr=subprocess.STDOUT,
                    timeout=30,
                )
            rows.append(
                dict(
                    mode=mode,
                    safe=safe,
                    build_command=command,
                    exit_code=result.returncode,
                    log=name + ".log",
                )
            )
    (WORK / "conditional-unwind-controls.json").write_text(
        json.dumps(rows, indent=2) + "\n"
    )


if __name__ == "__main__":
    main()
