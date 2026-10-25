#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Check existing primitives without a node build or a PQ handshake implementation.

No network access, production randomness, transport, key store, or consensus changes.
ML-KEM is optional and built only from the separately supplied pinned source tree.
Public deterministic fixture secrets may be passed to the test-only child process.
"""

import argparse
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "src/test/data/pq_p2p_primitives.json"
PIN = "d1b2fe782888bdb761a50336012923180be7f502"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha3_384(data).digest()


def mac(key, data):
    return hmac.digest(key, data, "sha3_384")


def expand(prk, info, length):
    require(0 <= length <= 255 * 48, "RFC 5869 output bound")
    out, block = b"", b""
    for i in range(1, (length + 47) // 48 + 1):
        block = mac(prk, block + info + bytes([i]))
        out += block
    return out[:length]


def build(args):
    dest = args.work_dir.resolve()
    dest.mkdir(parents=True, exist_ok=True)
    flags = ["-O1", "-g", "-fno-omit-frame-pointer"] if args.sanitize else ["-O2"]
    if args.sanitize:
        flags += ["-fsanitize=" + args.sanitize, "-fno-sanitize-recover=all"]
    include = ["-I" + str(ROOT / "src"), "-I" + str(ROOT / "src/crypto/mldsa_native/mldsa")]
    objects = []
    for name, defs in [("crypto/mercatura_mldsa.c", []), ("crypto/mldsa_native/mldsa/mldsa_native.c", ["-DMLD_CONFIG_CORE_API_ONLY"])]:
        obj = dest / (Path(name).stem + ".o")
        subprocess.run([args.cc, "-std=c99", *flags, *include, "-DMLD_CONFIG_PARAMETER_SET=65", *defs, "-c", str(ROOT / "src" / name), "-o", str(obj)], check=True)
        objects.append(str(obj))
    kem_flags = []
    if args.mlkem_source:
        source = args.mlkem_source.resolve()
        manifest = json.loads((ROOT / "doc/design/mercatura-pq-dependencies.json").read_text())
        require(manifest["mlkem_commit"] == PIN, "unexpected ML-KEM pin")
        for f in manifest["mlkem_files"]:
            data = (source / f["path"]).read_bytes()
            blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
            require(blob == f["blob"], "ML-KEM source mismatch: " + f["path"])
            require(hashlib.sha256(data).hexdigest() == f["sha256"], "ML-KEM SHA-256 mismatch: " + f["path"])
        obj = dest / "mlkem_native.o"
        kem_flags = ["-DPQ_CHECK_MLKEM", "-DMLK_CONFIG_PARAMETER_SET=768", "-DMLK_CONFIG_NO_RANDOMIZED_API", "-I" + str(source / "mlkem")]
        subprocess.run([args.cc, "-std=c99", *flags, *kem_flags, "-c", str(source / "mlkem/mlkem_native.c"), "-o", str(obj)], check=True)
        objects.append(str(obj))
    binary = dest / "pq-primitive-driver"
    sources = ["crypto/sha3.cpp", "crypto/hmac_sha3_384.cpp", "crypto/hkdf_sha3_384.cpp"]
    subprocess.run([args.cxx, "-std=c++20", *flags, *include, *kem_flags, str(ROOT / "contrib/devtools/pq-primitive-driver.cpp"), *(str(ROOT / "src" / s) for s in sources), *objects, "-o", str(binary)], check=True)
    return binary


class Driver:
    def __init__(self, binary):
        self.child = subprocess.Popen([str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def call(self, op, *args):
        parts = [op] + [(a.hex() or "-") if isinstance(a, bytes) else a for a in args]
        self.child.stdin.write(" ".join(parts) + "\n")
        self.child.stdin.flush()
        line = self.child.stdout.readline()
        require(bool(line), "primitive driver stopped during " + op)
        return line.split()

    def close(self):
        self.child.stdin.close()
        require(self.child.wait() == 0, "primitive driver failed")


def check_symmetric(driver):
    count = 0
    for size in [0, 1, 3, 8, 103, 104, 105, 207, 208, 209, 1024, 8193]:
        msg = bytes(i % 256 for i in range(size))
        require(driver.call("sha3", msg) == [sha(msg).hex()], "SHA3-384 mismatch")
        count += 1
        for key_size in [0, 1, 48, 103, 104, 105, 208]:
            key = bytes(i % 256 for i in range(key_size))
            require(driver.call("hmac", key, msg) == [mac(key, msg).hex()], "HMAC-SHA3-384 mismatch")
            count += 1
    for ikm, salt, info in [(b"", b"", b""), (bytes(range(64)), b"", b"\0"), (bytes(range(32)), bytes(range(120)), b"binary\0info"), (b"a" * 105, b"salt", b"Mercatura/P2PKeySchedule/v1")]:
        prk = mac(salt, ikm)
        require(driver.call("hkdf32", ikm, salt, info) == [prk.hex(), expand(prk, info, 32).hex()], "wallet HKDF mismatch")
        count += 1
    info = b"Mercatura/MLDSA65Key/v1\0" + bytes(range(32, 64)) + bytes(9)
    require(driver.call("hkdf32", bytes(range(32)), b"Mercatura/PQWalletMaster/v1", info) == ["0fa61af4e2a7c9a572bcf5b67443566a9c367a2e4148295e515964c105d44b8c0354aa88d98c9f2a3a548d6288f5dc01", "0be624be1e03a6fd1e078d25352115a74aca5696e332ab7e23e5dd019b784484"], "existing wallet golden vector changed")
    return count + 1


def check_acvp(driver, fixture, kem):
    count = 0
    for group in fixture["testGroups"]:
        if group["parameterSet"] == "ML-KEM-768" and not kem:
            continue
        for test in group["tests"]:
            b = lambda key: bytes.fromhex(test[key])
            label = f"ACVP tgId={group['tgId']} tcId={test['tcId']}"
            function = group.get("function", "keygen")
            if function == "keygen":
                if group["parameterSet"] == "ML-DSA-65":
                    pk, sk = driver.call("dsa-keygen", b("seed"))
                else:
                    rc, pk, sk = driver.call("kem-keygen", b("d") + b("z"))
                    require(rc == "0", label + " keygen failed")
                require(sha(bytes.fromhex(pk)).hex() == test["pk_sha3_384"], label + " public key mismatch")
                require(sha(bytes.fromhex(sk)).hex() == test["sk_sha3_384"], label + " secret key mismatch")
            elif function == "encapsulation":
                rc, ct, ss = driver.call("kem-enc", b("ek"), b("m"))
                require(rc == "0" and sha(bytes.fromhex(ct)).hex() == test["ct_sha3_384"] and ss == test["k"].lower(), label + " encapsulation mismatch")
            elif function == "decapsulation":
                rc, ss = driver.call("kem-dec", b("dk"), b("c"))
                require(rc == "0" and ss == test["k"].lower(), label + " implicit rejection/decapsulation mismatch")
            elif function in ["decapsulationKeyCheck", "encapsulationKeyCheck"]:
                rc = driver.call("kem-check-sk" if function == "decapsulationKeyCheck" else "kem-check-pk", b("dk" if function == "decapsulationKeyCheck" else "ek"))[0]
                expected = "0" if test["testPassed"] else ("-5" if function == "decapsulationKeyCheck" else "-4")
                require(rc == expected, label + " key check mismatch")
            else:
                raise RuntimeError("unsupported fixture group")
            count += 1
    return count


def check_signatures(driver):
    pk, sk = map(bytes.fromhex, driver.call("dsa-keygen", bytes(range(32))))
    msg, ctx, rnd = bytes(range(48)), b"Mercatura/P2PAuth/v1", bytes(range(32, 64))
    rc, sig = driver.call("dsa-sign", msg, ctx, rnd, sk)
    require(rc == "1" and len(bytes.fromhex(sig)) == 3309, "signing failed")
    require(driver.call("dsa-sign", msg, ctx, rnd, sk) == [rc, sig], "fixed-randomness determinism")
    require(driver.call("dsa-verify", bytes.fromhex(sig), msg, ctx, pk) == ["1"], "signature verify failed")
    sig_bytes = bytes.fromhex(sig)
    pk2 = bytes.fromhex(driver.call("dsa-keygen", bytes([99]) * 32)[0])
    cases = [(sig_bytes, msg[:-1] + bytes([msg[-1] ^ 1]), ctx, pk), (sig_bytes, msg, b"Mercatura/PQAuth/v1", pk), (sig_bytes, msg, ctx + b"\0", pk), (sig_bytes, msg, ctx, pk2), (bytes([sig_bytes[0] ^ 1]) + sig_bytes[1:], msg, ctx, pk), (sig_bytes[:-1], msg, ctx, pk), (sig_bytes, msg, ctx, pk[:-1])]
    for args in cases:
        require(driver.call("dsa-verify", *args) == ["0"], "bad signature accepted")
    require(driver.call("dsa-sign", msg, b"x" * 256, rnd, sk)[0] == "0", "oversized context accepted")
    digest = bytes.fromhex("3750b4d91da8afabc1584a2898626eb8d45f863f2fc4aa80e4c7d77ae26a59393a987e816588f269d759f8d978e589b5")
    tx_rc, tx_sig = driver.call("dsa-sign", digest, b"Mercatura/PQAuth/v1", bytes(32), sk)
    require(tx_rc == "1" and hashlib.sha256(bytes.fromhex(tx_sig)).hexdigest() == "12b1e37ac7df6e2b58601d5e2b9bd2f53cf6e25b89e1cb1d13dbd3644806bea4", "existing transaction signature golden vector changed")
    return 12


def check_kem_negatives(driver):
    rc, pk, sk = driver.call("kem-keygen", bytes(range(64)))
    require(rc == "0", "KEM keygen failed")
    pk, sk = bytes.fromhex(pk), bytes.fromhex(sk)
    rc, ct, ss = driver.call("kem-enc", pk, bytes(range(32)))
    require(rc == "0", "KEM encapsulation failed")
    ct = bytes.fromhex(ct)
    require(driver.call("kem-dec", sk, ct) == ["0", ss], "KEM roundtrip failed")
    modified = bytes([ct[0] ^ 1]) + ct[1:]
    rc_bad, secret_bad = driver.call("kem-dec", sk, modified)
    require(rc_bad == "0" and secret_bad != ss, "implicit rejection must succeed with a different secret")
    require(driver.call("kem-dec", sk, modified) == [rc_bad, secret_bad], "implicit rejection must be deterministic for fixed key/ciphertext")
    bad_pk = b"\xff" + bytes([pk[1] | 15]) + pk[2:]
    require(driver.call("kem-enc", bad_pk, bytes(32))[0] == "-4", "invalid modulus accepted")
    bad_sk = sk[:2336] + bytes([sk[2336] ^ 1]) + sk[2337:]
    require(driver.call("kem-dec", bad_sk, ct)[0] == "-5", "invalid embedded public-key hash accepted")
    return 5


def check_upstream_kat(driver, source):
    text = (source / "test/test_vectors/expected_test_vectors.h").read_text()
    block = text.split("#elif MLK_CONFIG_PARAMETER_SET == 768", 1)[1].split("#elif", 1)[0]
    def array(name, where):
        match = re.search(r"test_vector_" + name + r"\[\d+\]\s*=\s*\{([^}]+)\}", where)
        require(match is not None, "upstream KAT array missing")
        return bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", match[1]))
    coins = array("d", text) + array("z", text)
    m = array("m", text)
    pk, sk, ct, ss = (array(name, block) for name in ["pk", "sk", "ct", "ss"])
    require(driver.call("kem-keygen", coins) == ["0", pk.hex(), sk.hex()], "upstream keygen KAT mismatch")
    require(driver.call("kem-enc", pk, m) == ["0", ct.hex(), ss.hex()], "upstream encapsulation KAT mismatch")
    require(driver.call("kem-dec", sk, ct) == ["0", ss.hex()], "upstream decapsulation KAT mismatch")
    return 3


def check_candidate_primitives(driver):
    """Replay fixed primitive inputs only; this is not a handshake implementation."""
    fixture = json.loads((ROOT / "src/test/data/pq_p2p_candidate1.json").read_text())
    count = 0
    for row in fixture["signature_inputs"]:
        b = lambda name: bytes.fromhex(row[name])
        pk, sk = driver.call("dsa-keygen", b("seed"))
        require(pk == row["public_key"], "candidate identity fixture mismatch")
        require(driver.call("dsa-sign", b("message"), b("context"), b("randomness"), bytes.fromhex(sk)) == ["1", row["signature"]], "candidate signing fixture mismatch")
        require(driver.call("dsa-verify", b("signature"), b("message"), b("context"), b("public_key")) == ["1"], "candidate signature fixture rejected")
        count += 1
    for row in fixture["confirmation"].values():
        require(driver.call("hmac", bytes.fromhex(row["key"]), bytes.fromhex(row["message"])) == [row["tag"]], "candidate confirmation primitive mismatch")
        count += 1
    return count


def check_openssl_hkdf():
    """Independent RFC 5869 implementation, when the OpenSSL CLI is available."""
    if not shutil.which("openssl"):
        return None
    fixture = json.loads((ROOT / "src/test/data/pq_p2p_candidate1.json").read_text())
    def kdf(length, options):
        command = ["openssl", "kdf", "-keylen", str(length), "-kdfopt", "digest:SHA3-384"]
        for option in options:
            command += ["-kdfopt", option]
        result = subprocess.run([*command, "HKDF"], check=True, capture_output=True, text=True)
        return result.stdout.replace(":", "").strip().lower()
    row = fixture["extract"]
    require(kdf(48, ["mode:EXTRACT_ONLY", "hexkey:" + row["IKM"], "hexsalt:" + row["salt"]]) == row["PRK"], "OpenSSL Extract mismatch")
    for out in fixture["expand"]:
        require(kdf(out["length"], ["mode:EXPAND_ONLY", "hexkey:" + row["PRK"], "hexinfo:" + out["info"]]) == out["output"], "OpenSSL Expand mismatch")
    # Additional RFC 5869 cases exercise empty info, zero salt, and multiple blocks.
    for salt, info, length in [(b"", b"", 1), (bytes(48), b"", 48), (b"salt", b"binary\0info", 96), (bytes(range(120)), b"domain", 80)]:
        ikm = bytes(range(64))
        expected = expand(mac(salt, ikm), info, length).hex()
        require(kdf(length, ["hexkey:" + ikm.hex(), "hexsalt:" + salt.hex(), "hexinfo:" + info.hex()]) == expected, "OpenSSL multiblock reference mismatch")
    return 12


def check_mldsa_signature_acvp(driver):
    fixture = json.loads((ROOT / "src/test/data/pq_p2p_mldsa_acvp.json").read_text())
    count = 0
    for group in fixture["groups"]:
        for test in group["tests"]:
            b = lambda key: bytes.fromhex(test[key])
            label = f"NIST Pure ML-DSA tgId={group['tgId']} tcId={test['tcId']}"
            if group["operation"] == "sigGen":
                require(driver.call("dsa-sign", b("message"), b("context"), b("rnd"), b("sk")) == ["1", test["signature"]], label + " full signature mismatch")
                require(driver.call("dsa-verify", b("signature"), b("message"), b("context"), b("pk")) == ["1"], label + " generated signature rejected")
            else:
                require(driver.call("dsa-verify", b("signature"), b("message"), b("context"), b("pk")) == ["1" if test["testPassed"] else "0"], label + " verification result mismatch")
            count += 1
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--mlkem-source", type=Path)
    parser.add_argument("--sanitize", choices=["undefined", "address,undefined"])
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    args = parser.parse_args()
    binary = build(args)
    driver = Driver(binary)
    try:
        symmetric = check_symmetric(driver)
        acvp = check_acvp(driver, json.loads(DATA.read_text()), bool(args.mlkem_source))
        signatures = check_signatures(driver)
        kem_negatives = check_kem_negatives(driver) if args.mlkem_source else 0
        upstream = check_upstream_kat(driver, args.mlkem_source.resolve()) if args.mlkem_source else 0
        candidate = check_candidate_primitives(driver)
        openssl_hkdf = check_openssl_hkdf()
        nist_signatures = check_mldsa_signature_acvp(driver)
    finally:
        driver.close()
    print(json.dumps({"result": "PASS", "symmetric_cases": symmetric, "nist_acvp_cases": acvp, "nist_mldsa_signature_cases": nist_signatures, "signature_cases": signatures, "kem_negative_cases": kem_negatives, "upstream_kat_cases": upstream, "candidate_primitive_cases": candidate, "openssl_hkdf_cases": openssl_hkdf, "mlkem": bool(args.mlkem_source), "sanitizer": args.sanitize, "node_build": False}, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
