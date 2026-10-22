#!/usr/bin/env bash
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
export LC_ALL=C
set -euo pipefail
qualification_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$qualification_root/contrib/devtools/qualify-mercahash.py" "$@" --mode combined
