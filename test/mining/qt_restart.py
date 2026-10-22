#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Run real Qt Close twice, reopening the same isolated V2 chain and disk wallet."""
import os
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="mercatura-v2-qt-restart-") as datadir:
    env = dict(os.environ, MERCATURA_TEST_DATADIR=datadir)
    for restart in (False, True):
        if restart:
            env["MERCATURA_TEST_RESTART"] = "1"
        result = subprocess.run([sys.argv[1]], env=env, timeout=90, check=False)
        if result.returncode:
            sys.exit(result.returncode)
