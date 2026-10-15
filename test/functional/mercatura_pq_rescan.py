#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

"""Recover PQ receipts, change and rewards with filters, fallback and pruning."""

from decimal import Decimal, ROUND_DOWN
from pathlib import Path

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, util_xor


PASSPHRASE = "pq-rescan-test-passphrase"
COMMON_ARGS = ["-prune=1", "-fastprune=1", "-keypool=5", "-debug=scan", "-fallbackfee=0.01"]


class MercaturaPQRescanTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.rpc_timeout = 180
        self.extra_args = [self.node_args(indexed=True)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def node_args(self, *, indexed):
        return [*COMMON_ARGS, f"-blockfilterindex={int(indexed)}"]

    def mine(self, count, address):
        hashes = []
        while count:
            batch = min(10, count)
            hashes.extend(self.generatetoaddress(self.nodes[0], batch, address))
            count -= batch
        self.nodes[0].syncwithvalidationinterfacequeue()
        return hashes

    def wait_for_filters(self):
        node = self.nodes[0]
        self.wait_until(lambda: node.getindexinfo().get("basic block filter index") == {
            "synced": True, "best_block_height": node.getblockcount(),
        })

    def rescan(self, wallet, *heights):
        # Keep the existing explicit-rescan RPC unlock requirement. Automatic
        # backup recovery above/below still scans using locked public records.
        wallet.walletpassphrase(PASSPHRASE, 300)
        try:
            return wallet.rescanblockchain(*heights)
        finally:
            wallet.walletlock()

    def snapshot(self, wallet):
        coins = sorted((coin["txid"], coin["vout"], coin["amount"], coin["scriptPubKey"], coin["address"])
                       for coin in wallet.listunspent())
        history = sorted((entry["txid"], entry["category"], entry.get("address", ""), entry["amount"])
                         for entry in wallet.listtransactions("*", 1000))
        return {"balances": wallet.getbalances()["mine"], "coins": coins, "history": history,
                "txcount": wallet.getwalletinfo()["txcount"]}

    def check_recovery(self, name):
        wallet = self.nodes[0].get_wallet_rpc(name)
        assert_equal(wallet.getwalletinfo()["unlocked_until"], 0)
        for address in self.owned_addresses:
            assert_equal(wallet.getaddressinfo(address)["ismine"], True)
        assert_equal(self.snapshot(wallet), self.expected)
        for txid in self.expected_txids:
            assert wallet.gettransaction(txid)["confirmations"] > 0
        assert_equal({entry[0] for entry in self.expected["history"]}, self.expected_txids)
        assert_equal(wallet.gettransaction(self.reward_txid)["details"][0]["category"], "generate")
        assert any(coin[4] == self.change_address for coin in self.snapshot(wallet)["coins"])
        assert_equal(wallet.getbalance(), self.expected_balance)
        return wallet

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("miner", load_on_startup=True)
        node.createwallet("source", load_on_startup=False)
        miner = node.get_wallet_rpc("miner")
        source = node.get_wallet_rpc("source")
        mining_address = miner.getnewaddress()
        receive_a = source.getnewaddress("receive-a")
        receive_b = source.getnewaddress("receive-b")
        self.change_address = source.getrawchangeaddress()
        reward_address = source.getnewaddress("mining-reward")
        self.owned_addresses = [receive_a, receive_b, self.change_address, reward_address]
        assert_equal(len(set(self.owned_addresses)), 4)
        assert all(address.startswith("mcrt1z") for address in self.owned_addresses)
        assert_equal(source.getaddressinfo(self.change_address)["ischange"], True)
        source.encryptwallet(PASSPHRASE)
        backup = Path(self.options.tmpdir) / "pq-before-funding.dat"
        source.backupwallet(str(backup))
        assert_equal(source.getwalletinfo()["txcount"], 0)

        self.log.info("Create historical receipts and a mature native PQ reward")
        self.mine(101, mining_address)
        amount = (miner.getbalance() / 10).quantize(Decimal("0.01"), rounding=ROUND_DOWN)
        assert amount > Decimal("0.01")
        receipt_txid = miner.sendmany("", {receive_a: amount, receive_b: 2 * amount})
        receipt_block = self.mine(1, reward_address)[0]
        self.mine(100, mining_address)
        block = node.getblock(receipt_block, 2)
        self.reward_txid = block["tx"][0]["txid"]
        reward = sum(output["value"] for output in block["tx"][0]["vout"]
                     if output["scriptPubKey"].get("address") == reward_address)
        assert reward > 0

        self.log.info("Spend a recovered receipt into a pre-registered internal PQ change script")
        source.walletpassphrase(PASSPHRASE, 300)
        funding = next(coin for coin in source.listunspent() if coin["address"] == receive_a)
        payment = (amount / 4).quantize(Decimal("0.01"), rounding=ROUND_DOWN)
        funded = source.walletcreatefundedpsbt(
            [{"txid": funding["txid"], "vout": funding["vout"]}],
            [{mining_address: payment}],
            options={"add_inputs": False, "change_address": self.change_address},
        )
        assert funded["changepos"] >= 0
        signed = source.walletprocesspsbt(funded["psbt"])
        assert_equal(signed["complete"], True)
        final = node.finalizepsbt(signed["psbt"])
        assert_equal(final["complete"], True)
        spend_txid = node.sendrawtransaction(final["hex"])
        self.mine(1, mining_address)
        source.walletlock()
        self.expected_txids = {receipt_txid, spend_txid, self.reward_txid}
        self.expected_balance = 3 * amount + reward - payment - funded["fee"]
        self.expected = self.snapshot(source)
        assert_equal(source.getbalance(), self.expected_balance)
        assert_equal(self.expected["txcount"], 3)
        node.unloadwallet("source", load_on_startup=False)

        self.wait_for_filters()
        tip = node.getblockcount()
        with node.assert_debug_log(["fast variant using block filters"]):
            node.restorewallet("fast", str(backup), True)
        fast = self.check_recovery("fast")
        assert_raises_rpc_error(-13, "walletpassphrase", fast.rescanblockchain, 0, tip)
        with node.assert_debug_log(["fast variant using block filters"]):
            assert_equal(self.rescan(fast, 0, tip), {"start_height": 0, "stop_height": tip})
        self.check_recovery("fast")

        self.log.info("Compare locked-wallet recovery against the full-block scan")
        self.restart_node(0, self.node_args(indexed=False))
        self.check_recovery("fast")
        with node.assert_debug_log(["slow variant inspecting all blocks"]):
            node.restorewallet("full", str(backup), True)
        full = self.check_recovery("full")
        assert_equal(self.rescan(full, 0, tip), {"start_height": 0, "stop_height": tip})
        self.check_recovery("full")

        self.log.info("Unavailable filter data must fall back to block reads and recover actual funds")
        self.stop_node(0)
        filter_paths = sorted((node.chain_path / "indexes" / "blockfilter" / "basic").glob("fltr*.dat"))
        assert filter_paths
        filter_data = {path: path.read_bytes() for path in filter_paths}
        try:
            for path in filter_paths:
                path.write_bytes(b"")
            self.start_node(0, self.node_args(indexed=True))
            self.wait_for_filters()
            assert_raises_rpc_error(-32603, "Filter not found", node.getblockfilter, receipt_block)
            with node.assert_debug_log(["WARNING: block filter not found!"]):
                node.restorewallet("fallback", str(backup), True)
            self.check_recovery("fallback")
        finally:
            if node.running:
                self.stop_node(0)
            for path, data in filter_data.items():
                path.write_bytes(data)
        self.start_node(0, self.node_args(indexed=True))
        self.wait_for_filters()
        self.check_recovery("fallback")

        self.log.info("A matching filter must not hide unreadable required block data")
        raw_block = bytes.fromhex(node.getblock(receipt_block, 0))
        xor_key = node.read_xor_key()
        self.stop_node(0)
        damaged_path = None
        saved_data = None
        for path in sorted(node.blocks_path.glob("blk*.dat")):
            data = path.read_bytes()
            offset = util_xor(data, xor_key, offset=0).find(raw_block)
            if offset >= 0:
                damaged_path, saved_data = path, data
                damaged = bytearray(data)
                damaged[offset + 76] ^= 1  # Change only the old block's nonce, preserving record sizes.
                path.write_bytes(damaged)
                break
        assert damaged_path is not None and saved_data is not None
        try:
            self.start_node(0, self.node_args(indexed=True))
            self.wait_for_filters()
            fast = node.get_wallet_rpc("fast")
            receipt_height = node.getblockheader(receipt_block)["height"]
            with node.assert_debug_log(["GetHash() doesn't match index"]):
                assert_raises_rpc_error(-1, "Rescan failed", self.rescan, fast, receipt_height, receipt_height)
            self.check_recovery("fast")
            assert_equal(fast.getwalletinfo()["scanning"], False)
        finally:
            if node.running:
                self.stop_node(0)
            damaged_path.write_bytes(saved_data)
        self.start_node(0, self.node_args(indexed=True))
        self.wait_for_filters()
        self.check_recovery("fast")

        self.log.info("Pruned required history must fail both explicit rescan and backup restoration")
        self.mine(600 - node.getblockcount(), mining_address)
        self.wait_for_filters()
        pruned_height = node.pruneblockchain(node.getblockcount() - 288)
        assert pruned_height >= node.getblockheader(receipt_block)["height"]
        assert node.getblockfilter(receipt_block)["filter"]
        fast = node.get_wallet_rpc("fast")
        assert_raises_rpc_error(-1, "Can't rescan beyond pruned data", self.rescan, fast, 0)
        assert_raises_rpc_error(-4, "goes beyond pruned data", node.restorewallet, "pruned", str(backup))
        assert "pruned" not in node.listwallets()
        self.check_recovery("fast")
        self.restart_node(0, self.node_args(indexed=True))
        self.check_recovery("fast")


if __name__ == "__main__":
    MercaturaPQRescanTest(__file__).main()
