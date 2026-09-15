#!/usr/bin/env python3
# Copyright (c) 2014-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the wallet accounts properly when there is a double-spend conflict."""
from decimal import Decimal

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
)


class TxnMallTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def add_options(self, parser):
        parser.add_argument("--mineblock", dest="mine_block", default=False, action="store_true",
                            help="Test double-spend of 1-confirmed transaction")

    def setup_network(self):
        # Start with split network:
        super().setup_network()
        self.disconnect_nodes(1, 2)

    def spend_utxo(self, utxo, outputs):
        inputs = [utxo]
        tx = self.nodes[0].createrawtransaction(inputs, outputs)
        tx = self.nodes[0].fundrawtransaction(tx)
        tx = self.nodes[0].signrawtransactionwithwallet(tx['hex'])
        return self.nodes[0].sendrawtransaction(tx['hex'])

    def maturing_coinbase_value(self, node, base_height, blocks_ahead):
        """Return the coinbase value that matures after blocks_ahead blocks."""
        coinbase_height = base_height + blocks_ahead - COINBASE_MATURITY
        block = node.getblock(node.getblockhash(coinbase_height), 2)
        return sum(output["value"] for output in block["tx"][0]["vout"])

    def run_test(self):
        # Use Mercatura's actual cached-chain balance and the exact historical
        # coinbase rewards that will mature during this test.
        starting_balance = self.nodes[0].getbalance()
        base_height = self.nodes[0].getblockcount()
        maturing_reward_1 = self.maturing_coinbase_value(
            self.nodes[0], base_height, 1
        )
        maturing_reward_2 = self.maturing_coinbase_value(
            self.nodes[0], base_height, 2
        )

        # All nodes should be out of IBD.
        # If the nodes are not all out of IBD, that can interfere with
        # blockchain sync later in the test when nodes are connected, due to
        # timing issues.
        for n in self.nodes:
            assert n.getblockchaininfo()["initialblockdownload"] == False

        for i in range(3):
            assert_equal(self.nodes[i].getbalance(), starting_balance)

        # Assign coins to foo and bar addresses:
        node0_address_foo = self.nodes[0].getnewaddress()
        fund_foo_utxo = self.create_outpoints(self.nodes[0], outputs=[{node0_address_foo: 1219}])[0]
        fund_foo_tx = self.nodes[0].gettransaction(fund_foo_utxo['txid'])
        self.nodes[0].lockunspent(False, [fund_foo_utxo])

        node0_address_bar = self.nodes[0].getnewaddress()
        fund_bar_utxo = self.create_outpoints(node=self.nodes[0], outputs=[{node0_address_bar: 29}])[0]
        fund_bar_tx = self.nodes[0].gettransaction(fund_bar_utxo['txid'])

        assert_equal(self.nodes[0].getbalance(),
                     starting_balance + fund_foo_tx["fee"] + fund_bar_tx["fee"])

        # Coins are sent to node1_address
        node1_address = self.nodes[1].getnewaddress()

        # First: use the raw transaction API to create the conflicting
        # transaction, but don't broadcast it yet. Mercatura PQ authorization
        # is much larger than inherited ECDSA authorization, so derive the
        # minimum size-based fee from the signed transaction itself instead
        # of assuming Bitcoin's old fixed 0.02-coin fixture fee.
        inputs = [fund_foo_utxo, fund_bar_utxo]
        change_address = self.nodes[0].getnewaddress()

        provisional_fee = Decimal("-0.02")
        outputs = {
            node1_address: 1240,
            change_address: 1248 - 1240 + provisional_fee,
        }

        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        doublespend = self.nodes[0].signrawtransactionwithwallet(rawtx)
        assert_equal(doublespend["complete"], True)

        doublespend_vsize = self.nodes[0].decoderawtransaction(
            doublespend["hex"]
        )["vsize"]

        fee_units = (doublespend_vsize + 999) // 1000
        doublespend_fee = -Decimal(fee_units) * Decimal("0.01")

        outputs[change_address] = 1248 - 1240 + doublespend_fee

        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        doublespend = self.nodes[0].signrawtransactionwithwallet(rawtx)
        assert_equal(doublespend["complete"], True)

        # Create two spends using 1 50 BTC coin each
        txid1 = self.spend_utxo(fund_foo_utxo, {node1_address: 40})
        txid2 = self.spend_utxo(fund_bar_utxo, {node1_address: 20})

        # Have node0 mine a block:
        if (self.options.mine_block):
            self.generate(self.nodes[0], 1, sync_fun=lambda: self.sync_blocks(self.nodes[0:2]))

        tx1 = self.nodes[0].gettransaction(txid1)
        tx2 = self.nodes[0].gettransaction(txid2)

        # Node0's balance should include the exact Mercatura coinbase
        # reward that matured after the optional additional block.
        expected = starting_balance + fund_foo_tx["fee"] + fund_bar_tx["fee"]
        if self.options.mine_block:
            expected += maturing_reward_1
        expected += tx1["amount"] + tx1["fee"]
        expected += tx2["amount"] + tx2["fee"]
        assert_equal(self.nodes[0].getbalance(), expected)

        if self.options.mine_block:
            assert_equal(tx1["confirmations"], 1)
            assert_equal(tx2["confirmations"], 1)
            # Node1's balance should be both transaction amounts:
            assert_equal(self.nodes[1].getbalance(), starting_balance - tx1["amount"] - tx2["amount"])
        else:
            assert_equal(tx1["confirmations"], 0)
            assert_equal(tx2["confirmations"], 0)

        # Now give doublespend and its parents to miner:
        self.nodes[2].sendrawtransaction(fund_foo_tx["hex"])
        self.nodes[2].sendrawtransaction(fund_bar_tx["hex"])
        doublespend_txid = self.nodes[2].sendrawtransaction(doublespend["hex"])
        # ... mine a block...
        self.generate(self.nodes[2], 1, sync_fun=self.no_op)

        # Reconnect the split network, and sync chain:
        self.connect_nodes(1, 2)
        self.generate(self.nodes[2], 1)  # Mine another block to make sure we sync
        assert_equal(self.nodes[0].gettransaction(doublespend_txid)["confirmations"], 2)

        # Re-fetch transaction info:
        tx1 = self.nodes[0].gettransaction(txid1)
        tx2 = self.nodes[0].gettransaction(txid2)

        # Both transactions should be conflicted
        assert_equal(tx1["confirmations"], -2)
        assert_equal(tx2["confirmations"], -2)

        # Node0's final balance includes the two exact Mercatura coinbase
        # rewards that matured on the winning chain, minus the double-spend,
        # plus the transaction fees.
        expected = (
            starting_balance
            + maturing_reward_1
            + maturing_reward_2
            - 1240
            + fund_foo_tx["fee"]
            + fund_bar_tx["fee"]
            + doublespend_fee
        )
        assert_equal(self.nodes[0].getbalance(), expected)

        # Node1 receives the double-spend amount on top of its initial
        # Mercatura cached-chain balance.
        assert_equal(self.nodes[1].getbalance(), starting_balance + 1240)


if __name__ == '__main__':
    TxnMallTest(__file__).main()
