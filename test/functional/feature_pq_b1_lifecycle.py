#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""B1 local configuration failures followed by ordinary PQ identity/relay lifecycle."""

from feature_pq_transport import PQTransportTest


class PQB1LifecycleTest(PQTransportTest):
    def set_test_params(self):
        super().set_test_params()
        # The classical rejection fixture must share the selected regtest genesis
        # when it is temporarily restarted for local configuration validation.
        self.extra_args[2] += [f'-mercahashv2height={self.options.mercahash_v2_height}']

    def run_test(self):
        self.stop_node(2)
        fingerprint = 'mcapq1:' + 'a' * 96  # Public synthetic configuration value.
        invalid_pin = "Error: Invalid -pqp2ppin: require exact host:port (bracket IPv6), '=' and mcapq1: with 96 lowercase hex digits."
        for destination in ['localhost', 'localhost:0', 'localhost:65536', 'localhost:-1',
                            'localhost:abc', 'host name:18444', '2001:db8::1:18444']:
            self.nodes[2].assert_start_raises_init_error(
                extra_args=self.pq_args + [f'-pqp2ppin={destination}={fingerprint}'], expected_msg=invalid_pin)
        for value in [fingerprint[:-1], fingerprint.upper(), 'mcapq1:' + 'g' * 96]:
            self.nodes[2].assert_start_raises_init_error(
                extra_args=self.pq_args + [f'-pqp2ppin=localhost:18444={value}'], expected_msg=invalid_pin)
        binding = f'-pqp2ppin=localhost:18444={fingerprint}'
        self.nodes[2].assert_start_raises_init_error(
            extra_args=self.pq_args + [binding, binding], expected_msg='Error: Duplicate PQ pin destination.')
        for filename in ['', '../identity.dat', 'child/identity.dat', '/absolute-identity-not-used.dat']:
            self.nodes[2].assert_start_raises_init_error(
                extra_args=self.pq_args + [f'-pqp2pidentity={filename}'],
                expected_msg='Error: -pqp2pidentity must be a filename in the selected network datadir.')
        self.nodes[2].assert_start_raises_init_error(
            extra_args=self.pq_args + ['-pqp2pidentitycreate=1'],
            expected_msg='Error: -pqp2pidentitycreate requires an explicit -pqp2pidentity path.')
        self.start_node(2)
        self.log.info('B1 invalid pin/path configuration and duplicate binding checks passed')
        super().run_test()


if __name__ == '__main__':
    PQB1LifecycleTest(__file__).main()
