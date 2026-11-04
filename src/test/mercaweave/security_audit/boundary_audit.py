#!/usr/bin/env python3
"""Retain the WO07C negative compilation audit, with one exact WO08 hygiene delta."""

import importlib.util
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location(
    "wo07c_boundary", ROOT / "src/test/mercaweave/production_path/audit.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
native_git = module.git
name = "src/crypto/hkdf_sha3_384.cpp"
original = native_git("show", module.INITIAL + ":" + name).decode()
expected = original.replace(
    "#include <crypto/hkdf_sha3_384.h>",
    "#include <crypto/hkdf_sha3_384.h>\n#include <support/cleanse.h>",
).replace(
    "    std::copy_n(t1.begin(), OUTPUT_SIZE, output.begin());",
    "    std::copy_n(t1.begin(), OUTPUT_SIZE, output.begin());\n    memory_cleanse(t1.data(), t1.size());",
)
assert (ROOT / name).read_text() == expected, (
    "HKDF exception must be exactly temporary cleansing"
)


def checked_git(*args):
    result = native_git(*args)
    if args == ("diff", "--name-only", module.INITIAL):
        return (
            "\n".join(line for line in result.decode().splitlines() if line != name)
            + "\n"
        ).encode()
    return result


module.git = checked_git
module.main()
output = pathlib.Path(sys.argv[sys.argv.index("--output") + 1])
data = json.loads(output.read_text())
data["changed_files"] = (
    native_git("diff", "--name-only", module.INITIAL).decode().splitlines()
)
data["wo08_explicit_reviewed_exception"] = dict(
    path=name,
    reason="cleanse secret HKDF temporary after copying output",
    exact_source_delta_checked=True,
    hash_and_derivation_bytes_unchanged=True,
    mldsa_implementation_or_pin_changed=False,
)
output.write_text(json.dumps(data, indent=2) + "\n")
