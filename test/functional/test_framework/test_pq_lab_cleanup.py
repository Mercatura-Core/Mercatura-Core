#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Real owned-child cleanup on normal return and a watchdog termination signal."""
import signal
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from test_framework.pq_lab_proxy import run_owned_test  # noqa: E402


class CleanupTests(unittest.TestCase):
    def exercise(self, terminate, clear_at_shutdown=False):
        children = []
        fixture = SimpleNamespace(nodes=[])

        def main():
            child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'],
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            children.append(child)
            fixture.nodes.append(SimpleNamespace(process=child, running=True, rpc_connected=True))
            try:
                if terminate:
                    signal.raise_signal(signal.SIGTERM)
            finally:
                if clear_at_shutdown:
                    fixture.shutdown()

        fixture.main = main
        if clear_at_shutdown:
            fixture.shutdown = lambda: fixture.nodes.clear()
        original = signal.getsignal(signal.SIGTERM)
        if terminate:
            with self.assertRaises(SystemExit) as raised:
                run_owned_test(fixture)
            self.assertEqual(raised.exception.code, 128 + signal.SIGTERM)
        else:
            run_owned_test(fixture)
        self.assertEqual(signal.getsignal(signal.SIGTERM), original)
        self.assertIsNotNone(children[0].poll())
        if not clear_at_shutdown:
            self.assertIsNone(fixture.nodes[0].process)
            self.assertFalse(fixture.nodes[0].running)
            self.assertFalse(fixture.nodes[0].rpc_connected)

    def test_normal_return_reaps_child(self):
        self.exercise(False)

    def test_watchdog_signal_reaps_child(self):
        self.exercise(True)

    def test_framework_clears_nodes_before_return(self):
        self.exercise(False, clear_at_shutdown=True)

    def test_framework_clears_nodes_on_termination(self):
        self.exercise(True, clear_at_shutdown=True)


if __name__ == '__main__':
    unittest.main()
