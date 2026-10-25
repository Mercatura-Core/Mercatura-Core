#!/usr/bin/env python3
"""Independent test-only PQ packet derivation using the existing Python FS ciphers.

--write deliberately updates the permanent fixture; default checks it unchanged.
Never imports the C++ PQ packet wrapper. All keys are public test material.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/functional'))
from test_framework.crypto.chacha20 import FSChaCha20
from test_framework.crypto.bip324_cipher import FSChaCha20Poly1305


def derive():
    candidate = json.loads((ROOT / 'src/test/data/pq_p2p_candidate1.json').read_text())
    keys = {row['label']: bytes.fromhex(row['output']) for row in candidate['expand']}
    h5 = bytes.fromhex(candidate['transcripts']['h5'])
    label = b'Mercatura/P2PPacket/v1'
    prefix = len(label).to_bytes(2, 'little') + label + h5
    rows = []
    for role, direction in [(0, 'i2r'), (1, 'r2i')]:
        length_key, payload_key = keys['app/' + direction + '/length'], keys['app/' + direction + '/payload']
        length_cipher, payload_cipher = FSChaCha20(length_key), FSChaCha20Poly1305(payload_key)
        digest = hashlib.sha3_384()
        packets = []
        for i in range(674):
            contents = b'\x12' + i.to_bytes(8, 'little')
            control = 128 if i % 7 == 0 else 0
            wire_length = length_cipher.crypt(len(contents).to_bytes(3, 'little'))
            aad = prefix + bytes([role]) + wire_length
            wire = wire_length + payload_cipher.encrypt(aad, bytes([control]) + contents)
            digest.update(wire)
            if i in [0, 1, 222, 223, 224, 225, 446, 447, 448, 449, 670, 671, 672, 673]:
                packets.append({'index': i, 'epoch': i // 224, 'counter': i % 224, 'contents': contents.hex(), 'control': control, 'wire_length': wire_length.hex(), 'aad': aad.hex(), 'wire': wire.hex()})
        invalid = []
        for name, control, contents in [('invalid_control', 1, b'\x12'), ('empty_ordinary', 0, b'')]:
            length = FSChaCha20(length_key).crypt(len(contents).to_bytes(3, 'little'))
            aad = prefix + bytes([role]) + length
            wire = length + FSChaCha20Poly1305(payload_key).encrypt(aad, bytes([control]) + contents)
            invalid.append({'name': name, 'wire': wire.hex()})
        too_large = FSChaCha20(length_key).crypt((4_000_014).to_bytes(3, 'little'))
        rows.append({'role': role, 'length_key': length_key.hex(), 'payload_key': payload_key.hex(), 'packets': packets, 'count': 674, 'all_packets_sha3_384': digest.hexdigest(), 'invalid_packets': invalid, 'oversized_wire_length': too_large.hex()})
    return {'status': 'Stage A public deterministic packet fixtures; no production keys', 'candidate_sha256': hashlib.sha256((ROOT / 'src/test/data/pq_p2p_candidate1.json').read_bytes()).hexdigest(), 'reference': 'Existing Python test_framework.crypto FSChaCha20 and FSChaCha20Poly1305, independent of C++ PQ wrapper', 'h5': h5.hex(), 'directions': rows}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    target = ROOT / 'src/test/data/pq_p2p_packets.json'
    data = json.dumps(derive(), indent=2) + '\n'
    if args.write:
        target.write_text(data)
    else:
        assert target.read_text() == data, 'packet fixtures differ; do not silently alter frozen cryptography'
    print('PASS: both directions, 1348 packets, three rekey boundaries each')
