#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Render an operator-verified PQ testnet bootstrap inventory without networking.

This validates inventory syntax and stated evidence, not operator independence
or cryptographic authenticity. It never probes, installs or modifies live nodes.
"""
import argparse
from datetime import datetime, timezone
import ipaddress
import json
from pathlib import Path
import re

GENESIS = 'b3fc707c9a89dc45d1cf6a837284b22ad2b6fd41b99af601079db9dcbeff6920'


def render(inventory, lab=False):
    if set(inventory) != {'chain', 'genesis', 'operators'} or inventory['chain'] != 'test' or inventory['genesis'] != GENESIS:
        raise ValueError('Require the existing Mercatura public-testnet chain and genesis')
    operators = inventory['operators']
    if not isinstance(operators, list) or not 2 <= len(operators) <= 16:
        raise ValueError('Require 2..16 separately identified operators')
    seen_operators, destinations = set(), set()
    lines = ['# Generated from stated operator evidence; no live validation or deployment performed.',
             '# LAB ONLY' if lab else '# Verify operator independence and every endpoint before deployment.',
             '# Activation/release/deployment require separate founder approval.',
             'testnet=1', '[test]', 'pqp2p=required']
    for operator in operators:
        if set(operator) != {'operator', 'endpoints'}:
            raise ValueError('Unknown operator fields are forbidden')
        name = operator['operator']
        if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9 ._-]{0,79}', name) or name.casefold() in seen_operators:
            raise ValueError('Require distinct, single-line operator identifiers')
        seen_operators.add(name.casefold())
        records = operator['endpoints']
        if not isinstance(records, list) or not 1 <= len(records) <= 4:
            raise ValueError('Require 1..4 endpoints per operator')
        lines.append('# Operator: ' + name)
        for record in records:
            if set(record) - {'host', 'port', 'fingerprint', 'pq_verified_at_utc', 'source_commit'} or not {'host', 'port', 'pq_verified_at_utc', 'source_commit'} <= set(record):
                raise ValueError('Missing endpoint evidence or unknown endpoint fields')
            host, port = record['host'], record['port']
            if not isinstance(host, str) or not isinstance(port, int) or isinstance(port, bool) or not 1 <= port <= 65535:
                raise ValueError('Invalid endpoint host or port')
            try:
                address = ipaddress.ip_address(host)
            except ValueError:
                if lab or '.' not in host or host.endswith(('.local','.localhost','.invalid','.test','.example')) or len(host) > 253 or not all(re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?', part) for part in host.split('.')):
                    raise ValueError('Require canonical numeric IP or lowercase DNS name') from None
                address = None
            if address and ((lab and not address.is_loopback) or (not lab and not address.is_global)):
                raise ValueError('Lab endpoints must be loopback; deployment inventories require global endpoints')
            if address and str(address) != host:
                raise ValueError('Require canonical numeric IP spelling')
            destination = ('[' + host + ']' if address and address.version == 6 else host) + ':' + str(port)
            if destination in destinations:
                raise ValueError('An endpoint cannot count as two independent operators')
            destinations.add(destination)
            if not re.fullmatch(r'[0-9a-f]{40}', record['source_commit']):
                raise ValueError('Require full verified source commit')
            if not isinstance(record['pq_verified_at_utc'], str):
                raise ValueError('Require a UTC verification time string')
            verified = datetime.fromisoformat(record['pq_verified_at_utc'].replace('Z', '+00:00'))
            if verified.utcoffset() != timezone.utc.utcoffset(verified) or verified > datetime.now(timezone.utc):
                raise ValueError('Require a nonfuture UTC verification time')
            lines.append('seednode=' + destination)
            if 'fingerprint' in record:
                fingerprint = record['fingerprint']
                if not isinstance(fingerprint, str) or not re.fullmatch(r'mcapq1:[0-9a-f]{96}', fingerprint):
                    raise ValueError('Invalid public identity fingerprint')
                lines.append('pqp2ppin=' + destination + '=' + fingerprint)
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path, help='New configuration file; existing files are never replaced')
    parser.add_argument('--lab', action='store_true', help='Only loopback endpoints; labels output LAB ONLY')
    args = parser.parse_args()
    try:
        if args.inventory.stat().st_size > 65536:
            raise ValueError('Inventory exceeds the 64-KiB bound')
        text = render(json.loads(args.inventory.read_text(encoding='utf8')), args.lab)
        with args.output.open('x', encoding='utf8') as stream:
            stream.write(text)
    except (OSError, TypeError, KeyError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
