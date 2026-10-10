#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Check M3 source hygiene, including new files in an uncommitted checkout."""
import ast
from pathlib import Path
import re
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    files = sorted((root / "src/pool").glob("*")) + [
        root / "test/functional/mercatura_pool_coordinator.py",
        Path(__file__).resolve(),
        root / ".github/workflows/mercatura-pool.yml",
        root / "doc/pool-coordinator.md",
    ]
    failures = []
    for file in files:
        if not file.is_file():
            continue
        data = file.read_bytes()
        text = data.decode("utf-8")
        name = file.relative_to(root)
        if not data.endswith(b"\n") or b"\r" in data:
            failures.append(f"{name}: require LF lines and final newline")
        for index, line in enumerate(text.splitlines(), 1):
            if line != line.rstrip():
                failures.append(f"{name}:{index}: trailing whitespace")
        if file.suffix == ".py":
            ast.parse(text, filename=str(name))
        if file.suffix in {".h", ".cpp"}:
            includes = re.findall(r"^#include .+$", text, re.MULTILINE)
            if len(includes) != len(set(includes)):
                failures.append(f"{name}: duplicate include")
        if file.suffix == ".h":
            guard = "BITCOIN_POOL_" + file.stem.upper() + "_H"
            if f"#ifndef {guard}\n#define {guard}\n" not in text or not text.endswith(f"#endif // {guard}\n"):
                failures.append(f"{name}: incorrect include guard")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("PASS M3 source hygiene, Python syntax, headers and includes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
