#!/usr/bin/env python3
"""Link ONLY the existing native Solo wallet suite to an ordinary root build.

Reuses the exact generated native compiler/link settings and unchanged native
main.cpp + wallet/test/mining_tests.cpp. Does not add research guards or build
an executable miner. Output is a Boost unit-test executable, not a mining CLI.
"""

import argparse
import pathlib
import re
import shlex
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cwd = build / "src/test"
    flags = (cwd / "CMakeFiles/test_bitcoin.dir/flags.make").read_text()
    options = []
    for key in ["CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS"]:
        options += shlex.split(re.search("^" + key + r" = (.*)$", flags, re.M)[1])
    if any("MERCATURA_MERCAWEAVE" in arg for arg in options):
        raise RuntimeError("ordinary native flags required")
    objects, commands = [], []
    for source in ["src/test/main.cpp", "src/wallet/test/mining_tests.cpp"]:
        obj = out / (
            pathlib.Path(source).parent.name + "-" + pathlib.Path(source).stem + ".o"
        )
        cmd = ["/usr/bin/c++", *options, "-c", str(ROOT / source), "-o", str(obj)]
        commands.append(cmd)
        subprocess.run(cmd, check=True, cwd=cwd)
        objects.append(str(obj))
    original = shlex.split((cwd / "CMakeFiles/test_bitcoin.dir/link.txt").read_text())
    link = []
    i = 0
    while i < len(original):
        arg = original[i]
        i += 1
        if arg == "-o":
            i += 1
            continue
        if arg.endswith(".o") or arg.startswith("-Wl,--dependency-file="):
            continue
        if arg.endswith((".a", ".so")):
            arg = str((cwd / arg).resolve())
            if not pathlib.Path(arg).is_file():
                raise RuntimeError("build native dependency first: " + arg)
        link.append(arg)
    link[1:1] = objects
    link += ["-o", str(out / "wo07_native_unit_tests")]
    commands.append(link)
    subprocess.run(link, check=True, cwd=cwd)
    import json

    (out / "build-commands.json").write_text(json.dumps(commands, indent=2) + "\n")
    print(out / "wo07_native_unit_tests")


if __name__ == "__main__":
    main()
