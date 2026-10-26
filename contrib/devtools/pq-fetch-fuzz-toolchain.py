#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Extract a Debian Clang 19 fuzz toolchain locally; never install system packages.

Uses the normal HTTPS proxy and Debian package index, checks each downloaded
package's SHA-256, and leaves package/version records for the review evidence.
This helper is for the Debian 13 x86_64 qualification host, not node operation.
"""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--index', type=Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    index = args.index or args.directory / 'Packages.xz'
    if args.index is None:
        subprocess.run(['curl', '-fsSL', '--max-time', '120',
                        'https://deb.debian.org/debian/dists/trixie/main/binary-amd64/Packages.xz',
                        '-o', str(index)], check=True)
    names = {'clang-19', 'libclang-cpp19', 'libclang1-19', 'libclang-common-19-dev',
             'libclang-rt-19-dev', 'llvm-19-linker-tools'}
    records = []
    for block in lzma.decompress(index.read_bytes()).decode().split('\n\n'):
        fields = dict(line.split(': ', 1) for line in block.splitlines()
                      if line and not line[0].isspace() and ': ' in line)
        if fields.get('Package') not in names:
            continue
        target = args.directory / Path(fields['Filename']).name
        subprocess.run(['curl', '-fsSL', '--max-time', '120',
                        'https://deb.debian.org/debian/' + fields['Filename'],
                        '-o', str(target)], check=True)
        if hashlib.sha256(target.read_bytes()).hexdigest() != fields['SHA256']:
            raise RuntimeError('Package checksum mismatch: ' + fields['Package'])
        subprocess.run(['dpkg-deb', '-x', str(target), str(args.directory)], check=True)
        record = {field: fields[field] for field in ['Package', 'Version', 'SHA256', 'Filename']}
        records.append(record)
        print(fields['Package'], fields['Version'], flush=True)
    if {record['Package'] for record in records} != names:
        raise RuntimeError('Incomplete toolchain package set')
    (args.directory / 'package-records.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
