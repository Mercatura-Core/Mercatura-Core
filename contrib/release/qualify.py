#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Fail-closed v0.1.2 build evidence; no physical GPU or release claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def cache_values(path):
    values = {}
    for line in path.read_text().splitlines():
        match = re.fullmatch(r"([^#/:][^:]*):[^=]+=(.*)", line)
        if match:
            values[match[1]] = match[2]
    return values


def architecture(path, platform):
    data = path.read_bytes()
    if platform == 'windows':
        require(data[:2] == b'MZ', f'{path}: not PE')
        offset = struct.unpack_from('<I', data, 0x3c)[0]
        require(data[offset:offset + 4] == b'PE\0\0', f'{path}: invalid PE')
        require(struct.unpack_from('<H', data, offset + 4)[0] == 0x8664, f'{path}: not x64')
    elif platform == 'linux':
        require(data[:6] == b'\x7fELF\x02\x01', f'{path}: not ELF64 little endian')
        require(struct.unpack_from('<H', data, 18)[0] == 62, f'{path}: not x86_64')
    else:
        require(run('lipo', '-archs', str(path)).strip() == 'arm64', f'{path}: not ARM64 only')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('platform', choices=['windows', 'linux', 'macos'])
    parser.add_argument('build', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    values = cache_values(build / 'CMakeCache.txt')
    gpu = args.platform != 'macos'
    expected = {name: 'ON' for name in ['BUILD_GUI', 'BUILD_DAEMON', 'BUILD_CLI',
                'ENABLE_WALLET', 'BUILD_TESTS', 'BUILD_GUI_TESTS', 'WITH_QRENCODE',
                'MERCATURA_PUBLIC_TESTNET_RELEASE', 'CMAKE_COMPILE_WARNING_AS_ERROR']}
    expected.update(WITH_CUDA_MINING='ON' if gpu else 'OFF', WITH_OPENCL_MINING='ON' if gpu else 'OFF')
    for name, value in expected.items():
        require(values.get(name) == value, f'{name}: expected {value}, got {values.get(name)}')
    sha = run('git', 'rev-parse', 'HEAD').strip()
    require(re.fullmatch('[0-9a-f]{40}', sha), 'Missing full source SHA')
    require(not run('git', 'status', '--porcelain', '--untracked-files=no'), 'Dirty source tree')
    require(sha == os.environ['GITHUB_SHA'], 'Build must use the workflow source SHA')
    suffix = '.exe' if args.platform == 'windows' else ''
    binaries = build / 'bin' / ('Release' if args.platform == 'windows' else '')
    evidence = {'source_sha': sha, 'platform': args.platform, 'cache': expected,
                'runner': os.environ.get('ImageOS'), 'image_version': os.environ.get('ImageVersion'),
                'versions': {}, 'binaries': {}, 'gpu_runtime': 'not applicable (CPU-only)'}
    for name in ['mercatura-qt', 'mercaturad', 'mercatura-cli']:
        binary = binaries / (name + suffix)
        architecture(binary, args.platform)
        evidence['binaries'][binary.name] = hashlib.sha256(binary.read_bytes()).hexdigest()
        if name != 'mercatura-qt':
            version = run(str(binary), '-version')
            require('v0.1.2' in version, f'{name}: incorrect version: {version}')
            evidence['versions'][name] = version.splitlines()[0]
    compiler_files = list((build / 'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    require(len(compiler_files) == 1, 'Ambiguous compiler provenance')
    evidence['compiler'] = compiler_files[0].read_text()
    if gpu:
        require(values.get('CMAKE_CUDA_ARCHITECTURES') == '75-real;75-virtual;86-real', 'Unexpected CUDA architectures')
        nvcc = values['CMAKE_CUDA_COMPILER']
        evidence['versions']['cuda'] = run(nvcc, '--version')
        require('release 12.8' in evidence['versions']['cuda'], 'CUDA 12.8 required')
        cuobjdump = str(Path(nvcc).with_name('cuobjdump' + suffix))
        for name in ['mercatura-qt', 'mercaturad']:
            binary = binaries / (name + suffix)
            if args.platform == 'windows':
                symbols = binary.with_suffix('.map').read_text(errors='replace')
                require(values.get('CMAKE_GENERATOR') == 'Visual Studio 17 2022', 'VS2022 required')
            else:
                symbols = run('nm', '--defined-only', '-C', str(binary))
            for symbol in ['mining::CreateCudaBackend', 'mining::CreateOpenClBackend']:
                # MSVC map symbols are decorated, unlike nm -C output.
                require(symbol.split('::')[1] in symbols, f'{name}: missing definition {symbol}')
            elf = run(cuobjdump, '--list-elf', str(binary))
            ptx = run(cuobjdump, '--list-ptx', str(binary))
            require('sm_75' in elf and 'sm_86' in elf and 'sm_75' in ptx, f'{name}: missing CUDA cubins/PTX')
            evidence[name + '_cuda_images'] = elf + ptx
        checker = subprocess.run([str(binaries / ('mercatura-gpu-check' + suffix)), '--list'],
                                 text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        require(checker.returncode in [0, 77], checker.stdout)
        require('not compiled' not in checker.stdout, checker.stdout)
        evidence['gpu_runtime'] = {'exit_code': checker.returncode, 'output': checker.stdout,
                                   'physical_parity': 'NOT TESTED; exit 77 is a hardware skip'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
