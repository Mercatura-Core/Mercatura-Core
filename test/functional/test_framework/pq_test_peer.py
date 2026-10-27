#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Owned loopback message injector using the actual native authenticated adapter.

Only test builds contain this executable. Product daemons never accept the
classical envelope used between the Python injector and this local fixture.
"""
from pathlib import Path
import os
import subprocess
import time

from test_framework.util import p2p_port


class PQP2PBridge:
    def __init__(self, test, node, chain):
        build = Path(test.config['environment']['BUILDDIR']) / 'bin'
        filename = 'pq-test-peer.exe' if os.name == 'nt' else 'pq-test-peer'
        # Native MSVC is a multi-configuration build; other toolchains use bin/.
        candidates = [build / 'Release' / filename, build / filename]
        binary = next((path for path in candidates if path.is_file()), None)
        if binary is None:
            raise RuntimeError('Build the test-only pq_test_peer target before this fixture')
        self.command = [str(binary), chain, str(p2p_port(node.index))]
        root = Path(test.options.tmpdir)
        index = len(list(root.glob('pq-test-peer-*.port')))
        self.port_file = root / f'pq-test-peer-{index}.port'
        self.log_file = root / f'pq-test-peer-{index}.log'
        self.process = None
        self.streams = []

    def __enter__(self):
        self.streams = [self.port_file.open('xb'), self.log_file.open('xb')]
        self.process = subprocess.Popen(self.command, stdin=subprocess.DEVNULL,
                                        stdout=self.streams[0], stderr=self.streams[1])
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if self.process.poll() is not None:
                    raise RuntimeError('Owned PQ injector exited before listen readiness')
                text = self.port_file.read_text().strip()
                if text.isdecimal():
                    self.port = int(text)
                    assert 1 <= self.port <= 65535
                    return self
                time.sleep(.02)
            raise TimeoutError('Owned PQ injector did not become ready')
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_):
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        for stream in self.streams:
            stream.close()
