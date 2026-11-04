#!/usr/bin/env python3
"""Fresh-process replay of existing shared-engine valid/hostile stage measurements."""

import csv
import hashlib
import io
import json
import os
import pathlib
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[4]
WORK = ROOT / "work/wo08"
DEST = ROOT / "src/test/mercaweave/final-security-artifacts"


def main():
    binary = ROOT / "work/build-release/mercaweave_qualification_bench"
    records = []
    profiles = []
    for inputs, groups, large in [
        (100, 1, False),
        (256, 256, False),
        (4096, 4, False),
        (1, 1, True),
    ]:
        command = [
            str(binary),
            str(inputs),
            str(groups),
            *(["--large-output"] if large else []),
        ]
        log = WORK / f"hostile-{inputs}-{groups}-{int(large)}.csv"
        started = time.monotonic()
        with log.open("w") as stream:
            child = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT)
            _, status, usage = os.wait4(child.pid, 0)
            child.returncode = os.waitstatus_to_exitcode(status)
        assert child.returncode == 0
        rows = list(csv.DictReader(io.StringIO(log.read_text())))
        assert len(rows) == 1
        records.extend(rows)
        profiles.append(
            dict(
                command=command,
                exit_code=child.returncode,
                wall_seconds=time.monotonic() - started,
                user_seconds=usage.ru_utime,
                system_seconds=usage.ru_stime,
                max_rss_kib=usage.ru_maxrss,
            )
        )
    with (DEST / "hostile-stage-measurements.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=records[0].keys(), lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(records)
    data = dict(
        passed=True,
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        profiles=profiles,
        scope="shared-engine stage timings; actual Chainstate timings/profiles are separate",
        samples="three warmups then eleven measured, median and worst; fixture/signing outside timings",
        allocation_scope="untimed ordinary C++ new/new[] requested volume; excludes C malloc/realloc and stack",
    )
    (DEST / "hostile-stage-profiles.json").write_text(json.dumps(data, indent=2) + "\n")


if __name__ == "__main__":
    main()
