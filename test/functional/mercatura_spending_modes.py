#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license.

"""Native funding policies use actual PQ coins and signatures on isolated regtest."""
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class SpendingModesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.wallet_names = [False]
        self.rpc_timeout = 240
        self.extra_args = [["-mercaweaveheight=0", "-pqp2p=required"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    @staticmethod
    def version(script):
        raw = bytes.fromhex(script["hex"])
        assert_equal(len(raw), 34)
        assert_equal(raw[1], 32)
        assert raw[0] in (0x52, 0x53)
        return raw[0] - 0x50

    def versions(self, decoded):
        return [self.version(self.nodes[0].gettxout(vin["txid"], vin["vout"])["scriptPubKey"])
                for vin in decoded["vin"]]

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("policy")
        wallet = node.get_wallet_rpc("policy")
        standard = wallet.getnewaddress()
        group0 = wallet.getnewaddress("group0", "mercaweave")
        group1 = wallet.getnewaddress("group1", "mercaweave")
        blocks = []
        for address in [group0, group1, standard]:
            blocks += self.generatetoaddress(node, 1, address)
        for _ in range(11):
            self.generatetoaddress(node, 10, standard)
        coins = [{"txid": node.getblock(block, 2)["tx"][0]["txid"], "vout": 0} for block in blocks]

        def funded(mode, inputs, destination=standard):
            return wallet.walletcreatefundedpsbt(inputs, {destination: Decimal("1")},
                options={"spending_mode": mode, "add_inputs": not inputs, "fee_rate": Decimal("0.001")})

        def signed(result):
            processed = wallet.walletprocesspsbt(result["psbt"])
            assert processed["complete"]
            final = node.finalizepsbt(processed["psbt"])
            assert final["complete"]
            acceptance = node.testmempoolaccept([final["hex"]])[0]
            assert acceptance["allowed"], acceptance
            return final["hex"]

        self.log.info("Explicit v2 funding excludes grouped coins even for a v3 recipient")
        v2 = funded("v2", [], group0)
        decoded = node.decodepsbt(v2["psbt"])["tx"]
        assert set(self.versions(decoded)) == {2}
        assert_equal(self.version(decoded["vout"][v2["changepos"]]["scriptPubKey"]), 2)
        signed(v2)

        self.log.info("Conflicting manual inputs fail at the native funding boundary")
        assert_raises_rpc_error(-4, "conflicts with the spending mode", funded, "v2", [coins[0]])
        assert_raises_rpc_error(-4, "conflicts with the spending mode", funded, "v3", [coins[2]])
        assert_raises_rpc_error(-8, "Unknown spending_mode", funded, "invalid", [])
        assert_raises_rpc_error(-4, "v2", wallet.walletcreatefundedpsbt, [], {standard: wallet.getbalance() + 1},
                                options={"spending_mode": "v2", "fee_rate": Decimal("0.001")})

        self.log.info("Automatic retains real mixed-input signing and unchanged defaults")
        mixed = funded("automatic", [coins[0], coins[2]])
        assert set(self.versions(node.decodepsbt(mixed["psbt"])["tx"])) == {2, 3}
        signed(mixed)
        default = wallet.walletcreatefundedpsbt([coins[0], coins[2]], {standard: Decimal("1")},
                                              options={"add_inputs": False, "fee_rate": Decimal("0.001")})
        signed(default)

        self.log.info("V3-only uses one grouped authorization and default v2 change")
        grouped = funded("v3", coins[:2])
        decoded = node.decodepsbt(grouped["psbt"])["tx"]
        assert_equal(self.versions(decoded), [3, 3])
        assert_equal(self.version(decoded["vout"][grouped["changepos"]]["scriptPubKey"]), 2)
        raw = signed(grouped)
        transaction = node.decoderawtransaction(raw)
        assert_equal(sorted(len(vin["txinwitness"]) for vin in transaction["vin"]), [2, 3])

        unsigned = node.createrawtransaction(coins[:2], {standard: Decimal("1")})
        raw_funded = wallet.fundrawtransaction(unsigned, {"spending_mode": "v3", "add_inputs": False, "fee_rate": Decimal("0.001")})
        assert_equal(self.versions(node.decoderawtransaction(raw_funded["hex"])), [3, 3])
        txid = node.sendrawtransaction(raw)
        self.generatetoaddress(node, 1, standard)
        assert_equal(wallet.gettransaction(txid)["confirmations"], 1)
        assert wallet.getbalance() > 1
        assert_raises_rpc_error(-4, "v3", funded, "v3", [])


if __name__ == "__main__":
    SpendingModesTest(__file__).main()
