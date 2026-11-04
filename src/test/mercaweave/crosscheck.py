#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Check C++ consensus bytes independently with Python's standard library.

This verifies serialization, hashes and derivation, not ML-DSA mathematics.
ML-DSA signatures are actually verified by the pinned C wrapper in C++ tests.
"""

import hmac
import json
from pathlib import Path
import struct
import subprocess
import sys

from reference import EXPECTED, commitment, compact, pqh


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def read(self, n):
        if n < 0 or self.pos + n > len(self.data):
            raise ValueError("truncated bytes")
        result = self.data[self.pos : self.pos + n]
        self.pos += n
        return result

    def count(self):
        n = self.read(1)[0]
        if n < 253:
            return n
        width = {253: 2, 254: 4, 255: 8}[n]
        val = int.from_bytes(self.read(width), "little")
        if val < {253: 253, 254: 65536, 255: 4294967296}[n]:
            raise ValueError("noncanonical CompactSize")
        return val

    def varbytes(self):
        return self.read(self.count())


def parse(wire):
    r = Reader(wire)
    version = r.read(4)
    n = r.count()
    has_witness = n == 0
    if has_witness:
        assert r.read(1) == b"\x01"
        n = r.count()
    inputs = []
    for _ in range(n):
        inputs.append(
            {"prevout": r.read(36), "scriptSig": r.varbytes(), "sequence": r.read(4)}
        )
    outputs = []
    for _ in range(r.count()):
        amount, script = r.read(8), r.varbytes()
        outputs.append(amount + compact(len(script)) + script)
    if has_witness:
        for txin in inputs:
            txin["witness"] = [r.varbytes() for _ in range(r.count())]
    locktime = r.read(4)
    assert r.pos == len(wire)
    return version, inputs, outputs, locktime


def analyze(case):
    g = bytes.fromhex(case["genesis"])
    version, inputs, outputs, locktime = parse(bytes.fromhex(case["wire"]))
    spent = case["spent"]
    assert len(inputs) == len(spent)
    groups = {}
    for i, (txin, prev) in enumerate(zip(inputs, spent)):
        script = bytes.fromhex(prev["script"])
        if script[:2] != b"\x53\x20" or len(script) != 34:
            continue
        assert not txin["scriptSig"]
        witness = txin["witness"]
        if len(witness) == 3:
            sig, pk, nonce = witness
            assert (len(sig), len(pk), len(nonce)) == (3309, 1952, 32)
            assert all(group["pk"] != pk for group in groups.values())
            groups[i] = {"pk": pk, "members": [(i, nonce)]}
        else:
            assert len(witness) == 2
            nonce, reference = witness
            assert len(nonce) == 32 and len(reference) == 4
            anchor = int.from_bytes(reference, "little")
            assert anchor < i and anchor in groups
            groups[anchor]["members"].append((i, nonce))
            pk = groups[anchor]["pk"]
        assert commitment(g, pk, nonce) == script[2:]
    components = [
        b"".join(txin["prevout"] for txin in inputs),
        b"".join(struct.pack("<q", p["amount"]) for p in spent),
        b"".join(
            compact(len(bytes.fromhex(p["script"]))) + bytes.fromhex(p["script"])
            for p in spent
        ),
        b"".join(txin["sequence"] for txin in inputs),
        b"".join(outputs),
    ]
    names = ["Hprev", "Hamount", "Hscript", "Hseq", "Hout"]
    tags = ["Prevouts", "Amounts", "ScriptPubKeys", "Sequences", "Outputs"]
    hashes = [
        pqh("Mercatura/PQGroupAuth/v1/" + tag, data)
        for tag, data in zip(tags, components)
    ]
    htx = pqh(
        "Mercatura/PQGroupAuth/v1/Tx",
        version
        + locktime
        + struct.pack("<QQ", len(inputs), len(outputs))
        + g
        + b"".join(hashes),
    )
    manifest = compact(len(groups))
    for anchor, group in groups.items():
        manifest += (
            struct.pack("<I", anchor) + group["pk"] + compact(len(group["members"]))
        )
        manifest += b"".join(
            struct.pack("<I", i) + nonce for i, nonce in group["members"]
        )
    hm = pqh("Mercatura/PQGroupAuth/v1/Manifest", manifest)
    expected = dict(zip(names, [h.hex() for h in hashes]))
    expected.update(Htx=htx.hex(), Hmanifest=hm.hex(), manifest=manifest.hex())
    expected["digests"] = [
        pqh(
            "Mercatura/PQGroupAuth/v1/Final",
            b"\x01" + g + htx + hm + struct.pack("<I", a) + gr["pk"],
        ).hex()
        for a, gr in groups.items()
    ]
    assert expected == case["hashes"]
    # Independently parse the corresponding genuine-signature v2 wire image.
    v2version, v2in, v2out, v2locktime = parse(bytes.fromhex(case["v2_wire"]))
    assert v2version == version and v2locktime == locktime and v2out == outputs
    assert len(v2in) == len(inputs) - sum(
        bytes.fromhex(p["script"])[:2] != b"\x53\x20" for p in spent
    )
    assert all([len(item) for item in txin["witness"]] == [3309, 1952] for txin in v2in)


def check(vectors):
    synthetic = vectors["synthetic"]
    for name in ("Hprev", "Hamount", "Hscript", "Hseq", "Hout", "Htx", "Hmanifest"):
        assert synthetic[name] == EXPECTED[name], name
    assert synthetic["digests"] == [EXPECTED["Dgroup"]]
    assert len(bytes.fromhex(synthetic["manifest"])) == 2030
    for case in vectors["cases"]:
        analyze(case)
    master, genesis = bytes(range(32, 64)), bytes(range(32))
    prk = hmac.digest(b"Mercatura/PQGroupWalletMaster/v1", master, "sha3_384")

    def expand(info):
        return hmac.digest(prk, info + b"\x01", "sha3_384")[:32]

    for purpose, vector in enumerate(vectors["derivation"]):
        path = genesis + struct.pack("<IBI", 7, purpose, 9)
        seed = expand(b"Mercatura/PQGroupMLDSA65Key/v1\x00" + path)
        nonce = expand(
            b"Mercatura/PQGroupAddrNonce/v1\x00" + path + struct.pack("<I", 11)
        )
        assert vector["seed"] == seed.hex()
        assert vector["nonce"] == nonce.hex()
        assert (
            vector["commitment"]
            == commitment(genesis, bytes.fromhex(vector["pk"]), nonce).hex()
        )
    print(
        "PASS cross-language: 8 locked hash/digest values, 4 transaction/manifest cases, 3 derivation purposes"
    )


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: crosscheck.py EXECUTABLE [--save PATH]")
    vectors = json.loads(subprocess.check_output([sys.argv[1], "--vectors"]))
    check(vectors)
    permanent = Path(__file__).with_name("vectors.json")
    if permanent.exists():
        assert json.loads(permanent.read_text()) == vectors, (
            "permanent deterministic vectors differ"
        )
    if len(sys.argv) == 4 and sys.argv[2] == "--save":
        path = Path(sys.argv[3])
        content = json.dumps(vectors, indent=2) + "\n"
        if path.exists():
            assert json.loads(path.read_text()) == vectors, "permanent vectors differ"
        else:
            path.write_text(content)
