#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Bundle native Ubuntu 24.04 dependencies, excluding glibc and GPU drivers."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

from qualify import require, run

# These are supplied by the supported OS/graphics driver, never by this wallet.
SYSTEM = re.compile(r'^(ld-linux|lib(c\.|m\.|pthread\.|dl\.|rt\.|resolv\.|util\.|nss_|anl\.|BrokenLocale\.|GL|EGL|GLES|glapi|drm|gbm|cuda\.|nvidia|OpenCL\.))')


def package_owner(source):
    # ldd can report /lib while merged-/usr package records use /usr/lib.
    # Try both spellings, retaining support for records using the original path.
    for path in dict.fromkeys([source, source.resolve()]):
        result = subprocess.run(['dpkg-query', '-S', str(path)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if result.returncode == 0:
            return result.stdout.split(': ', 1)[0].strip()
    raise RuntimeError(f'Cannot identify the distribution package for {source}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    stage = args.output / 'Mercatura-Core-v0.1.2-testnet'
    require(not stage.exists(), 'Package staging must start empty')
    (stage / 'bin').mkdir(parents=True)
    (stage / 'lib').mkdir()
    licenses = stage / 'licenses'
    licenses.mkdir()
    shutil.copy('COPYING', licenses / 'Mercatura-COPYING')
    shutil.copy('doc/release-notes/release-notes-0.1.2-testnet.md', stage / 'RELEASE-NOTES.md')
    shutil.copy('doc/release-v0.1.2-testnet.md', stage / 'BUILD-AND-BETA.md')
    shutil.copy(args.output / 'linux-build.json', stage / 'build.json')
    cuda = Path('/usr/local/cuda-12.8')
    license_files = list(cuda.glob('**/EULA*')) + list(cuda.glob('**/LICENSE*'))
    require(license_files, 'CUDA redistribution notices missing')
    for index, path in enumerate(license_files):
        if path.is_file():
            shutil.copy(path, licenses / f'CUDA-{index}-{path.name}')
    pending = []
    for name in ['mercatura-qt', 'mercaturad', 'mercatura-cli', 'mercatura-gpu-check']:
        dest = stage / 'bin' / name
        shutil.copy(build / 'bin' / name, dest)
        pending.append(dest)
    plugins = Path(run('qtpaths6', '--query', 'QT_INSTALL_PLUGINS').strip())
    for group in ['platforms', 'imageformats']:
        for plugin in (plugins / group).glob('*.so'):
            dest = stage / 'lib/qt/plugins' / group / plugin.name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(plugin, dest)
            pending.append(dest)
    require((stage / 'lib/qt/plugins/platforms/libqxcb.so').is_file(), 'Missing XCB Qt platform')
    (stage / 'bin/qt.conf').write_text('[Paths]\nPlugins=../lib/qt/plugins\n')
    packages = set()
    copied = set()
    while pending:
        binary = pending.pop()
        deps = run('ldd', str(binary))
        require('not found' not in deps, deps)
        for match in re.finditer(r'=> (/\S+)', deps):
            source = Path(match[1])
            if SYSTEM.match(source.name) or source.name in copied:
                continue
            copied.add(source.name)
            dest = stage / 'lib' / source.name
            shutil.copy(source, dest)
            pending.append(dest)
            owner = package_owner(source)
            packages.add(owner)
    for package in sorted(packages):
        copyright_file = Path('/usr/share/doc') / package.split(':')[0] / 'copyright'
        require(copyright_file.is_file(), f'Missing license for {package}')
        shutil.copy(copyright_file, licenses / (package.replace(':', '-') + '-copyright'))
    shutil.copytree('/usr/share/common-licenses', licenses / 'common-licenses')
    (stage / 'DEPENDENCIES.txt').write_text(run('dpkg-query', '-W', *sorted(packages)))
    for binary in (stage / 'bin').iterdir():
        if binary.name != 'qt.conf':
            subprocess.check_call(['patchelf', '--set-rpath', '$ORIGIN/../lib', str(binary)])
    for library in (stage / 'lib').glob('*.so*'):
        subprocess.check_call(['patchelf', '--set-rpath', '$ORIGIN', str(library)])
    for plugin in (stage / 'lib/qt/plugins').glob('*/*.so'):
        subprocess.check_call(['patchelf', '--set-rpath', '$ORIGIN/../../..', str(plugin)])
    for binary in (stage / 'bin').iterdir():
        if binary.name != 'qt.conf':
            require('not found' not in run('ldd', str(binary)), f'Missing packaged dependency: {binary}')
    for name in ['mercaturad', 'mercatura-cli']:
        require('v0.1.2' in run(str((stage / 'bin' / name).resolve()), '-version'), 'Package version failure')
    archive = args.output / 'Mercatura-Core-v0.1.2-testnet-linux-x86_64.tar.gz'
    with tarfile.open(archive, 'w:gz') as output:
        output.add(stage, arcname=stage.name)
    with tarfile.open(archive) as source:
        (args.output / 'linux-contents.txt').write_text('\n'.join(source.getnames()) + '\n')
    shutil.rmtree(stage)


if __name__ == '__main__':
    main()
