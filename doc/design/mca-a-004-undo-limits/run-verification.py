#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Link the bounded investigation fixture into an existing Unix Makefiles build."""

import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path, help="Configured and built CMake tree")
    args = parser.parse_args()
    build = args.build.resolve()
    output = Path(tempfile.mkdtemp(prefix="mercatura-b5-verification-"))
    print(f"Evidence directory: {output}", flush=True)
    test_dir = build / "src/test"
    flags = {}
    for line in (test_dir / "CMakeFiles/test_bitcoin.dir/flags.make").read_text().splitlines():
        if " = " in line:
            key, value = line.split(" = ", 1)
            flags[key] = shlex.split(value)
    link = shlex.split((test_dir / "CMakeFiles/test_bitcoin.dir/link.txt").read_text())
    if Path(link[0]).name == "ccache":
        link.pop(0)
    source = Path(__file__).with_name("verification.cpp")
    obj = output / "verification.o"
    compile_command = [link[0], *flags["CXX_DEFINES"], *flags["CXX_INCLUDES"],
                       *flags["CXX_FLAGS"], "-c", str(source), "-o", str(obj)]
    with (output / "compile.log").open("w") as log:
        subprocess.run(compile_command, stdout=log, stderr=subprocess.STDOUT, check=True)
    executable = output / "test_b5"
    position = link.index("-o")
    link[position + 1] = str(executable)
    link.insert(position, str(obj))
    with (output / "link.log").open("w") as log:
        subprocess.run(link, cwd=test_dir, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run([str(executable), "--run_test=b5_*", "--log_level=message",
                    "--report_level=detailed", "--", f"-testdatadir={output / 'data'}"], check=True)


if __name__ == "__main__":
    main()
