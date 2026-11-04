#!/usr/bin/env python3
"""Ordinary-node wallet safety probe: import/inspect only; no v3 output funding."""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'test/functional'))
from test_framework.authproxy import JSONRPCException
from test_framework.segwit_addr import encode_segwit_address
from test_framework.test_framework import BitcoinTestFramework


class InactiveWalletSafety(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

    def add_options(self, parser):
        parser.add_argument('--evidence',required=True)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.createwallet('ordinary')
        node.createwallet('tracking',disable_private_keys=True,blank=True)
        ordinary = node.get_wallet_rpc('ordinary')
        tracking = node.get_wallet_rpc('tracking')
        rows = []
        for request in [None,'legacy','p2sh-segwit','bech32','bech32m']:
            address = ordinary.getnewaddress() if request is None else ordinary.getnewaddress('',request)
            info = ordinary.getaddressinfo(address)
            assert info['witness_version'] == 2 and info['ismine']
            rows.append(dict(request=request,witness_version=info['witness_version'],ismine=info['ismine']))
        change = ordinary.getaddressinfo(ordinary.getrawchangeaddress())
        assert change['witness_version'] == 2
        program = bytes([0x35])*32
        unsafe_address = encode_segwit_address('mcrt',3,program)
        raw = '5320'+program.hex()
        descriptor = node.getdescriptorinfo('raw('+raw+')')['descriptor']
        private_import = ordinary.importdescriptors([dict(desc=descriptor,timestamp='now')])[0]
        assert private_import['success'] is False
        tracked = tracking.importdescriptors([dict(desc=descriptor,timestamp='now',label='explicit-unprotected-watch')])[0]
        assert tracked['success']
        info = tracking.getaddressinfo(unsafe_address)
        assert tracking.getwalletinfo()['private_keys_enabled'] is False
        assert info['solvable'] is False
        try:
            tracking.getnewaddress()
        except JSONRPCException as error:
            receive_error = error.error
        else:
            raise AssertionError('watch-only raw v3 import enabled a receiving destination')
        active = tracking.importdescriptors([dict(desc=descriptor,timestamp='now',active=True)])[0]
        assert active['success'] is False
        data = dict(passed=True,native_receive_requests=rows,native_change_version=2,
            private_wallet_raw_v3_import=private_import,
            explicit_watch_only_import=tracked,
            watch_only_v3_information={key:info[key] for key in ['ismine','iswatchonly','solvable','witness_version'] if key in info},
            tracking_wallet_receive_error=receive_error,active_v3_import=active,
            v3_funding_outputs_created=0,v3_transactions_broadcast=0,
            grouped_network_authorization_tested=False)
        pathlib.Path(self.options.evidence).write_text(json.dumps(data,indent=2)+'\n')


if __name__ == '__main__':
    InactiveWalletSafety(__file__).main()
