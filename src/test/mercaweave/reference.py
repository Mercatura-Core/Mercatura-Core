#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Independent standard-library oracle for the locked MercaWeave v0.1 fixture.

The patterned public key is structural data, not a valid signing fixture.
"""
import hashlib
import json
import struct
import sys


def compact(n):
    if not 0 <= n <= 0xffffffffffffffff:
        raise ValueError("CompactSize range")
    if n < 253:
        return bytes([n])
    if n <= 0xffff:
        return b"\xfd" + struct.pack("<H", n)
    if n <= 0xffffffff:
        return b"\xfe" + struct.pack("<I", n)
    return b"\xff" + struct.pack("<Q", n)


def pqh(tag, data):
    tag = tag.encode("ascii")
    return hashlib.sha3_384(compact(len(tag)) + tag + data).digest()


def commitment(genesis, public_key, nonce):
    if (len(genesis), len(public_key), len(nonce)) != (32, 1952, 32):
        raise ValueError("commitment lengths")
    tag = hashlib.sha256(b"Mercatura/PQGroupKey/v1").digest()
    return hashlib.sha256(tag + tag + genesis + public_key + nonce).digest()


EXPECTED = {
    "C0": "e7619d364b194cbfac8edca35b48c1b8ef4f4f255943251173ece8b3be19132e",
    "C1": "bb47c62b9598812333b43e7ac4d9093be831a6dc01a1996cd2277e36b95c92e7",
    "Hprev": "c3a2622d72e41ba9ca4db477380f79d1695892c1aa58da363f619472458f1bae74dc68e3f35ffc1c4a25d3171ca3f8ed",
    "Hamount": "46f69dddebc597d8a1ce461066bbf0787f0991499407edbe30f4b95320013c1ee87c9dad260cd264f381d25a8c3e61ff",
    "Hscript": "8e19629094b3008c0402b10bdd025b52d2687f268ec3cb52717ac439e037699ab5b5641037b32b7a83f0b17bb09a7c05",
    "Hseq": "893f60c283b397319de21743450cea825d21de5b13e84d2b2c79bded4ee6638714141ce57790cb435055faee21f7a20c",
    "Hout": "c388bbff0ca9c57591df4ca125e192887f621a9a8a68ce685c743e247daac2780f3bebafca8456bf83a69fc76d6be119",
    "Htx": "7fb38f5cb14b93df70207f52f1fd508a6b890583ad79bf67a6c7e17009e450562a68f5361eb03ff0b05ba4492560f262",
    "Hmanifest": "56fe8b56240f7840e2f8eadc2cf1b05328d9a2922ab2df36233e8df42510ce31336eb5168094477823241d113b188ef8",
    "Dgroup": "ecc046c62d0a1a8fad53fad0109cf27ecd98e6c03ac85a2a3485528ba7f0c1302fbe21c7a0feebd87c52fdfd6c76aab4",
}


def fixture():
    g = bytes(range(32))
    pk = bytes(range(256)) * 7 + bytes(range(160))
    nonces = [b"\x10" * 32, b"\x20" * 32]
    c = [commitment(g, pk, nonce) for nonce in nonces]
    scripts = [b"\x53\x20" + x for x in c]
    components = [
        b"\xa1" * 32 + struct.pack("<I", 1) + b"\xb2" * 32 + struct.pack("<I", 2),
        struct.pack("<qq", 100, 200),
        b"".join(compact(len(s)) + s for s in scripts),
        struct.pack("<II", 0xfffffffe, 0xfffffffd),
        struct.pack("<q", 250) + compact(34) + b"\x52\x20" + b"\xcc" * 32,
    ]
    suffixes = ["Prevouts", "Amounts", "ScriptPubKeys", "Sequences", "Outputs"]
    hashes = [pqh("Mercatura/PQGroupAuth/v1/" + t, v) for t, v in zip(suffixes, components)]
    body = struct.pack("<IIQQ", 2, 0, 2, 1) + g + b"".join(hashes)
    manifest = compact(1) + struct.pack("<I", 0) + pk + compact(2)
    manifest += b"".join(struct.pack("<I", i) + nonce for i, nonce in enumerate(nonces))
    htx = pqh("Mercatura/PQGroupAuth/v1/Tx", body)
    hm = pqh("Mercatura/PQGroupAuth/v1/Manifest", manifest)
    final = b"\x01" + g + htx + hm + struct.pack("<I", 0) + pk
    values = dict(zip(["C0", "C1", "Hprev", "Hamount", "Hscript", "Hseq", "Hout", "Htx", "Hmanifest", "Dgroup"],
                      c + hashes + [htx, hm, pqh("Mercatura/PQGroupAuth/v1/Final", final)]))
    lengths = {"manifest": len(manifest), "body": len(body), "final": len(final)}
    assert lengths == {"manifest": 2030, "body": 296, "final": 2085}
    failures = []
    for name, actual in values.items():
        expected = bytes.fromhex(EXPECTED[name])
        if actual != expected:
            first = next(i for i, (a, b) in enumerate(zip(actual, expected)) if a != b)
            failures.append({"name": name, "first_differing_byte_zero_based": first,
                             "expected": expected.hex(), "actual": actual.hex()})
    result = {"lengths": lengths, "actual": {k: v.hex() for k, v in values.items()}, "failures": failures}
    print(json.dumps(result, indent=2))
    if failures:
        sys.exit(1)


if __name__ == "__main__":
    fixture()
