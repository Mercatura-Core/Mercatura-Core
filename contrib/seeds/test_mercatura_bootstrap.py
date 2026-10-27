#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Offline bootstrap inventory security regressions; all operators are fixtures."""
import copy
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).with_name('mercatura-bootstrap.py')
SPEC = importlib.util.spec_from_file_location('mercatura_bootstrap', TOOL)
BOOTSTRAP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BOOTSTRAP)


class BootstrapInventoryTests(unittest.TestCase):
    def setUp(self):
        now = datetime.now(timezone.utc).isoformat()
        self.inventory = {'chain': 'test', 'genesis': BOOTSTRAP.GENESIS, 'operators': [
            {'operator': name, 'endpoints': [{'host': '127.0.0.1', 'port': port,
                'source_commit': 'a' * 40, 'pq_verified_at_utc': now}]}
            for name, port in [('lab-A', 12345), ('lab-B', 12346)]]}

    def test_configuration_injection_and_malformed_evidence(self):
        for key, value in [('host', '127.0.0.1\npqp2p=off'), ('host', 'https://localhost/'),
                           ('host', 'localhost'), ('host', '203.0.113.1'), ('port', True),
                           ('port', 0), ('port', 65536), ('fingerprint', 'mcapq1:' + 'A' * 96),
                           ('source_commit', 'a' * 12), ('pq_verified_at_utc', 7),
                           ('pq_verified_at_utc', '2999-01-01T00:00:00+00:00')]:
            with self.subTest(field=key, value=value):
                inventory = copy.deepcopy(self.inventory)
                inventory['operators'][0]['endpoints'][0][key] = value
                with self.assertRaises((ValueError, TypeError)):
                    BOOTSTRAP.render(inventory, lab=True)

    def test_operator_and_chain_claims_cannot_weaken_policy(self):
        for mutation in range(6):
            inventory = copy.deepcopy(self.inventory)
            if mutation == 0:
                inventory['operators'][0]['operator'] = 'lab-A\npqp2p=off'
            elif mutation == 1:
                inventory['operators'][1]['operator'] = 'LAB-a'
            elif mutation == 2:
                inventory['operators'][1]['endpoints'][0]['port'] = 12345
            elif mutation == 3:
                inventory['chain'] = 'main'
            elif mutation == 4:
                inventory['genesis'] = '0' * 64
            else:
                inventory['operators'][0]['endpoints'][0]['private_seed'] = 'public-test-rejection-sentinel'
            with self.subTest(mutation=mutation), self.assertRaises((ValueError, TypeError)):
                BOOTSTRAP.render(inventory, lab=True)

    def test_exclusive_creation_and_size_bound(self):
        expected = BOOTSTRAP.render(self.inventory, lab=True)
        self.assertEqual(expected.count('seednode='), 2)
        self.assertIn('testnet=1', expected)
        self.assertIn('# LAB ONLY', expected)
        self.assertNotIn('pqp2p=off', expected)
        with tempfile.TemporaryDirectory(prefix='pq-bootstrap-check-') as temp:
            root = Path(temp)
            inventory, output = root / 'public-fixture.json', root / 'new.conf'
            inventory.write_text(json.dumps(self.inventory))
            command = [sys.executable, str(TOOL), '--inventory', str(inventory), '--output', str(output), '--lab']
            subprocess.run(command, check=True, capture_output=True, timeout=10)
            self.assertEqual(output.read_text(), expected)
            retry = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertEqual(retry.returncode, 2)
            self.assertEqual(output.read_text(), expected)
            inventory.write_text(' ' * 65537)
            oversized = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertEqual(oversized.returncode, 2)
            self.assertIn('64-KiB', oversized.stderr)
            self.assertEqual(output.read_text(), expected)


if __name__ == '__main__':
    unittest.main()
