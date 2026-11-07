#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Run real Qt Close twice, reopening the same isolated V2 chain and disk wallet."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="mercatura-v2-qt-restart-") as datadir:
    records = Path(datadir) / "qt-mining-records.jsonl"
    env = dict(os.environ, MERCATURA_TEST_DATADIR=datadir, MERCATURA_TEST_RECORDS=str(records))
    for restart in (False, True):
        if restart:
            env["MERCATURA_TEST_RESTART"] = "1"
        try:
            result = subprocess.run([sys.argv[1]], env=env, timeout=90, check=False)
        except subprocess.TimeoutExpired:
            # Keep the existing deadline and expose bounded progress evidence
            # before TemporaryDirectory removes this disposable test state.
            print(f"Qt restart timed out (restart={restart}); recent test records:", file=sys.stderr)
            if records.exists():
                with records.open("rb") as stream:
                    stream.seek(max(0, records.stat().st_size - 16384))
                    print(stream.read().decode("utf-8", errors="replace"), file=sys.stderr)
            else:
                print("No application progress records were written.", file=sys.stderr)
            raise
        if result.returncode:
            sys.exit(result.returncode)
