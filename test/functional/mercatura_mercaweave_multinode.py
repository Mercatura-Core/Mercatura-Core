#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license.

"""Three actual PQ-connected validators, native grouped custody and optional Qt.

All connected nodes use one disposable regtest schedule. Optional inspection
loads only the widget probe into the actual Qt node; no signing/validation
capability or production source is changed. Evidence contains public transaction
and process metadata, never RPC cookies, wallet seeds or private keys.
"""

from copy import deepcopy
from decimal import Decimal
import importlib.util
import json
from pathlib import Path
import time

from mercatura_mercaweave import MercaWeaveTest
from test_framework.address import address_to_scriptpubkey
from test_framework.messages import COIN, COutPoint, CTxIn, CTxOut, tx_from_hex
from test_framework.psbt import PSBT, PSBT_IN_WITNESS_UTXO
from test_framework.segwit_addr import encode_segwit_address
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class GuiProbe:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.sequence = 0
        self.history = []

    def call(self, action, **kwargs):
        self.sequence += 1
        command = dict(id=self.sequence, action=action, **kwargs)
        temporary = self.directory / 'command.tmp'
        temporary.write_text(json.dumps(command))
        temporary.replace(self.directory / 'command.json')
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            response = self.directory / 'response.json'
            if response.exists():
                result = json.loads(response.read_text())
                if result.get('id') == self.sequence:
                    self.history.append(dict(command=command, result=result))
                    (self.directory / 'inspection.json').write_text(json.dumps(self.history, indent=2))
                    return result
            time.sleep(0.1)
        raise AssertionError(f'Native Qt inspection timed out: {action}')

    def require(self, action, **kwargs):
        result = self.call(action, **kwargs)
        assert result['ok'], result
        return result

    def widget(self, name):
        state = self.require('state')
        return next(widget for widget in state['widgets'] if widget['name'] == name)


class MercaWeaveMultinodeTest(MercaWeaveTest):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.wallet_names = [False] * 3
        self.rpc_timeout = 240
        self.extra_args = [['-mercaweaveheight=110', '-pqp2p=required', '-connect=0',
                            '-rpcbind=127.0.0.1', '-rpcallowip=127.0.0.1', '-unsafesqlitesync=0'] for _ in range(3)]
        self.evidence = []
        self.gui = None

    def add_options(self, parser):
        parser.add_argument('--artifact-dir', type=Path)
        parser.add_argument('--gui-probe-dir', type=Path)
        parser.add_argument('--qt-binary', type=Path)
        parser.add_argument('--gui-probe-library', type=Path)
        parser.add_argument('--qt-platform', choices=['offscreen', 'xcb', 'wayland'], default='offscreen')
        parser.add_argument('--gui-layout-matrix', action='store_true',
                            help='Inspect the populated native Send/Coin Control layout at representative desktop sizes')
        parser.add_argument('--interactive', choices=['inactive', 'activated'],
                            help='Hold the disposable network for manual inspection; does not run the full qualification')

    def setup_network(self):
        (Path(self.options.tmpdir) / '.mercaweave-qualification-fixture').write_text('Disposable native regtest fixture only\n')
        self.add_nodes(self.num_nodes, self.extra_args)
        if self.options.qt_binary:
            self.nodes[0].args[0] = str(self.options.qt_binary.resolve())
            self.nodes[0].args += ['-nosplash', '-resetguisettings', '-choosedatadir=0', '-lang=en']
            env = dict(QT_QPA_PLATFORM=self.options.qt_platform,
                       XDG_CONFIG_HOME=str(Path(self.options.tmpdir) / 'qt-config'),
                       XDG_CACHE_HOME=str(Path(self.options.tmpdir) / 'qt-cache'))
            if self.options.gui_probe_dir:
                assert self.options.gui_probe_library
                directory = self.options.gui_probe_dir.resolve()
                directory.mkdir(parents=True, exist_ok=True)
                self.gui = GuiProbe(directory)
                env.update(LD_PRELOAD=str(self.options.gui_probe_library.resolve()),
                           MERCAWEAVE_GUI_PROBE_DIR=str(directory))
            self.start_node(0, env=env)
        else:
            assert not self.options.interactive and not self.options.gui_probe_dir
            self.start_node(0)
        self.start_node(1)
        self.start_node(2)
        # A line, rather than three direct links, proves a second relay hop.
        self.connect_nodes(0, 1)
        self.connect_nodes(1, 2)
        self.sync_all()

    def sync_all(self, nodes=None):
        BitcoinTestFramework.sync_all(self, nodes=self.nodes if nodes is None else nodes)

    def record(self, name, **details):
        self.evidence.append(dict(name=name, result='PASS', **details))
        path = self.options.artifact_dir or Path(self.options.tmpdir) / 'qualification'
        path.mkdir(parents=True, exist_ok=True)
        (path / 'results.json').write_text(json.dumps(self.evidence, indent=2, default=str) + '\n')
        self.log.info('QUALIFICATION PASS: %s', name)

    def hold_for_inspection(self):
        directory = Path(self.options.tmpdir)
        (directory / '.mercaweave-qualification-fixture').write_text('Disposable native regtest fixture only\n')
        stop = directory / 'STOP-ONLY-THIS-FIXTURE'
        self.log.info('Interactive demo only, not the complete automated qualification. To stop these three nodes safely, create: %s', stop)
        self.wait_until(stop.exists, timeout=86400)

    def tips(self):
        hashes = [node.getbestblockhash() for node in self.nodes]
        heights = [node.getblockcount() for node in self.nodes]
        assert_equal(len(set(hashes)), 1)
        assert_equal(len(set(heights)), 1)
        return dict(hash=hashes[0], height=heights[0])

    def network(self):
        snapshot = []
        for node in self.nodes:
            assert node.process.poll() is None
            assert_equal(node.getblockchaininfo()['chain'], 'regtest')
            peers = node.getpeerinfo()
            assert peers
            for peer in peers:
                assert peer['addr'].startswith('127.0.0.1:'), peer['addr']
                assert peer['pq_encrypted'] and peer['pq_authenticated']
                assert_equal(peer['transport_protocol_type'], 'pq-v1')
            snapshot.append(dict(index=node.index, pid=node.process.pid, datadir=str(node.datadir_path),
                                 argv=node.args + node.extra_args,
                                 config=[line for line in node.bitcoinconf.read_text().splitlines()
                                         if line.startswith(('port=', 'rpcport=', 'bind=', 'rpcbind=', 'discover=',
                                                             'dnsseed=', 'fixedseeds=', 'listenonion=', 'connect='))],
                                 peers=[dict(address=p['addr'], subversion=p['subver'], pq=p['pq_authenticated']) for p in peers]))
        assert_equal(len({item['pid'] for item in snapshot}), 3)
        assert_equal(len({item['datadir'] for item in snapshot}), 3)
        return snapshot

    @staticmethod
    def version(script):
        raw = bytes.fromhex(script['hex'])
        assert_equal(len(raw), 34)
        assert_equal(raw[1], 32)
        assert raw[0] in (0x52, 0x53)
        return raw[0] - 0x50

    def versions(self, tx):
        return [self.version(self.nodes[0].gettxout(vin['txid'], vin['vout'])['scriptPubKey']) for vin in tx['vin']]

    def reject_blocks_independently(self, block, expected):
        # Connected peers may learn a rejected block before the next RPC call,
        # producing duplicate-invalid instead of its original consensus reason.
        # Give each validator a unique coinbase extranonce/header, while keeping
        # the same adversarial transaction, parent, amounts and consensus rules.
        for index, peer in enumerate(self.nodes):
            candidate = deepcopy(block)
            candidate.vtx[0].vin[0].scriptSig = bytes(candidate.vtx[0].vin[0].scriptSig) + bytes([1, index + 1])
            assert len(candidate.vtx[0].vin[0].scriptSig) <= 100
            candidate.hashMerkleRoot = candidate.calc_merkle_root()
            candidate.nNonce = 0
            self.solve_mercatura_block(peer, candidate)
            assert_equal(peer.submitblock(candidate.serialize().hex()), expected)

    def fund(self, wallet, mode, inputs, outputs):
        return wallet.walletcreatefundedpsbt(inputs, outputs, options={
            'spending_mode': mode, 'add_inputs': not inputs, 'fee_rate': Decimal('0.001')})

    def gui_start(self):
        if not self.gui:
            return
        self.wait_until(lambda: self.gui.call('wallet', wallet='sender')['ok'], timeout=60)
        self.gui.require('resize', width=1000, height=800)
        self.wait_until(lambda: any(w['name'] == 'spendingMode' and w['visible']
                                   for w in self.gui.call('send').get('widgets', [])), timeout=60)
        mode = self.gui.widget('spendingMode')
        assert mode['visible']
        assert_equal(mode['index'], 0)
        assert_equal([item['text'] for item in mode['items']], [
            'Automatic (Recommended)', 'Standard PQ — Witness v2 only', 'MercaWeave — Witness v3 only'])
        assert not mode['items'][2]['enabled']
        assert not self.gui.widget('frameCoinControl')['visible']
        self.gui.require('screenshot', file='A-send-inactive-1000x800.png')
        self.gui.require('popup', name='spendingMode')
        self.gui.require('screenshot', popup=True, file='B-dropdown-expanded-inactive.png')
        self.gui.require('hidePopup')
        self.gui.require('resize', width=850, height=650)
        self.gui.require('screenshot', file='A-send-inactive-850x650.png')
        self.gui.require('resize', width=1000, height=800)
        self.record('actual-native-Qt-inactive-Send', platform='offscreen', mode=mode)

    def gui_active(self):
        if not self.gui:
            return
        self.wait_until(lambda: self.gui.widget('spendingMode')['items'][2]['enabled'], timeout=30)
        self.gui.require('screenshot', file='D-send-active-automatic.png')
        self.gui.require('popup', name='spendingMode')
        self.gui.require('screenshot', popup=True, file='B-dropdown-expanded-active.png')
        self.gui.require('hidePopup')
        self.gui.require('click', name='pushButtonCoinControl')
        rows = self.gui.widget('treeWidget')['rows']
        assert {'Standard PQ (v2)', 'MercaWeave (v3)'} <= {row['type'] for row in rows}
        self.gui.require('screenshot', **{'class': 'CoinControlDialog'}, file='C-coin-control-all-types.png')
        self.gui.require('scrollTypes')
        self.gui.require('screenshot', **{'class': 'CoinControlDialog'}, file='C-coin-control-types-scrolled.png')
        self.gui.require('close', **{'class': 'CoinControlDialog'})
        self.gui.require('select', name='spendingMode', index=1)
        self.gui.require('click', name='pushButtonCoinControl')
        self.gui.require('select', name='inputTypeFilter', index=0)
        rows = self.gui.widget('treeWidget')['rows']
        assert all(not row['checkable'] for row in rows if row['type'] == 'MercaWeave (v3)')
        self.gui.require('coin', type='Standard PQ (v2)', selected=True)
        self.gui.require('close', **{'class': 'CoinControlDialog'})
        self.gui.require('select', name='spendingMode', index=2)
        self.gui.require('click', name='pushButtonCoinControl')
        rows = self.gui.widget('treeWidget')['rows']
        assert any(row['type'] == 'Standard PQ (v2)' and row['selected'] for row in rows)
        self.gui.require('coin', type='Standard PQ (v2)', selected=False)
        self.gui.require('coin', type='MercaWeave (v3)', selected=True)
        self.gui.require('screenshot', **{'class': 'CoinControlDialog'}, file='C-coin-control-v3-selection.png')
        self.gui.require('scrollTypes')
        self.gui.require('screenshot', **{'class': 'CoinControlDialog'}, file='C-coin-control-v3-selection-scrolled.png')
        self.gui.require('coin', type='MercaWeave (v3)', selected=False)
        self.gui.require('close', **{'class': 'CoinControlDialog'})
        self.gui.require('screenshot', file='D-send-active-grouped.png')
        self.gui.require('click', name='buttonChooseFee')
        self.gui.require('screenshot', file='D-send-active-fee-controls.png')
        self.gui.require('wallet', wallet='unsupported')
        self.wait_until(lambda: not self.gui.widget('spendingMode')['items'][2]['enabled'], timeout=30)
        self.gui.require('screenshot', file='E-unsupported-wallet.png')
        self.gui.require('wallet', wallet='sender')
        self.wait_until(lambda: self.gui.widget('spendingMode')['items'][2]['enabled'], timeout=30)
        self.record('actual-native-Qt-Coin-Control-and-wallet-switch', rows=rows, fee=self.gui.widget('labelCoinControlBytes'))
        if self.options.gui_layout_matrix:
            self.gui_layout_matrix()

    def gui_layout_matrix(self):
        previous_mode = self.gui.widget('spendingMode')['index']
        self.gui.require('select', name='spendingMode', index=0)
        measurements = []
        for width, height in [(1280, 800), (1366, 768), (1920, 1080)]:
            size = f'{width}x{height}'
            self.gui.require('resize', width=width, height=height)
            mode = self.gui.widget('spendingMode')
            assert mode['visible'] and mode['index'] == 0
            self.gui.require('screenshot', file=f'H-send-{size}.png')
            self.gui.require('popup', name='spendingMode')
            self.gui.require('screenshot', popup=True, file=f'H-dropdown-{size}.png')
            self.gui.require('hidePopup')
            self.gui.require('click', name='pushButtonCoinControl')
            # Keep the existing ordinary dialog width, rather than enlarging it
            # to the desktop just to make the authorization column visible.
            self.gui.require('resize', **{'class': 'CoinControlDialog'}, width=1000, height=500)
            for view in ['List', 'Tree']:
                self.gui.require('click', name=f'radio{view}Mode')
                self.gui.require('expandCoins')
                tree = self.gui.widget('treeWidget')
                column = next(item for item in tree['columns'] if item['logical'] == 6)
                assert_equal(tree['horizontal_scroll'], 0)
                assert column['x'] >= 0
                assert column['x'] + column['width'] <= tree['viewport_width'], tree
                assert {'Standard PQ (v2)', 'MercaWeave (v3)'} <= {row['type'] for row in tree['rows']}
                assert all(row['type_text_width'] < column['width'] for row in tree['rows'])
                self.gui.require('screenshot', **{'class': 'CoinControlDialog'}, file=f'H-coins-{view.lower()}-{size}.png')
                measurements.append(dict(main_size=size, view=view, tree=tree))
            self.gui.require('close', **{'class': 'CoinControlDialog'})
        self.gui.require('select', name='spendingMode', index=previous_mode)
        assert_equal(self.gui.widget('spendingMode')['index'], previous_mode)
        self.record('actual-native-Qt-Coin-Control-layout-matrix', measurements=measurements,
                    restored_spending_mode=previous_mode)

    def gui_stale_proposal(self, recipient, ancestor):
        if not self.gui:
            return
        self.gui.require('recipient', address=recipient, amount=COIN)
        self.gui.require('clickQueued', name='sendButton')
        self.wait_until(lambda: any(dialog['class'] == 'SendConfirmationDialog'
                                   for dialog in self.gui.require('state')['dialogs']), timeout=30)
        self.gui.require('screenshot', **{'class': 'SendConfirmationDialog'}, file='F-grouped-send-confirmation.png')
        previous = self.tips()
        for peer in self.nodes:
            peer.invalidateblock(ancestor)
        self.wait_until(lambda: not any(dialog['class'] == 'SendConfirmationDialog'
                                       for dialog in self.gui.require('state')['dialogs']), timeout=30)
        mode = self.gui.widget('spendingMode')
        assert_equal(mode['index'], 2)
        assert not mode['items'][2]['enabled']
        assert all(not peer.getrawmempool() for peer in self.nodes)
        self.gui.require('screenshot', file='F-stale-confirmation-invalidated.png')
        for peer in self.nodes:
            peer.reconsiderblock(ancestor)
        self.sync_all()
        assert_equal(self.tips(), previous)
        self.wait_until(lambda: self.gui.widget('spendingMode')['items'][2]['enabled'], timeout=30)
        self.record('actual-native-pending-Send-invalidated-by-activation-reorg', tip=self.tips())

    def foreign_commitment_negative(self, sender, standard, recipient, original, public):
        # Reuse the locked public commitment oracle, not a fixture signer.
        # These are real funded coins; this intentionally invalid witness must
        # fail ownership before its borrowed signature can even be considered.
        path = Path(self.config['environment']['SRCDIR']) / 'src/test/mercaweave/reference.py'
        spec = importlib.util.spec_from_file_location('mercaweave_public_commitment_oracle', path)
        oracle = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(oracle)
        genesis = bytes.fromhex(self.nodes[0].getblockhash(0))[::-1]
        key, nonce = bytes.fromhex(public['pq_group_public_key']), bytes.fromhex(public['pq_address_nonce'])
        expected = oracle.commitment(genesis, key, nonce)
        assert_equal(public['scriptPubKey'], (b'\x53\x20' + expected).hex())
        foreign_genesis = bytes([genesis[0] ^ 1]) + genesis[1:]
        program = oracle.commitment(foreign_genesis, key, nonce)
        assert program != expected
        address = encode_segwit_address('mcrt', 3, program)
        funded = self.fund(sender, 'v2', [], {address: Decimal('2')})
        raw, _ = self.finish(sender, funded['psbt'])
        txid = self.nodes[0].sendrawtransaction(raw)
        self.sync_all()
        self.mine(1, standard)
        decoded = self.nodes[0].decoderawtransaction(raw)
        index = next(out['n'] for out in decoded['vout'] if out['scriptPubKey']['hex'] == (b'\x53\x20' + program).hex())
        invalid = deepcopy(original)
        invalid.vin = [CTxIn(COutPoint(int(txid, 16), index))]
        invalid.vout = [CTxOut(2 * COIN - 6, address_to_scriptpubkey(recipient))]
        invalid.wit.vtxinwit = [deepcopy(original.wit.vtxinwit[0])]
        for peer in self.nodes:
            rejection = peer.testmempoolaccept([invalid.serialize().hex()])[0]
            assert not rejection['allowed']
            assert_equal(rejection['reject-reason'], 'pqgroup-authorization-13')
        block = self.block(self.nodes[0], [address_to_scriptpubkey(standard)], [invalid], 6)
        self.reject_blocks_independently(block, 'pqgroup-authorization-13')
        self.record('foreign-genesis-output-commitment-rejected-by-all-validators',
                    funding_txid=txid, expected='pqgroup-authorization-13', tip=self.tips())

    def run_test(self):
        node, relay, validator = self.nodes
        for peer, name in zip(self.nodes, ['sender', 'receiver', 'observer']):
            peer.createwallet(name, load_on_startup=True)
        if self.gui:
            node.createwallet('unsupported', disable_private_keys=True, blank=True, load_on_startup=True)
        sender = node.get_wallet_rpc('sender')
        receiver = relay.get_wallet_rpc('receiver')
        observer = validator.get_wallet_rpc('observer')
        standard, recipient, reward = sender.getnewaddress(), receiver.getnewaddress(), observer.getnewaddress()
        self.record('three-independent-loopback-PQ-processes', nodes=self.network(), tip=self.tips())
        # Native Send is deliberately disabled during initial synchronization.
        # A genuine current block leaves genesis IBD without bypassing that guard.
        self.mine(1, standard)
        self.gui_start()
        if self.options.interactive == 'inactive':
            self.hold_for_inspection()
            return
        for wallet in [sender, receiver, observer]:
            assert_raises_rpc_error(-4, 'not active', wallet.getnewaddress, '', 'mercaweave')
        self.mine(108, standard)
        self.record('preactivation-and-real-reward-maturity', tip=self.tips(), spendable=sender.getbalance())
        self.mine(1, standard)
        group0 = sender.getnewmercaweaveaddress('group-0')
        group1 = sender.getnewmercaweaveaddress('group-1')
        foreign = receiver.getnewmercaweaveaddress('receiver-group')
        first = sender.getaddressinfo(group0)
        second = sender.getaddressinfo(group1)
        assert first['pq_group_authenticated'] and second['pq_group_authenticated']
        assert_equal(first['pq_group_public_key'], second['pq_group_public_key'])
        assert first['pq_address_nonce'] != second['pq_address_nonce']
        funding = self.fund(sender, 'v2', [], [{group0: Decimal('20')}, {group1: Decimal('20')}, {foreign: Decimal('8')}])
        funding_raw, _ = self.finish(sender, funding['psbt'])
        assert set(self.versions(node.decoderawtransaction(funding_raw))) == {2}
        boundary = node.getblockhash(110)
        ancestor = node.getblockhash(109)
        for peer in self.nodes:
            peer.invalidateblock(boundary)
            assert peer.testmempoolaccept([funding_raw])[0]['allowed']
        for wallet in [sender, receiver, observer]:
            assert_raises_rpc_error(-4, 'not active', wallet.getnewmercaweaveaddress)
        self.record('candidate-height-H-versus-active-tip-H-minus-one', tip=self.tips())
        for peer in self.nodes:
            peer.invalidateblock(ancestor)
            result = peer.testmempoolaccept([funding_raw])[0]
            assert not result['allowed']
            assert_equal(result['reject-reason'], 'mercaweave-inactive')
        assert_raises_rpc_error(-4, 'active consensus', self.fund, sender, 'v3', [], {recipient: Decimal('1')})
        ordinary = self.fund(sender, 'v2', [], {recipient: Decimal('1')})
        self.finish(sender, ordinary['psbt'])
        for peer in self.nodes:
            peer.reconsiderblock(ancestor)
            peer.reconsiderblock(boundary)
        self.sync_all()
        assert_equal(self.tips()['height'], 110)
        self.record('activation-boundary-reorg-and-v2-preservation', tip=self.tips())
        funding_txid = node.sendrawtransaction(funding_raw)
        self.sync_all()
        assert all(funding_txid in peer.getrawmempool() for peer in self.nodes)
        self.mine(1, standard)
        assert sender.getaddressinfo(group0)['pq_group_authenticated']
        assert receiver.getaddressinfo(foreign)['pq_group_authenticated']
        self.record('native-v2-funding-of-v3-output-relay-and-block', txid=funding_txid, tip=self.tips())
        self.gui_active()
        if self.options.interactive == 'activated':
            self.hold_for_inspection()
            return
        self.gui_stale_proposal(recipient, ancestor)

        def owned_group_points():
            return [{'txid': coin['txid'], 'vout': coin['vout']} for coin in sender.listunspent()
                    if coin.get('address') in [group0, group1]]

        points = owned_group_points()
        assert_equal(len(points), 2)
        standard_coins = [coin for coin in sender.listunspent() if self.version(node.gettxout(coin['txid'], coin['vout'])['scriptPubKey']) == 2]
        v2_points = [dict(txid=coin['txid'], vout=coin['vout']) for coin in standard_coins[:2]]
        assert_equal(len(v2_points), 2)
        assert_raises_rpc_error(-4, 'conflicts with the spending mode', self.fund, sender, 'v2', points, {recipient: Decimal('1')})
        assert_raises_rpc_error(-4, 'conflicts with the spending mode', self.fund, sender, 'v3', v2_points, {recipient: Decimal('1')})
        assert_raises_rpc_error(-4, 'v3', self.fund, sender, 'v3', [], {recipient: sender.getbalance()})
        grouped = self.fund(sender, 'v3', points, {recipient: Decimal('1')})
        grouped_raw, _ = self.finish(sender, grouped['psbt'])
        grouped_tx = tx_from_hex(grouped_raw)
        assert_equal(self.versions(node.decoderawtransaction(grouped_raw)), [3, 3])
        assert_equal(sorted(len(w.scriptWitness.stack) for w in grouped_tx.wit.vtxinwit), [2, 3])
        comparison = self.fund(sender, 'v2', v2_points, {recipient: Decimal('1')})
        comparison_raw, _ = self.finish(sender, comparison['psbt'])
        assert_equal(len(grouped_tx.vin), len(tx_from_hex(comparison_raw).vin))
        assert_equal(len(grouped_tx.vout), len(tx_from_hex(comparison_raw).vout))
        group_size, standard_size = len(bytes.fromhex(grouped_raw)), len(bytes.fromhex(comparison_raw))
        assert group_size < standard_size
        assert_equal(grouped['fee'], Decimal((group_size + 999) // 1000) / COIN)
        assert_equal(comparison['fee'], Decimal((standard_size + 999) // 1000) / COIN)
        self.record('explicit-policies-manual-conflicts-output-independence-and-real-fees',
                    grouped_bytes=group_size, standard_bytes=standard_size, saved_bytes=standard_size-group_size,
                    grouped_fee=grouped['fee'], standard_fee=comparison['fee'])

        hostile = PSBT.from_base64(grouped['psbt'])
        actual = node.gettxout(points[0]['txid'], points[0]['vout'])
        hostile.i[0].map[PSBT_IN_WITNESS_UTXO] = CTxOut(int(actual['value'] * COIN) + 1,
            bytes.fromhex(actual['scriptPubKey']['hex'])).serialize()
        assert_raises_rpc_error(-25, 'Inputs missing or spent', sender.walletprocesspsbt, hostile.to_base64())
        hostile = PSBT.from_base64(grouped['psbt'])
        alternate = node.gettxout(points[1]['txid'], points[1]['vout'])
        hostile.i[0].map[PSBT_IN_WITNESS_UTXO] = CTxOut(int(actual['value'] * COIN),
            bytes.fromhex(alternate['scriptPubKey']['hex'])).serialize()
        assert_raises_rpc_error(-25, 'Inputs missing or spent', sender.walletprocesspsbt, hostile.to_base64())
        self.record('wrong-actual-prevout-snapshot-rejected-before-signing')

        # Warm each real validator with valid authorization before mutating its
        # witness/body. Cached validation must not authorize any changed form.
        for peer in self.nodes:
            assert peer.testmempoolaccept([grouped_raw])[0]['allowed']
        mutations = [('invalid-signature', self.corrupt_signature(grouped_tx), 14)]
        for name, item in [('incorrect-group-public-key', 1), ('incorrect-output-commitment-nonce', 2)]:
            changed = deepcopy(grouped_tx)
            stack = next(w.scriptWitness.stack for w in changed.wit.vtxinwit if len(w.scriptWitness.stack) == 3)
            data = bytearray(stack[item])
            data[0] ^= 1
            stack[item] = bytes(data)
            mutations.append((name, changed, 13))
        changed = deepcopy(grouped_tx)
        changed.wit.vtxinwit[1].scriptWitness.stack[1] = (1).to_bytes(4, 'little')
        mutations.append(('self-follower-role', changed, 8))
        changed = deepcopy(grouped_tx)
        changed.wit.vtxinwit[1].scriptWitness.stack = []
        mutations.append(('missing-required-witness', changed, 7))
        changed = deepcopy(grouped_tx)
        changed.vout[0].nValue -= 1
        mutations.append(('changed-signed-transaction-commitment', changed, 14))
        changed = deepcopy(grouped_tx)
        changed.vin.reverse()
        changed.wit.vtxinwit.reverse()
        mutations.append(('reordered-input-roles', changed, 8))
        changed = deepcopy(grouped_tx)
        changed.wit.vtxinwit[1] = deepcopy(changed.wit.vtxinwit[0])
        mutations.append(('duplicate-group-key-anchor', changed, 9))
        for name, changed, code in mutations:
            expected = f'pqgroup-authorization-{code}'
            # Reconstruct rejects malformed roles/stacks/duplicate anchors
            # before cryptographic authorization. The existing work accountant
            # assigns MAX_BLOCK_SIGOPS_COST+1, failing closed at admission.
            expected_mempool = 'bad-txns-too-many-sigops' if code in (7, 8, 9) else expected
            if code == 7:
                expected_mempool = 'bad-witness-nonstandard'
            expected_block = 'bad-blk-sigops' if code in (7, 8, 9) else expected
            reasons = []
            for peer in self.nodes:
                rejection = peer.testmempoolaccept([changed.serialize().hex()])[0]
                assert not rejection['allowed'], (name, rejection)
                assert_equal(rejection['reject-reason'], expected_mempool)
                reasons.append(rejection['reject-reason'])
            extra_fee = sum(o.nValue for o in grouped_tx.vout) - sum(o.nValue for o in changed.vout)
            bad_block = self.block(node, [address_to_scriptpubkey(standard)], [changed], int(grouped['fee'] * COIN) + extra_fee)
            self.reject_blocks_independently(bad_block, expected_block)
            self.record('independent-mempool-and-block-rejection-' + name, expected_mempool=expected_mempool,
                        expected_block=expected_block, reasons=reasons, tip=self.tips())
        for peer in self.nodes:
            assert peer.testmempoolaccept([grouped_raw])[0]['allowed']
        txid = node.sendrawtransaction(grouped_raw)
        self.sync_all()
        assert all(txid in peer.getrawmempool() for peer in self.nodes)
        self.generatetoaddress(validator, 1, reward)
        assert all(txid in peer.getblock(peer.getbestblockhash())['tx'] for peer in self.nodes)
        assert_equal(receiver.gettransaction(txid)['confirmations'], 1)
        self.record('real-grouped-two-hop-P2P-relay-and-independent-mined-block', txid=txid, tip=self.tips())
        native_txid = node.sendrawtransaction(comparison_raw)
        self.sync_all()
        self.generatetoaddress(validator, 1, reward)
        assert all(native_txid in peer.getblock(peer.getbestblockhash())['tx'] for peer in self.nodes)
        self.record('existing-v2-authorization-accepted-after-activation', txid=native_txid, tip=self.tips())
        saved_balance = receiver.getbalance()
        saved_tip = self.tips()
        self.restart_node(1)
        self.connect_nodes(0, 1)
        self.connect_nodes(1, 2)
        self.sync_all()
        receiver = relay.get_wallet_rpc('receiver')
        assert_equal(receiver.getbalance(), saved_balance)
        assert receiver.getaddressinfo(foreign)['pq_group_authenticated']
        assert_equal(self.tips(), saved_tip)
        self.record('receiving-wallet-process-restart-after-grouped-confirmation', balance=saved_balance, tip=self.tips())
        self.foreign_commitment_negative(sender, standard, recipient, grouped_tx, first)

        refill = self.fund(sender, 'v2', [], [{group0: Decimal('5')}, {group1: Decimal('5')}])
        refill_raw, _ = self.finish(sender, refill['psbt'])
        node.sendrawtransaction(refill_raw)
        self.mine(1, standard)
        mixed_points = owned_group_points() + [v2_points[0]]
        # The comparison consumed v2_points; obtain an actually unspent v2 coin.
        native = next(coin for coin in sender.listunspent() if self.version(node.gettxout(coin['txid'], coin['vout'])['scriptPubKey']) == 2)
        mixed_points[-1] = dict(txid=native['txid'], vout=native['vout'])
        mixed = self.fund(sender, 'automatic', mixed_points, {recipient: Decimal('1')})
        mixed_raw, _ = self.finish(sender, mixed['psbt'])
        assert set(self.versions(node.decoderawtransaction(mixed_raw))) == {2, 3}
        mixed_id = node.sendrawtransaction(mixed_raw)
        self.sync_all()
        self.mine(1, standard)
        self.record('real-Automatic-mixed-v2-v3-relay-and-confirmation', txid=mixed_id, tip=self.tips())

        refill = self.fund(sender, 'v2', [], [{group0: Decimal('4')}, {group1: Decimal('4')}])
        refill_raw, _ = self.finish(sender, refill['psbt'])
        node.sendrawtransaction(refill_raw)
        self.mine(1, standard)
        pending = self.fund(sender, 'v3', owned_group_points(), {recipient: Decimal('1')})
        pending_raw, _ = self.finish(sender, pending['psbt'])
        pending_id = node.sendrawtransaction(pending_raw)
        self.sync_all()
        saved_tip = self.tips()
        self.restart_node(2)
        self.connect_nodes(1, 2)
        self.sync_all()
        assert pending_id in validator.getrawmempool()
        assert_equal(self.tips(), saved_tip)
        self.record('validator-process-restart-and-grouped-mempool-persistence', txid=pending_id, tip=self.tips())
        node.unloadwallet('sender')
        node.loadwallet('sender')
        sender = node.get_wallet_rpc('sender')
        sender.rescanblockchain()
        assert sender.getaddressinfo(group0)['pq_group_authenticated']
        assert pending_id in node.getrawmempool()
        self.record('wallet-reload-rescan-authenticated-known-ranges', authenticated=True, txid=pending_id)
        self.restart_node(2, extra_args=self.extra_args[2] + ['-reindex'])
        self.connect_nodes(1, 2)
        self.sync_all()
        assert_equal(self.tips(), saved_tip)
        assert validator.verifychain(4, 0)
        assert pending_id in validator.getrawmempool()
        self.record('grouped-history-reindex-and-level4-replay', tip=self.tips())

        self.disconnect_nodes(1, 2)
        self.generatetoaddress(node, 1, standard, sync_fun=self.no_op)
        self.sync_blocks(self.nodes[:2])
        confirmed_hash = node.getbestblockhash()
        assert pending_id in node.getblock(confirmed_hash)['tx']
        validator.prioritisetransaction(pending_id, 0, -100000000)
        self.generatetoaddress(validator, 2, reward, sync_fun=self.no_op)
        assert all(pending_id not in validator.getblock(validator.getblockhash(height))['tx']
                   for height in [saved_tip['height'] + 1, saved_tip['height'] + 2])
        self.connect_nodes(1, 2)
        self.sync_all()
        assert pending_id in node.getrawmempool()
        assert_equal(sender.gettransaction(pending_id)['confirmations'], 0)
        self.record('real-competing-chain-reorg-and-disconnected-grouped-mempool-return',
                    disconnected_block=confirmed_hash, txid=pending_id, tip=self.tips())
        validator.prioritisetransaction(pending_id, 0, 100000000)
        self.generatetoaddress(validator, 1, reward)
        assert all(pending_id in peer.getblock(peer.getbestblockhash())['tx'] for peer in self.nodes)
        assert_raises_rpc_error(-25, 'Inputs missing or spent', sender.walletprocesspsbt, pending['psbt'])
        self.record('reconfirmation-stale-PSBT-rejection-and-coin-reconciliation', txid=pending_id, tip=self.tips())
        assert_equal(owned_group_points(), [])
        for vin in node.decoderawtransaction(pending_raw)['vin']:
            assert all(peer.gettxout(vin['txid'], vin['vout']) is None for peer in self.nodes)
        assert_equal(sum((coin['amount'] for coin in sender.listunspent()), Decimal(0)), sender.getbalance())
        assert_equal(receiver.gettransaction(pending_id)['confirmations'], 1)
        for peer in self.nodes:
            assert peer.verifychain(4, 0)
        self.record('final-independent-validation-and-PQ-network-agreement', tip=self.tips(), nodes=self.network())


if __name__ == '__main__':
    MercaWeaveMultinodeTest(__file__).main()
