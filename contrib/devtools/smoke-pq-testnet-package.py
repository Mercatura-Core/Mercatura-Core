#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Launch a native Qt testnet candidate on fresh owned offline data, then stop.

RPC binds loopback. P2P listeners, discovery and all P2P activity are disabled.
No live public-testnet connection or operator data is used.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        rpc_port = reservation.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix='pq-package-smoke-') as temp:
        datadir = Path(temp)
        command = [str(args.qt.resolve()), f'-datadir={datadir}', '-testnet', '-networkactive=0', '-listen=0',
                   '-connect=0', '-discover=0', '-dnsseed=0', '-fixedseeds=0', '-natpmp=0', '-listenonion=0',
                   '-server=1', '-rpcbind=127.0.0.1', f'-rpcport={rpc_port}', '-disablewallet=1']
        started = time.monotonic()
        # Preserve inherited runtime authentication/proxy/CA configuration.
        env = dict(os.environ)
        env.setdefault('QT_QPA_PLATFORM', 'windows' if os.name == 'nt' else 'offscreen')
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

        def rpc(method, params=()):
            cookie = (datadir / 'testnet' / '.cookie').read_text().strip()
            request = urllib.request.Request(f'http://127.0.0.1:{rpc_port}',
                data=json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': list(params)}).encode(),
                headers={'Content-Type': 'application/json',
                         'Authorization': 'Basic ' + base64.b64encode(cookie.encode()).decode()})
            with opener.open(request, timeout=3) as response:
                result = json.load(response)
            if result.get('error'):
                raise RuntimeError('Owned smoke RPC not ready')
            return result['result']

        with (datadir / 'process.log').open('wb') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=env)
            try:
                deadline = time.monotonic() + 60
                while True:
                    if process.poll() is not None:
                        raise RuntimeError(f'Qt smoke exited before RPC startup: {process.returncode}')
                    try:
                        info = rpc('getnetworkinfo')
                        break
                    except (OSError, urllib.error.URLError, RuntimeError):
                        if time.monotonic() > deadline:
                            raise TimeoutError('Qt smoke RPC deadline exceeded') from None
                        time.sleep(0.1)
                assert info['pqp2p']['required'] and info['pqp2p']['chain'] == 'test'
                assert info['pqp2p']['testnet_enforced_build']
                assert info['pqp2p']['testnet_activation'] == 'required-from-startup'
                assert not info['networkactive'] and not info['pqp2p']['listening']
                assert info['connections'] == info['pqp2p']['connected'] == 0
                genesis = rpc('getblockhash', [0])
                assert genesis == 'b3fc707c9a89dc45d1cf6a837284b22ad2b6fd41b99af601079db9dcbeff6920'
                rpc('stop')
                process.wait(timeout=30)
                assert process.returncode == 0
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
        args.output.write_text(json.dumps({'result': 'PASS', 'seconds': time.monotonic()-started,
            'qt_binary': str(args.qt.resolve()), 'genesis': genesis, 'pqp2p': info['pqp2p'],
            'wallet_load_qualified': False, 'installation_qualified': False,
            'p2p_enabled': False, 'all_children_exited': True}, indent=2) + '\n')


if __name__ == '__main__':
    main()
