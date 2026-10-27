#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Package a clean development checkpoint with exact build provenance.

No official version assignment, signing, publication, installation or deployment.
Native dependency/installer qualification remains separate from this bundle.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True, help='New directory, never replaced')
    parser.add_argument('--qt-plugin-dir', type=Path, help='Native Qt plugin root for Windows development bundles')
    parser.add_argument('--smoke', action='store_true', help='Launch staged Qt offline on owned public-testnet data')
    args = parser.parse_args()
    if git('status', '--porcelain', '--untracked-files=no'):
        parser.error('Require a clean tracked source checkpoint')
    source, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    build = args.build_dir.resolve()
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if line.startswith(('CMAKE_CXX_COMPILER:', 'CMAKE_C_COMPILER:', 'CMAKE_BUILD_TYPE:', 'CMAKE_CXX_FLAGS:',
                            'CMAKE_CXX_FLAGS_RELEASE:', 'MERCATURA_PUBLIC_TESTNET_RELEASE:', 'MERCATURA_REQUIRE_PQ_TESTNET:', 'BUILD_GUI:',
                            'ENABLE_WALLET:', 'WITH_CUDA_MINING:', 'WITH_OPENCL_MINING:')):
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    bin_dir = build / 'bin'
    if (bin_dir / 'Release').is_dir():
        bin_dir /= 'Release'
    suffix = '.exe' if platform.system() == 'Windows' else ''
    daemon = bin_dir / ('mercaturad' + suffix)
    version = subprocess.check_output([str(daemon), '-version'], text=True, timeout=20).splitlines()[0]
    if 'p9p11-dev' not in version or source[:12] not in version or '-dirty' in version:
        parser.error('Binary must identify this exact clean development source checkpoint')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    output = args.output_dir.resolve()
    label = f'Mercatura-Core-p9p11-dev-{source[:12]}-{platform.system().lower()}-{platform.machine().lower()}'
    compilers = {}
    for language in ('C','CXX'):
        matches = sorted((build / 'CMakeFiles').glob('*/CMake' + language + 'Compiler.cmake'))
        if matches:
            content = matches[-1].read_text()
            for key in ('ID','VERSION','ARCHITECTURE_ID'):
                found = re.search(r'set\(CMAKE_' + language + '_COMPILER_' + key + r' "([^"\n]*)"\)',content)
                if found:
                    compilers[language + '_' + key] = found[1]
    manifest = {'channel': 'p9p11-dev', 'official_release': False, 'source_commit': source, 'source_tree': tree,
                'binary_version': version, 'platform': platform.platform(), 'configuration': cache, 'compilers': compilers,
                'suite': 1, 'protocol_version': 1, 'main_policy': 'required from first connection; no classical fallback',
                'testnet_profile': 'enforced-qualification' if cache.get('MERCATURA_REQUIRE_PQ_TESTNET') == 'ON' else 'activation-pending',
                'testnet_activation': 'pending founder approval',
                'testnet_genesis': 'b3fc707c9a89dc45d1cf6a837284b22ad2b6fd41b99af601079db9dcbeff6920',
                'dependency_scope': 'Native build bundle; clean-machine dependency and installer qualification is separate',
                'files': {}}
    with tempfile.TemporaryDirectory(prefix='pq-candidate-') as temp:
        stage = Path(temp) / label
        (stage / 'bin').mkdir(parents=True)
        for name in ('mercaturad', 'mercatura-cli', 'mercatura-qt', 'Mercatura-Qt'):
            path = bin_dir / (name + suffix)
            if path.is_file():
                shutil.copy2(path, stage / 'bin' / path.name)
        # Native vcpkg copies dependent DLLs beside the executable. Preserve
        # them, but do not pretend this replaces clean-machine installation.
        if suffix:
            for path in bin_dir.glob('*.dll'):
                shutil.copy2(path, stage / 'bin' / path.name)
            for name in ('platforms', 'styles', 'imageformats'):
                if (bin_dir / name).is_dir():
                    shutil.copytree(bin_dir / name, stage / 'bin' / name)
        if args.qt_plugin_dir:
            for name in ('platforms', 'styles', 'imageformats'):
                path = args.qt_plugin_dir / name
                if path.is_dir() and not (stage / 'bin' / name).exists():
                    shutil.copytree(path, stage / 'bin' / name)
        if args.smoke:
            qt = next((stage / 'bin' / (name + suffix) for name in ('mercatura-qt','Mercatura-Qt')
                       if (stage / 'bin' / (name + suffix)).exists()), None)
            if qt is None:
                raise RuntimeError('Qt smoke requires the staged native GUI binary')
            import sys
            subprocess.run([sys.executable, str(Path(__file__).with_name('smoke-pq-testnet-package.py')),
                            '--qt', str(qt), '--output', str(output / 'SMOKE.json')], check=True, timeout=110)
            manifest['offline_staged_qt_smoke'] = json.loads((output / 'SMOKE.json').read_text())
            if manifest['offline_staged_qt_smoke']['pqp2p']['build'] not in version:
                raise RuntimeError('Staged Qt and daemon build identifiers disagree')
        for name in ('COPYING', 'doc/design/mercatura-pq-transport-v1.md'):
            shutil.copy2(name, stage / Path(name).name)
        bundle = build / 'mercatura-macos-app.zip'
        if bundle.exists():
            shutil.copy2(bundle, stage / bundle.name)
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                manifest['files'][path.relative_to(stage).as_posix()] = {'bytes': path.stat().st_size, 'sha256': digest(path)}
        (stage / 'BUILD-PROVENANCE.json').write_text(json.dumps(manifest, indent=2) + '\n')
        package = output / (label + '.zip')
        with zipfile.ZipFile(package, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(stage.rglob('*')):
                if path.is_file():
                    archive.write(path, path.relative_to(stage.parent))
        with zipfile.ZipFile(package) as archive:
            if archive.testzip() is not None:
                raise RuntimeError('Package CRC validation failed')
            for name, expected in manifest['files'].items():
                if hashlib.sha256(archive.read(label + '/' + name)).hexdigest() != expected['sha256']:
                    raise RuntimeError('Package file hash validation failed')
    # git archive is deterministic for this exact tree and contains tracked
    # sources only. No datadirs, wallets, cookies or operational keys enter it.
    source_archive = output / f'Mercatura-Core-p9p11-dev-{source[:12]}-source.tar'
    subprocess.run(['git', 'archive', '--format=tar', '--prefix=Mercatura-Core/', '-o', str(source_archive), source], check=True)
    with tarfile.open(source_archive) as archive:
        if not archive.getmembers():
            raise RuntimeError('Empty source archive')
    manifest['artifacts'] = {path.name: {'sha256': digest(path), 'bytes': path.stat().st_size}
                             for path in (package, source_archive)}
    (output / 'BUILD-PROVENANCE.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'SHA256SUMS').write_text(''.join(f"{item['sha256']}  {name}\n" for name, item in manifest['artifacts'].items()))
    print(json.dumps({'source_commit': source, 'artifacts': manifest['artifacts']}))


if __name__ == '__main__':
    main()
