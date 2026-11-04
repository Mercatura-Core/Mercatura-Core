#!/usr/bin/env python3
"""Measure one native test process using wait4 (independent of build RSS)."""

import argparse
import json
import os
import pathlib
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[0] == "--" else args.command
    started = time.monotonic()
    process = subprocess.Popen(command)
    _, status, usage = os.wait4(process.pid, 0)
    process.returncode = os.waitstatus_to_exitcode(status)
    args.output.write_text(
        json.dumps(
            dict(
                command=command,
                exit_code=process.returncode,
                wall_seconds=time.monotonic() - started,
                cpu_user_seconds=usage.ru_utime,
                cpu_system_seconds=usage.ru_stime,
                max_rss_kib=usage.ru_maxrss,
                minor_page_faults=usage.ru_minflt,
                major_page_faults=usage.ru_majflt,
            ),
            indent=2,
        )
        + "\n"
    )
    raise SystemExit(process.returncode)


if __name__ == "__main__":
    main()
