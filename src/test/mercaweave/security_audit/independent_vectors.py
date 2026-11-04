#!/usr/bin/env python3
"""WO08 separate parser/serializer/hash route; imports no project/oracle code.

Reconstructs groups from serialized actual C++ transactions. Python hashlib
and struct implement the locked bytes independently; this is not an independent
ML-DSA implementation or an external cryptographic audit.
"""

import argparse
import hashlib
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[4]


def compact(n):
    if n < 253:
        return bytes([n])
    code, fmt = (
        (253, "H") if n <= 65535 else (254, "I") if n <= 0xFFFFFFFF else (255, "Q")
    )
    return bytes([code]) + struct.pack("<" + fmt, n)


class Cursor:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def take(self, n):
        assert 0 <= n <= len(self.data) - self.offset
        value = self.data[self.offset : self.offset + n]
        self.offset += n
        return value

    def count(self):
        lead = self.take(1)[0]
        if lead < 253:
            return lead
        value = int.from_bytes(self.take({253: 2, 254: 4, 255: 8}[lead]), "little")
        assert value >= {253: 253, 254: 65536, 255: 4294967296}[lead]
        return value

    def vector(self):
        return self.take(self.count())

    def done(self):
        assert self.offset == len(self.data)


def tagged(suffix, data):
    tag = ("Mercatura/PQGroupAuth/v1/" + suffix).encode("ascii")
    return hashlib.sha3_384(compact(len(tag)) + tag + data).digest()


def commitment(genesis, key, nonce):
    tag = hashlib.sha256(b"Mercatura/PQGroupKey/v1").digest()
    return hashlib.sha256(tag + tag + genesis + key + nonce).digest()


def calculate(wire, spent, genesis):
    reader = Cursor(wire)
    version = reader.take(4)
    assert reader.take(2) == b"\x00\x01"
    n = reader.count()
    prev = []
    sequences = []
    for _ in range(n):
        prev.append(reader.take(36))
        assert reader.vector() == b""
        sequences.append(reader.take(4))
    m = reader.count()
    outputs = []
    for _ in range(m):
        amount = reader.take(8)
        script = reader.vector()
        outputs.append(amount + compact(len(script)) + script)
    groups = {}
    keys = set()
    for i in range(n):
        witness = [reader.vector() for _ in range(reader.count())]
        if len(witness) == 3:
            sig, key, nonce = witness
            assert (len(sig), len(key), len(nonce)) == (3309, 1952, 32)
            assert key not in keys
            keys.add(key)
            anchor = i
            groups[anchor] = [key, []]
        else:
            nonce, ref = witness
            assert (len(nonce), len(ref)) == (32, 4)
            anchor = int.from_bytes(ref, "little")
            assert anchor < i and anchor in groups
        groups[anchor][1].append((i, nonce))
    locktime = reader.take(4)
    reader.done()
    source = Cursor(spent)
    assert source.count() == n
    amounts = []
    scripts = []
    for _ in range(n):
        amounts.append(source.take(8))
        scripts.append(source.vector())
    source.done()
    manifest = compact(len(groups))
    for anchor, (key, members) in groups.items():
        manifest += struct.pack("<I", anchor) + key + compact(len(members))
        for i, nonce in members:
            assert scripts[i] == b"\x53\x20" + commitment(genesis, key, nonce)
            manifest += struct.pack("<I", i) + nonce
    components = [
        b"".join(prev),
        b"".join(amounts),
        b"".join(compact(len(s)) + s for s in scripts),
        b"".join(sequences),
        b"".join(outputs),
    ]
    hashes = [
        tagged(tag, value)
        for tag, value in zip(
            ["Prevouts", "Amounts", "ScriptPubKeys", "Sequences", "Outputs"], components
        )
    ]
    body = version + locktime + struct.pack("<QQ", n, m) + genesis + b"".join(hashes)
    txhash = tagged("Tx", body)
    mh = tagged("Manifest", manifest)
    digests = [
        tagged("Final", b"\x01" + genesis + txhash + mh + struct.pack("<I", a) + key)
        for a, (key, _) in groups.items()
    ]
    return dict(
        hashes=[h.hex() for h in hashes + [txhash, mh]],
        manifest=manifest.hex(),
        digests=[d.hex() for d in digests],
        inputs=n,
        groups=len(groups),
    )


def locked_fixture():
    genesis = bytes(range(32))
    key = bytes(i % 256 for i in range(1952))
    nonces = [b"\x10" * 32, b"\x20" * 32]
    scripts = [b"\x53\x20" + commitment(genesis, key, n) for n in nonces]
    wire = struct.pack("<I", 2) + b"\x00\x01\x02"
    for fill, index, seq in [(0xA1, 1, 0xFFFFFFFE), (0xB2, 2, 0xFFFFFFFD)]:
        wire += (
            bytes([fill]) * 32
            + struct.pack("<I", index)
            + b"\x00"
            + struct.pack("<I", seq)
        )
    wire += b"\x01" + struct.pack("<q", 250) + b"\x22\x52\x20" + b"\xcc" * 32
    for stack in [[bytes(3309), key, nonces[0]], [nonces[1], bytes(4)]]:
        wire += compact(len(stack)) + b"".join(
            compact(len(item)) + item for item in stack
        )
    wire += bytes(4)
    spent = b"\x02" + b"".join(
        struct.pack("<q", amount) + compact(len(script)) + script
        for amount, script in zip([100, 200], scripts)
    )
    actual = calculate(wire, spent, genesis)
    expected = json.loads((ROOT / "src/test/mercaweave/vectors.json").read_text())[
        "synthetic"
    ]
    for name, value in zip(
        ["Hprev", "Hamount", "Hscript", "Hseq", "Hout", "Htx", "Hmanifest"],
        actual["hashes"],
    ):
        assert expected[name] == value
    assert (
        actual["digests"][0]
        == "ecc046c62d0a1a8fad53fad0109cf27ecd98e6c03ac85a2a3485528ba7f0c1302fbe21c7a0feebd87c52fdfd6c76aab4"
    )
    assert expected["manifest"] == actual["manifest"]
    assert (
        scripts[0][2:].hex()
        == "e7619d364b194cbfac8edca35b48c1b8ef4f4f255943251173ece8b3be19132e"
    )
    assert (
        scripts[1][2:].hex()
        == "bb47c62b9598812333b43e7ac4d9093be831a6dc01a1996cd2277e36b95c92e7"
    )
    return dict(
        ten_locked_values_matched=True,
        manifest_bytes=2030,
        body_bytes=296,
        final_bytes=2085,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logs", nargs="+", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    streams = []
    results = []
    for log in args.logs:
        lines = [
            line.removeprefix("WO08_VECTOR ")
            for line in log.read_text().splitlines()
            if line.startswith("WO08_VECTOR ")
        ]
        assert len(lines) == 4
        canonical = ("\n".join(lines) + "\n").encode()
        streams.append(canonical)
        for line in lines:
            record = json.loads(line)
            actual = calculate(
                bytes.fromhex(record["wire"]),
                bytes.fromhex(record["spent"]),
                bytes.fromhex(record["genesis"]),
            )
            assert all(actual[key] == record[key] for key in actual)
        results.append(
            dict(log=str(log), vectors=4, sha256=hashlib.sha256(canonical).hexdigest())
        )
    assert all(stream == streams[0] for stream in streams)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(
            dict(
                passed=True,
                locked=locked_fixture(),
                configurations=results,
                byte_identical=True,
                independent_crypto_implementation=False,
            ),
            indent=2,
        )
        + "\n"
    )
    args.output.with_suffix(".jsonl").write_bytes(streams[0])


if __name__ == "__main__":
    main()
