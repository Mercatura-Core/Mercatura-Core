#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Negative release gates must reject plausible but incomplete candidates."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from qualify import architecture, cache_values


class ReleaseGates(unittest.TestCase):
    def test_cache_requires_explicit_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory) / 'CMakeCache.txt'
            cache.write_text('// WITH_CUDA_MINING:BOOL=ON\nWITH_CUDA_MINING:BOOL=OFF\nWITH_OPENCL_MINING:BOOL=ON\n')
            self.assertEqual(cache_values(cache)['WITH_CUDA_MINING'], 'OFF')
            self.assertNotIn('BUILD_GUI', cache_values(cache))

    def test_architecture_rejects_wrong_platform(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'binary'
            data = bytearray(64)
            data[:6] = b'\x7fELF\x02\x01'
            struct.pack_into('<H', data, 18, 62)
            binary.write_bytes(data)
            architecture(binary, 'linux')
            with self.assertRaises(RuntimeError):
                architecture(binary, 'windows')
            struct.pack_into('<H', data, 18, 183)  # ARM64 is not Linux x86_64.
            binary.write_bytes(data)
            with self.assertRaises(RuntimeError):
                architecture(binary, 'linux')

    def assemble(self, root):
        return subprocess.run([sys.executable, str(Path(__file__).with_name('assemble.py')), str(root)],
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)

    def manifests(self, root, sha):
        for platform in ['windows', 'linux', 'macos']:
            gpu = platform != 'macos'
            data = {'source_sha': sha, 'platform': platform,
                    'cache': {name: 'ON' if gpu else 'OFF' for name in ['WITH_CUDA_MINING', 'WITH_OPENCL_MINING']}}
            (root / f'{platform}-build.json').write_text(json.dumps(data))

    def test_mixed_source_rejected(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, GITHUB_SHA='a' * 40):
            root = Path(directory)
            self.manifests(root, 'b' * 40)
            result = self.assemble(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Mixed source commits', result.stdout)
            self.assertFalse((root / 'SHA256SUMS').exists())

    def test_cpu_only_gpu_candidate_rejected(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, GITHUB_SHA='a' * 40):
            root = Path(directory)
            self.manifests(root, 'a' * 40)
            path = root / 'windows-build.json'
            data = json.loads(path.read_text())
            data['cache']['WITH_CUDA_MINING'] = 'OFF'
            path.write_text(json.dumps(data))
            result = self.assemble(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Missing required backend', result.stdout)
            self.assertFalse((root / 'SHA256SUMS').exists())

    def test_missing_distributable_rejected(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, GITHUB_SHA='a' * 40):
            root = Path(directory)
            self.manifests(root, 'a' * 40)
            result = self.assemble(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Missing distributable', result.stdout)
            self.assertFalse((root / 'SHA256SUMS').exists())


if __name__ == '__main__':
    unittest.main()
