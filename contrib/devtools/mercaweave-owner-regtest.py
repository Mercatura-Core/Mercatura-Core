#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.

"""Owner controls for the existing native three-node MercaWeave regtest fixture.

No production capability or new network framework: the foreground server extends
only the existing fixture's interactive hold, adding an owned restart command.
Wallets/cookies stay in a newly allocated private directory and are never deleted.
"""

import argparse
import configparser
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

MARKER = 'mercaweave-owner-regtest-v1'


def write_json(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2, default=str) + '\n', encoding='utf-8')
    temporary.replace(path)


def load_state(directory):
    root = directory.resolve()
    state = json.loads((root / 'owner.json').read_text(encoding='utf-8'))
    if state['kind'] != MARKER or state['root'] != str(root):
        raise RuntimeError('Not an owned disposable MercaWeave fixture')
    return root, state


def controller_alive(state):
    pid = state.get('controller_pid')
    if not pid:
        return False
    try:
        os.kill(pid, 0)
        command = Path(f'/proc/{pid}/cmdline')
        return not command.exists() or (b'_serve' in command.read_bytes() and state['root'].encode() in command.read_bytes())
    except (OSError, ProcessLookupError):
        return False


def cli(root, state, node, method, *args, wallet=None):
    directory = root / 'cluster' / f'node{node}'
    command = [state['cli'], '-regtest', '-datadir=' + str(directory), '-rpcconnect=127.0.0.1']
    if wallet:
        command.append('-rpcwallet=' + wallet)
    result = subprocess.run(command + [method] + [str(arg) for arg in args],
                            capture_output=True, text=True, timeout=240)
    if result.returncode:
        raise RuntimeError(f'Node {node}, {method}: {result.stderr.strip()}')
    try:
        return json.loads(result.stdout, parse_float=Decimal) if result.stdout.strip() else None
    except json.JSONDecodeError:
        return result.stdout.strip()


def wait_for(function, seconds=240):
    deadline = time.monotonic() + seconds
    last = None
    while time.monotonic() < deadline:
        try:
            result = function()
            if result:
                return result
        except (OSError, RuntimeError, json.JSONDecodeError) as error:
            last = error
        time.sleep(0.3)
    raise TimeoutError(f'Fixture operation timed out: {last}')


def serve():
    # Run the published, proven fixture unchanged; only its interactive hold is extended.
    repo = Path(os.environ['MCA_OWNER_SOURCE'])
    sys.path.insert(0, str(repo / 'test/functional'))
    from mercatura_mercaweave_multinode import MercaWeaveMultinodeTest

    class OwnerFixture(MercaWeaveMultinodeTest):
        def set_test_params(self):
            super().set_test_params()

        def run_test(self):
            super().run_test()

        def fund(self, wallet, mode, inputs, outputs):
            # Compatibility with the published c938061 fixture: its boundary
            # rollback also needs the funding coin to remain mature at tip 108.
            if mode == 'v2' and not inputs and self.nodes[0].getblockcount() == 110:
                coin = next(c for c in wallet.listunspent(103)
                            if c['scriptPubKey'].startswith('5220'))
                inputs = [dict(txid=coin['txid'], vout=coin['vout'])]
            return super().fund(wallet, mode, inputs, outputs)

        def hold_for_inspection(self):
            cluster = Path(self.options.tmpdir)
            root, state = load_state(cluster.parent)
            state.update(controller_pid=os.getpid(), nodes=self.network(), tip=self.tips(),
                         recipient_v2=self.nodes[1].get_wallet_rpc('receiver').getnewaddress('owner-recipient-v2'),
                         recipient_v3=self.nodes[1].get_wallet_rpc('receiver').getnewmercaweaveaddress('owner-recipient-v3'))
            write_json(root / 'owner.json', state)
            (root / 'READY').write_text('Actual native Qt and two independent daemons; disposable regtest only\n')
            previous = None
            while not (cluster / 'STOP-ONLY-THIS-FIXTURE').exists():
                request = root / 'command.json'
                if request.exists():
                    command = json.loads(request.read_text(encoding='utf-8'))
                    if command['id'] != previous:
                        previous = command['id']
                        if command['action'] != 'restart':
                            raise RuntimeError('Unknown owner fixture command')
                        self.stop_nodes()
                        self.nodes[0].args = [arg for arg in self.nodes[0].args if arg != '-resetguisettings']
                        env = dict(QT_QPA_PLATFORM=self.options.qt_platform,
                                   XDG_CONFIG_HOME=str(cluster / 'qt-config'), XDG_CACHE_HOME=str(cluster / 'qt-cache'))
                        if self.options.gui_probe_dir:
                            env.update(LD_PRELOAD=str(self.options.gui_probe_library.resolve()),
                                       MERCAWEAVE_GUI_PROBE_DIR=str(self.options.gui_probe_dir.resolve()))
                        self.start_node(0, env=env)
                        self.start_node(1)
                        self.start_node(2)
                        self.connect_nodes(0, 1)
                        self.connect_nodes(1, 2)
                        self.sync_all()
                        state.update(nodes=self.network(), tip=self.tips())
                        write_json(root / 'owner.json', state)
                        write_json(root / 'response.json', dict(id=previous, ok=True, tip=self.tips()))
                time.sleep(0.2)

    OwnerFixture(str(repo / 'test/functional/mercatura_mercaweave_multinode.py')).main()


def status(root, state):
    nodes = []
    for index, wallet in enumerate(['sender', 'receiver', 'observer']):
        info = cli(root, state, index, 'getblockchaininfo')
        if info['chain'] != 'regtest':
            raise RuntimeError('Owner fixture must remain regtest')
        peers = cli(root, state, index, 'getpeerinfo')
        if not peers or any(not p['addr'].startswith('127.0.0.1:') or not p['pq_authenticated'] or not p['pq_encrypted'] for p in peers):
            raise RuntimeError('Expected independent loopback PQ peers')
        coins = cli(root, state, index, 'listunspent', wallet=wallet)
        types = {str(v): sum(1 for c in coins if c['scriptPubKey'].startswith(f'{0x50 + v:02x}20')) for v in [2, 3]}
        groups = {coin['address'] for coin in coins if coin['scriptPubKey'].startswith('5320')}
        for address in groups:
            if not cli(root, state, index, 'getaddressinfo', address, wallet=wallet)['pq_group_authenticated']:
                raise RuntimeError('Unauthenticated grouped ownership record')
        nodes.append(dict(node=index, wallet=wallet, height=info['blocks'], hash=info['bestblockhash'],
                          balance=cli(root, state, index, 'getbalance', wallet=wallet), spendable_utxos=types,
                          mempool=cli(root, state, index, 'getrawmempool'),
                          authenticated_group_addresses=sorted(groups)))
    if len({n['hash'] for n in nodes}) != 1:
        raise RuntimeError('Wait for independent nodes to synchronize')
    return dict(root=str(root), nodes=nodes, recipient_v2=state['recipient_v2'], recipient_v3=state['recipient_v3'])


def mine(root, state, blocks):
    address = cli(root, state, 2, 'getnewaddress', 'owner-mining', wallet='observer')
    hashes = cli(root, state, 2, 'generatetoaddress', blocks, address)
    wait_for(lambda: len({cli(root, state, i, 'getbestblockhash') for i in range(3)}) == 1)
    return hashes


def verify(root, state, txid):
    known = cli(root, state, 0, 'gettransaction', txid, wallet='sender')
    decoded = cli(root, state, 0, 'decoderawtransaction', known['hex'])
    versions = []
    anchors = followers = 0
    for vin in decoded['vin']:
        prev = cli(root, state, 0, 'gettransaction', vin['txid'], wallet='sender')
        previous = cli(root, state, 0, 'decoderawtransaction', prev['hex'])['vout'][vin['vout']]['scriptPubKey']['hex']
        version = 2 if previous.startswith('5220') else 3 if previous.startswith('5320') else 0
        versions.append(version)
        witness = [bytes.fromhex(value) for value in vin.get('txinwitness', [])]
        if version == 3:
            if [len(x) for x in witness] == [3309, 1952, 32]:
                anchors += 1
            elif [len(x) for x in witness] == [32, 4]:
                followers += 1
            else:
                raise RuntimeError('Unexpected native grouped witness structure')
    block = known.get('blockhash')
    if block:
        validation = [txid in cli(root, state, i, 'getblock', block)['tx'] for i in range(3)]
    else:
        validation = [txid in cli(root, state, i, 'getrawmempool') for i in range(3)]
    if not all(validation):
        raise RuntimeError('Transaction not yet relayed/validated by every node')
    return dict(txid=txid, confirmations=known['confirmations'], input_versions=versions,
                grouped_anchors=anchors, grouped_followers=followers, serialized_bytes=len(known['hex']) // 2,
                actual_wallet_fee=known.get('fee'), independently_present_on_all_three=True, blockhash=block)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '_serve':
        del sys.argv[1]
        serve()
        return
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['start', 'status', 'restart', 'stop', 'fund', 'mine', 'verify'])
    parser.add_argument('--state-dir', type=Path, required=True, help='New private directory for start; the same directory for all later commands')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--qt-platform', choices=['auto', 'xcb', 'wayland', 'offscreen'], default='auto')
    parser.add_argument('--probe-library', type=Path, help='Optional inspection-only Qt probe; not needed for owner testing')
    parser.add_argument('--address')
    parser.add_argument('--amount', type=Decimal, default=Decimal('5'))
    parser.add_argument('--blocks', type=int, default=1)
    parser.add_argument('--txid')
    args = parser.parse_args()
    if args.action == 'start':
        if not args.build_dir:
            parser.error('start requires --build-dir')
        build = args.build_dir.resolve()
        config = configparser.ConfigParser()
        config.read(build / 'test/config.ini')
        repo = Path(config['environment']['SRCDIR'])
        suffix = config['environment']['EXEEXT']
        platform = args.qt_platform
        if platform == 'auto':
            platform = 'xcb' if os.environ.get('DISPLAY') else 'wayland' if os.environ.get('WAYLAND_DISPLAY') else ''
        if not platform:
            parser.error('No interactive display. Use WSLg/desktop, or explicitly --qt-platform=offscreen for headless checks.')
        binaries = {name: build / 'bin' / (name + suffix) for name in ['mercatura-qt', 'mercaturad', 'mercatura-cli']}
        if not all(path.is_file() for path in binaries.values()):
            parser.error('Expected built native Qt, daemon and CLI applications in build/bin')
        root = args.state_dir.resolve()
        root.mkdir(mode=0o700, parents=False, exist_ok=False)
        state = dict(kind=MARKER, root=str(root), cli=str(binaries['mercatura-cli']), source=str(repo),
                     binary_sha256={name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in binaries.items()},
                     qt_platform=platform)
        write_json(root / 'owner.json', state)
        command = [sys.executable, str(Path(__file__).resolve()), '_serve', '--configfile=' + str(build / 'test/config.ini'),
                   '--tmpdir=' + str(root / 'cluster'), '--artifact-dir=' + str(root / 'evidence'), '--nocleanup',
                   '--qt-binary=' + str(binaries['mercatura-qt']), '--qt-platform=' + platform, '--interactive=activated', '--timeout-factor=3']
        if args.probe_library:
            command += ['--gui-probe-dir=' + str(root / 'screenshots'), '--gui-probe-library=' + str(args.probe_library.resolve())]
        with (root / 'fixture.log').open('w', encoding='utf-8') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                       env=dict(os.environ, MCA_OWNER_SOURCE=str(repo)))
            try:
                def ready():
                    if process.poll() is not None:
                        raise ValueError('Fixture failed; inspect fixture.log')
                    return (root / 'READY').exists()
                wait_for(ready, 600)
                print(json.dumps(status(root, load_state(root)[1]), indent=2, default=str), flush=True)
                print('READY: use the actual Qt Receive and Send tabs. Keep this terminal open. Stop from another terminal with this helper.', flush=True)
                result = process.wait()
            except BaseException:
                if (root / 'cluster').is_dir():
                    (root / 'cluster/STOP-ONLY-THIS-FIXTURE').touch()
                process.wait(timeout=240)
                raise
        if result:
            raise RuntimeError('Fixture failed; retain directory and inspect fixture.log')
        (root / 'STOPPED').write_text('All owned nodes stopped normally; data preserved\n')
        return
    root, state = load_state(args.state_dir)
    if args.action == 'stop':
        (root / 'cluster/STOP-ONLY-THIS-FIXTURE').touch()
        if controller_alive(state):
            wait_for(lambda: (root / 'STOPPED').exists(), 240)
        else:
            # A host/terminal failure can leave owned nodes without their controller.
            # Authenticate to these exact disposable profiles and use normal RPC stop.
            for index in range(3):
                try:
                    info = cli(root, state, index, 'getblockchaininfo')
                except RuntimeError:
                    continue
                if info['chain'] != 'regtest':
                    raise RuntimeError('Refusing to stop a non-regtest process')
                cli(root, state, index, 'stop')
            def stopped():
                for index in range(3):
                    try:
                        cli(root, state, index, 'getblockchaininfo')
                        return False
                    except RuntimeError:
                        pass
                return True
            wait_for(stopped, 240)
            (root / 'STOPPED').write_text('Owned nodes stopped via authenticated RPC; data preserved\n')
        print('Stopped only this fixture; all wallets and data are preserved.')
        return
    status(root, state)  # Before mutations, verify regtest, loopback PQ peers and authenticated custody.
    if args.action == 'status':
        result = status(root, state)
    elif args.action == 'restart':
        if not controller_alive(state):
            raise RuntimeError('Fixture controller is unavailable. Preserve data; use stop for owned-node cleanup and start a fresh fixture.')
        request_id = str(uuid.uuid4())
        before = status(root, state)
        write_json(root / 'command.json', dict(id=request_id, action='restart'))
        def response():
            path = root / 'response.json'
            return path.exists() and json.loads(path.read_text())['id'] == request_id
        wait_for(response, 300)
        result = status(root, load_state(root)[1])
        if [(n['hash'], n['balance'], n['spendable_utxos']) for n in before['nodes']] != [(n['hash'], n['balance'], n['spendable_utxos']) for n in result['nodes']]:
            raise RuntimeError('Restart did not preserve chain/balances/UTXOs')
    elif args.action == 'mine':
        if not 1 <= args.blocks <= 20:
            parser.error('--blocks must be 1..20 for this manual fixture')
        result = dict(blocks=mine(root, state, args.blocks))
    elif args.action == 'fund':
        if not args.address or not 0 < args.amount <= 100:
            parser.error('fund needs --address and --amount in (0,100] MCA')
        info = cli(root, state, 0, 'validateaddress', args.address)
        if not info.get('isvalid') or not info['scriptPubKey'].startswith(('5220', '5320')):
            parser.error('Use a native witness-v2/v3 address generated in this isolated regtest wallet')
        outputs = '[{' + json.dumps(args.address) + ':' + format(args.amount, 'f') + '}]'
        funded = cli(root, state, 0, 'walletcreatefundedpsbt', '[]', outputs, '0',
                     '{"spending_mode":"v2","fee_rate":0.001}', wallet='sender')
        signed = cli(root, state, 0, 'walletprocesspsbt', funded['psbt'], wallet='sender')
        if not signed['complete']:
            raise RuntimeError('Funding PSBT is not completely signed; unlock the disposable wallet in Qt')
        final = cli(root, state, 0, 'finalizepsbt', signed['psbt'])
        if not final['complete']:
            raise RuntimeError('Funding PSBT did not finalize')
        txid = cli(root, state, 0, 'sendrawtransaction', final['hex'])
        wait_for(lambda: all(txid in cli(root, state, i, 'getrawmempool') for i in range(3)))
        mine(root, state, 1)
        result = verify(root, state, txid)
    else:
        if not args.txid:
            parser.error('verify needs --txid from the actual Qt transaction')
        result = verify(root, state, args.txid)
    print(json.dumps(result, indent=2, default=str))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        sys.exit(str(error))
