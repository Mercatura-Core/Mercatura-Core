#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Assemble only a complete, same-source candidate. Never tag or publish."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tarfile
import zipfile

from qualify import require

ASSETS = ['Mercatura-Core-v0.1.2-testnet-win64.zip',
          'Mercatura-Core-v0.1.2-testnet-win64-setup.exe',
          'Mercatura-Core-v0.1.2-testnet-linux-x86_64.tar.gz',
          'Mercatura-Core-v0.1.2-testnet-macos-arm64.zip']


def main():
    root = Path(sys.argv[1])
    for platform in ['windows', 'linux', 'macos']:
        evidence = json.loads((root / f'{platform}-build.json').read_text())
        require(evidence['source_sha'] == os.environ['GITHUB_SHA'], 'Mixed source commits prohibited')
        require(evidence['platform'] == platform, 'Wrong platform evidence')
        gpu = platform != 'macos'
        for backend in ['WITH_CUDA_MINING', 'WITH_OPENCL_MINING']:
            require(evidence['cache'][backend] == ('ON' if gpu else 'OFF'), 'Missing required backend')
    sums = []
    for name in ASSETS:
        path = root / name
        require(path.is_file() and path.stat().st_size > 0, f'Missing distributable {name}')
        if name.endswith('.zip'):
            with zipfile.ZipFile(path) as archive:
                require(archive.testzip() is None, f'Corrupt {name}')
                names = archive.namelist()
                expected = ['Mercatura Core.app/Contents/MacOS/Mercatura-Qt'] if 'macos' in name else ['mercatura-qt.exe', 'mercaturad.exe', 'mercatura-cli.exe', 'platforms/qwindows.dll', 'build.json']
                require(all(entry in names for entry in expected), f'Incomplete package {name}')
        elif name.endswith('.tar.gz'):
            with tarfile.open(path) as archive:
                names = archive.getnames()
                for entry in ['bin/mercatura-qt', 'bin/mercaturad', 'bin/mercatura-cli', 'build.json', 'lib/qt/plugins/platforms/libqxcb.so']:
                    require(f'Mercatura-Core-v0.1.2-testnet/{entry}' in names, f'Missing {entry}')
        else:
            require(path.read_bytes()[:2] == b'MZ', 'Installer must be an actual executable')
        with path.open("rb") as stream:
            sums.append(f'{hashlib.file_digest(stream, "sha256").hexdigest()}  {name}')
    (root / 'SHA256SUMS').write_text('\n'.join(sums) + '\n')
    source = Path('doc/release-notes/release-notes-0.1.2-testnet.md')
    (root / 'PROPOSED-PRERELEASE-NOTES.md').write_text(source.read_text())
    (root / 'SOURCE-COMMIT.txt').write_text(os.environ['GITHUB_SHA'] + '\n')
    print('\n'.join(sums))


if __name__ == '__main__':
    main()
