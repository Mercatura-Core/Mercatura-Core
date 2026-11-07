#!/usr/bin/env python3
"""WO05 normal-wallet source erasure, compile-symbol and API isolation checks."""

import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
MACRO = "MERCATURA_MERCAWEAVE_WALLET_RESEARCH_TEST_ONLY"
# Normal activated custody supersedes WO05's dormant-source baseline. The
# private research API and compile/symbol fences remain independently checked.
BASE = "a90f95beaeab9aff68e1cb20a2ee640c95366458"


def project(text):
    ordinary, count = re.subn(
        r"#ifdef " + MACRO + r"\n.*?#else\n", "", text, flags=re.S
    )
    ordinary = re.sub(r"#endif // " + MACRO + r" (prefix|suffix)\n", "", ordinary)
    if count != 2:
        raise RuntimeError("wallet research guard count changed")
    return ordinary


def main():
    build = pathlib.Path(sys.argv[1]).resolve(strict=True)
    text = (ROOT / "src/wallet/wallet.cpp").read_text()
    ordinary = project(text)
    original = project(
        subprocess.check_output(
            ["git", "-C", str(ROOT), "show", BASE + ":src/wallet/wallet.cpp"], text=True
        )
    )

    # A shared SHA256 include is now hoisted above the research guard. Compare
    # the exact dependency set and every remaining source byte independently.
    def normalized(source):
        includes = sorted(set(re.findall(r"^#include .*", source, re.M)))
        body = re.sub(r"^#include .*\n", "", source, flags=re.M)
        return includes, [line for line in body.splitlines() if line.strip()]

    if normalized(ordinary) != normalized(original):
        raise RuntimeError("normal wallet source changed")
    definitions = subprocess.check_output(
        ["git", "-C", str(ROOT), "grep", "-l", MACRO, "--", "*CMakeLists.txt"],
        text=True,
    ).splitlines()
    if definitions != ["src/test/mercaweave/CMakeLists.txt"]:
        raise RuntimeError("wallet macro outside standalone target")
    allowed = {
        "mercaweave_sqlite_native",
        "mercaweave_parameter_probe",
        "mercaweave_durable_wallet",
        "mercaweave_durable_tests",
        "mercaweave_wallet_primitives",
        "mercaweave_wallet_primitive_probe",
        "mercaweave_wallet",
        "mercaweave_wallet_tests",
        "mercaweave_psbt_tests",
        "mercaweave_reward_planner",
        "mercaweave_reward_tests",
        "mercaweave_qt_bridge",
        "mercaweave_qt_integration_tests",
        "mercaweave_wallet_bench",
    }
    isolated = 0
    for entry in json.loads((build / "compile_commands.json").read_text()):
        command = entry["command"]
        if MACRO in command:
            target = re.search(r"CMakeFiles/([^/]+)\.dir/", command)
            if target is None or target[1] not in allowed:
                raise RuntimeError("wallet macro leaked into ordinary target")
            isolated += 1
    normal = subprocess.check_output(
        ["nm", "-C", str(build / "mercaweave_normal_control")], text=True
    )
    if "ResearchEncryptPQSeed" in normal or "wallet_research::" in normal:
        raise RuntimeError("normal control exposes research wallet")
    forbidden = subprocess.run(
        [
            "c++",
            "-std=c++20",
            "-UNDEBUG",
            "-I" + str(ROOT / "src"),
            "-fsyntax-only",
            "-x",
            "c++",
            "-",
        ],
        input="#include <test/mercaweave/research_wallet.h>\n",
        capture_output=True,
        text=True,
    )
    if (
        forbidden.returncode == 0
        or "forbidden in normal builds" not in forbidden.stderr
    ):
        raise RuntimeError("wallet header failed to reject normal compilation")
    for file in ["CMakeLists.txt", "src/CMakeLists.txt", "src/wallet/CMakeLists.txt"]:
        if "mercaweave" in (ROOT / file).read_text().lower():
            raise RuntimeError("normal build includes experimental wallet")
    print(
        json.dumps(
            {
                "passed": True,
                "normal_wallet_source_sha256": hashlib.sha256(
                    ordinary.encode()
                ).hexdigest(),
                "wallet_compile_units": isolated,
                "normal_wallet_symbols_absent": True,
                "normal_header_rejected": True,
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
