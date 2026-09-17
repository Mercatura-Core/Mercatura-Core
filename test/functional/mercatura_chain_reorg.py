#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native competing-chain reorg functional test."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_not_equal


class MercaturaChainReorgTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.uses_wallet = True

    def mine_blocks_batched(self, node, count, address, batch_size=10):
        hashes = []

        while count > 0:
            batch = min(count, batch_size)

            hashes.extend(
                self.generatetoaddress(
                    node,
                    batch,
                    address,
                )
            )

            count -= batch

        return hashes

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]

        self.log.info("Creating PQ mining wallets")

        node0.createwallet(wallet_name="miner0")
        node1.createwallet(wallet_name="miner1")

        miner0 = node0.get_wallet_rpc("miner0")
        miner1 = node1.get_wallet_rpc("miner1")

        address0 = miner0.getnewaddress()
        address1 = miner1.getnewaddress()

        assert address0.startswith("mcrt1z")
        assert address1.startswith("mcrt1z")

        #
        # Establish common chain.
        #
        self.log.info("Mining common Mercatura chain")

        self.mine_blocks_batched(
            node0,
            20,
            address0,
        )

        self.sync_blocks()

        common_height = node0.getblockcount()
        common_tip = node0.getbestblockhash()

        assert_equal(common_height, 20)
        assert_equal(node1.getblockcount(), common_height)
        assert_equal(node1.getbestblockhash(), common_tip)

        #
        # Split network.
        #
        self.log.info("Disconnecting nodes")

        self.disconnect_nodes(0, 1)

        #
        # Build competing branches.
        #
        self.log.info("Mining shorter branch on node 0")

        branch0 = self.generatetoaddress(
            node0,
            2,
            address0,
            sync_fun=self.no_op,
        )

        self.log.info("Mining longer branch on node 1")

        branch1 = self.generatetoaddress(
            node1,
            3,
            address1,
            sync_fun=self.no_op,
        )

        assert_equal(len(branch0), 2)
        assert_equal(len(branch1), 3)

        assert_equal(node0.getblockcount(), 22)
        assert_equal(node1.getblockcount(), 23)

        tip0_before = node0.getbestblockhash()
        tip1_before = node1.getbestblockhash()

        assert_not_equal(tip0_before, tip1_before)

        #
        # Reconnect and allow most-work chain selection.
        #
        self.log.info("Reconnecting nodes")

        self.connect_nodes(0, 1)

        self.sync_blocks()

        final_height0 = node0.getblockcount()
        final_height1 = node1.getblockcount()

        final_tip0 = node0.getbestblockhash()
        final_tip1 = node1.getbestblockhash()

        self.log.info(
            f"Final node 0 height/tip: "
            f"{final_height0} / {final_tip0}"
        )

        self.log.info(
            f"Final node 1 height/tip: "
            f"{final_height1} / {final_tip1}"
        )

        assert_equal(final_height0, 23)
        assert_equal(final_height1, 23)
        assert_equal(final_tip0, final_tip1)

        #
        # Node 1's former tip should be the winning active tip.
        #
        assert_equal(
            final_tip0,
            tip1_before,
        )

        #
        # Node 0's losing branch tip should still be known but not active.
        #
        losing_header = node0.getblockheader(
            tip0_before,
        )

        assert_equal(
            losing_header["confirmations"],
            -1,
        )

        #
        # Common ancestor remains part of active chain.
        #
        common_header = node0.getblockheader(
            common_tip,
        )

        assert common_header["confirmations"] > 0

        self.log.info(
            "Mercatura native competing-chain reorg passed"
        )


if __name__ == "__main__":
    MercaturaChainReorgTest(__file__).main()
