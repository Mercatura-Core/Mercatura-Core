#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Bounded storage upgrade qualification using generated, secret-free chain copies.

Create a fixture with the pre-storage-fix binary, then run the patched binary
with --legacy-fixture. Without arguments, exercise same-version persistence.
Never opens a supplied fixture as a live datadir or copies its wallets.
"""
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import shutil
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def manifest(directory):
    return {str(path.relative_to(directory)): digest(path)
            for path in sorted(directory.rglob('*')) if path.is_file()}


class SecurityStorageCompatibility(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument('--create-legacy-fixture', type=Path)
        parser.add_argument('--legacy-fixture', type=Path)
        parser.add_argument('--legacy-source-sha', default='same-version generated fixture')

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.extra_args = [['-dnsseed=0', '-fixedseeds=0', '-discover=0', '-connect=0', '-natpmp=0']]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def snapshot(self):
        node = self.nodes[0]
        chain = node.getblockchaininfo()
        coins = node.gettxoutsetinfo()
        return dict(height=chain['blocks'], tip=chain['bestblockhash'], chainwork=chain['chainwork'],
                    utxo_hash=coins['hash_serialized_3'], txouts=coins['txouts'],
                    amount=str(coins['total_amount']),
                    hashes=[node.getblockhash(height) for height in range(chain['blocks'] + 1)])

    def create_fixture(self, directory):
        node = self.nodes[0]
        node.createwallet('storage-compat')
        wallet = node.get_wallet_rpc('storage-compat')
        address = wallet.getnewaddress()
        for count in [10] * 10 + [1]:
            self.generatetoaddress(node, count, address)
        txid = wallet.sendtoaddress(wallet.getnewaddress(), Decimal('1.00'))
        spending_block = self.generatetoaddress(node, 1, address)[0]
        self.generatetoaddress(node, 1, address)
        assert_equal(wallet.gettransaction(txid)['confirmations'], 2)
        expected = self.snapshot()
        writer = dict(source_sha=self.options.legacy_source_sha,
                      binary_sha256=digest(Path(self.binary_paths.bitcoind)))
        self.stop_node(0)
        directory.mkdir(parents=True, exist_ok=False)
        for name in ['blocks', 'chainstate']:
            shutil.copytree(node.chain_path / name, directory / name)
        (directory / 'expected.json').write_text(json.dumps(dict(writer=writer, snapshot=expected,
                                                                spending_block=spending_block), indent=2) + '\n')
        self.log.info('Created old-format chain fixture at height %d; wallets and identities excluded', expected['height'])

    def restart_import(self, flag, height):
        self.stop_node(0)
        node = self.nodes[0]
        node.start(extra_args=self.extra_args[0] + [flag])
        node.wait_for_rpc_connection(wait_for_import=False)
        last_height = -1
        last_progress = time.monotonic()

        def imported():
            nonlocal last_height, last_progress
            current = node.getblockcount()
            if current != last_height:
                last_height, last_progress = current, time.monotonic()
            assert time.monotonic() - last_progress < 60 * self.options.timeout_factor
            return current == height and node.getmempoolinfo()['loaded']

        self.wait_until(imported, timeout=300)

    def run_test(self):
        assert not (self.options.create_legacy_fixture and self.options.legacy_fixture)
        if self.options.create_legacy_fixture:
            self.create_fixture(self.options.create_legacy_fixture)
            return
        fixture = self.options.legacy_fixture
        if fixture is None:
            fixture = Path(self.options.tmpdir) / 'generated-chain-fixture'
            self.create_fixture(fixture)
        else:
            self.stop_node(0)
        before = manifest(fixture)
        record = json.loads((fixture / 'expected.json').read_text())
        expected = record['snapshot']
        node = self.nodes[0]
        assert node.chain_path.is_relative_to(Path(self.options.tmpdir))
        for name in ['blocks', 'chainstate']:
            destination = node.chain_path / name
            if destination.exists():
                shutil.rmtree(destination)  # This test's own stopped, disposable node only.
            shutil.copytree(fixture / name, destination)
        self.start_node(0)
        assert_equal(self.snapshot(), expected)
        assert node.verifychain(4, 0)
        self.restart_node(0)
        assert_equal(self.snapshot(), expected)

        # Disconnect the old PQ spending block and its successor, exercising
        # existing undo records, then restore precisely the old chain and UTXOs.
        node.invalidateblock(record['spending_block'])
        assert_equal(node.getblockcount(), expected['height'] - 2)
        node.reconsiderblock(record['spending_block'])
        assert_equal(self.snapshot(), expected)

        # Mix a new-format writer's block/undo metadata into the old database.
        self.generateblock(node, output='raw(6a)', transactions=[])
        extended = self.snapshot()
        assert_equal(extended['hashes'][:-1], expected['hashes'])
        for flag in ['-reindex-chainstate', '-reindex']:
            self.restart_import(flag, extended['height'])
            assert_equal(self.snapshot(), extended)
            assert node.verifychain(4, 0)
        self.restart_node(0)
        assert_equal(self.snapshot(), extended)
        assert_equal(manifest(fixture), before)
        self.log.info('Storage upgrade, history/UTXO/chainwork, undo reorg, both reindexes and restart passed; fixture unchanged')


if __name__ == '__main__':
    SecurityStorageCompatibility(__file__).main()
