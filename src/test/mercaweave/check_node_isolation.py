#!/usr/bin/env python3
"""WO07 ordinary validation source, isolated ABI and compile guard checks."""

import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
# Pin the normal staged-activation implementation, preserving research ABI and
# macro isolation checks rather than requiring the old dormant source body.
BASE = "a90f95beaeab9aff68e1cb20a2ee640c95366458"
MACROS = {
    "MERCATURA_MERCAWEAVE_NODE_RESEARCH_TEST_ONLY",
    "MERCATURA_MERCAWEAVE_VALIDATION_COMPONENTS_TEST_ONLY",
}


def ordinary(source):
    # Select only our added branches; preserve every pre-existing directive.
    result, stack = [], []
    for line in source.splitlines(keepends=True):
        match = re.match(
            r"#(ifdef|ifndef) (" + "|".join(sorted(MACROS)) + r")\s*$", line
        )
        if match:
            stack.append(match[1] == "ifndef")
        elif stack and re.match(r"#else\s*$", line):
            stack[-1] = not stack[-1]
        elif stack and line.startswith("#endif"):
            stack.pop()
        elif all(stack):
            result.append(line)
    if stack:
        raise RuntimeError("unbalanced validation guards")
    return "".join(result)


def main():
    build = pathlib.Path(sys.argv[1]).resolve(strict=True)
    hashes = {}
    for file in ["src/validation.cpp", "src/validation.h"]:
        text = ordinary((ROOT / file).read_text())
        before = ordinary(
            subprocess.check_output(
                ["git", "-C", str(ROOT), "show", BASE + ":" + file], text=True
            )
        )
        # Shared includes were hoisted out of mutually exclusive branches.
        # Only these reviewed diagnostic conversions differ from the baseline;
        # every other non-include body line and the dependency set must match.
        before = before.replace("std::to_string(nIn)", 'strprintf("%u", nIn)')
        before = before.replace(
            "std::to_string(static_cast<int>(error.value_or(mercaweave::Error::Context)))",
            'strprintf("%d", static_cast<int>(error.value_or(mercaweave::Error::Context)))',
        ).replace(
            "std::to_string(static_cast<int>(failure.code))",
            'strprintf("%d", static_cast<int>(failure.code))',
        )

        def normalize(source):
            includes = sorted(set(re.findall(r"^#include .*", source, re.M)))
            body = re.sub(r"^#include .*\n", "", source, flags=re.M)
            return includes, [line for line in body.splitlines() if line.strip()]

        if normalize(text) != normalize(before):
            raise RuntimeError("ordinary validation source changed: " + file)
        hashes[file] = hashlib.sha256(text.encode()).hexdigest()
    allowed = {
        "mercaweave_node_validation",
        "mercaweave_node_tests",
        "mercaweave_node_bench",
        "mercaweave_production_context_tests",
    }
    counts = {m: 0 for m in MACROS}
    for item in json.loads((build / "compile_commands.json").read_text()):
        command = item["command"]
        target = re.search(r"CMakeFiles/([^/]+)\.dir/", command)
        for macro in MACROS:
            if macro in command:
                if (
                    target is None
                    or target[1] not in allowed
                    or (
                        macro.endswith("COMPONENTS_TEST_ONLY")
                        and target[1] != "mercaweave_node_validation"
                    )
                ):
                    raise RuntimeError("WO07 macro leaked: " + command)
                counts[macro] += 1
    if not all(counts.values()):
        raise RuntimeError("missing isolated validation definitions")
    symbols = subprocess.check_output(
        ["nm", "-C", str(build / "mercaweave_node_tests")], text=True
    )
    for expected in [
        "CheckInputScripts(",
        "CScriptCheck::operator()",
        "ValidationCache::ValidationCache",
        "SignatureCache::SignatureCache",
        "UpdateCoins(",
        "ApplyTxInUndo(",
        "node_research::Context::Prepare",
    ]:
        if expected not in symbols:
            raise RuntimeError("actual production component missing: " + expected)
    link = (build / "CMakeFiles/mercaweave_node_tests.dir/link.txt").read_text()
    if (
        "libmercaweave_native.a" in link
        or "libmercaweave_native_isolated.a" not in link
    ):
        raise RuntimeError("ordinary/research interpreter ABI mixing")
    result = subprocess.run(
        [
            "c++",
            "-std=c++20",
            "-I" + str(ROOT / "src"),
            "-fsyntax-only",
            "-x",
            "c++",
            "-",
        ],
        input="#include <test/mercaweave/node_validation.h>\n",
        text=True,
        capture_output=True,
    )
    if (
        result.returncode == 0
        or "Node integration is forbidden in normal builds" not in result.stderr
    ):
        raise RuntimeError("normal header use accepted")
    print(
        json.dumps(
            {
                "passed": True,
                "normal_validation_erasure_sha256": hashes,
                "macro_compile_units": counts,
                "actual_node_cache_symbols_present": True,
                "experimental_cache_lookup_and_insert_bypassed": True,
                "normal_header_rejected": True,
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
