#!/usr/bin/env python3
"""Verify WO04 compile/link isolation and normal inactive behavior (no node).

Usage: check_isolation.py BUILD_DIRECTORY
Checks the standalone Makefiles build used by this qualification.
"""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
MACRO = "MERCATURA_MERCAWEAVE_RESEARCH_TEST_ONLY"
BASE = "2ff60be0b6c5f58f36110ecba5cb72ca2663126d"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    build = pathlib.Path(sys.argv[1]).resolve(strict=True)
    erased = {}
    for name in ["src/script/interpreter.h", "src/script/interpreter.cpp"]:
        source = (ROOT / name).read_text()
        normal, count = re.subn(r"#ifdef " + MACRO + r"\n.*?#endif\n", "", source, flags=re.DOTALL)
        original = subprocess.check_output(["git", "-C", str(ROOT), "show", BASE + ":" + name], text=True)
        require(count == 1 and normal == original, "normal-source erasure mismatch: " + name)
        erased[name] = hashlib.sha256(normal.encode()).hexdigest()
    commands = json.loads((build / "compile_commands.json").read_text())
    allow = {
        "mercaweave_native_isolated", "mercaweave_context_isolated", "mercaweave_block_isolated",
        "mercaweave_isolated_control", "mercaweave_coin_validation", "mercaweave_coin_probe",
        "mercaweave_integration", "mercaweave_integration_tests", "mercaweave_integration_bench",
        "mercaweave_wallet_primitives", "mercaweave_wallet_primitive_probe", "mercaweave_wallet",
        "mercaweave_wallet_tests", "mercaweave_wallet_bench", "mercaweave_psbt_tests", "mercaweave_reward_planner", "mercaweave_reward_tests", "mercaweave_qt_bridge", "mercaweave_qt_integration_tests",
    }
    flags, ordinary, interpreters = 0, 0, []
    for entry in commands:
        command = entry.get("command", " ".join(entry.get("arguments", [])))
        target = re.search(r"CMakeFiles/([^/]+)\.dir/", command)
        enabled = MACRO in command
        if enabled:
            require(target is not None and target[1] in allow, "macro leaked outside isolated target")
            flags += 1
        else:
            ordinary += 1
        if pathlib.Path(entry["file"]).resolve() == ROOT / "src/script/interpreter.cpp":
            interpreters.append(enabled)
    require(sorted(interpreters) == [False, True], "need separate ordinary and isolated interpreter objects")
    targets = ["mercaweave_isolated_control", "mercaweave_coin_probe", "mercaweave_integration_tests"]
    if (build / "mercaweave_integration_bench").exists():
        targets.append("mercaweave_integration_bench")
    for target in targets:
        link = (build / f"CMakeFiles/{target}.dir/link.txt").read_text()
        require("libmercaweave_native_isolated.a" in link and "libmercaweave_native.a" not in link,
                "checker ABI mixing: " + target)
    normal_symbols = subprocess.check_output(["nm", "-C", str(build / "mercaweave_normal_control")], text=True)
    isolated_symbols = subprocess.check_output(["nm", "-C", str(build / "mercaweave_integration_tests")], text=True)
    method = "CheckMercaWeaveResearchAuthorization"
    require(method not in normal_symbols and method in isolated_symbols, "hook symbol isolation")
    require("SignatureCache::" not in isolated_symbols and "ValidationCache::" not in isolated_symbols,
            "experimental result may reach node caches")
    controls = {}
    for target in ["mercaweave_normal_control", "mercaweave_isolated_control"]:
        controls[target] = json.loads(subprocess.check_output([str(build / target)], text=True))
        require(controls[target]["passed"], "inactive behavior control")
    require(controls["mercaweave_normal_control"]["contextless_v3_mandatory"] is True and
            controls["mercaweave_isolated_control"]["contextless_v3_mandatory"] is False, "wrong v3 controls")
    definitions = subprocess.check_output(["git", "-C", str(ROOT), "grep", "-l", MACRO, "--", "*CMakeLists.txt"], text=True).splitlines()
    require(definitions == ["src/test/mercaweave/CMakeLists.txt"], "normal build defines research hook")
    for name in ["CMakeLists.txt", "src/CMakeLists.txt", "src/test/CMakeLists.txt"]:
        require("mercaweave" not in (ROOT / name).read_text().lower(), "research target included by normal build")
    forbidden = subprocess.run(["c++", "-std=c++20", "-UNDEBUG", "-I" + str(ROOT / "src"), "-fsyntax-only", "-x", "c++", "-"],
        input="#include <test/mercaweave/isolated_validation.h>\n", capture_output=True, text=True)
    require(forbidden.returncode != 0 and "must never be included by a normal node target" in forbidden.stderr,
            "research-only header failed to reject normal compilation")
    print(json.dumps({"passed": True, "normal_source_erasure_sha256": erased,
        "isolated_compile_units": flags, "ordinary_compile_units": ordinary,
        "checker_abis_separate": True, "normal_hook_symbol_absent": True,
        "node_cache_symbols_absent": True, "normal_header_use_rejected": True, "controls": controls}, sort_keys=True))


if __name__ == "__main__":
    main()
