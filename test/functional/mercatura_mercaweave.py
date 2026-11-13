#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license.

"""Normal-node grouped authorization, activation boundaries and durable wallets.

Uses real MercaHash, real ML-DSA-65, actual wallet databases and PQ node links.
The immediate schedule node is deliberately isolated from the delayed network.
Raw preactivation blocks demonstrate historical unknown-witness permissiveness;
they are never presented as safe wallet funding or protected ownership.
"""

from copy import deepcopy
from decimal import Decimal
from pathlib import Path

from test_framework.address import address_to_scriptpubkey
from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.messages import COIN, COutPoint, CTransaction, CTxIn, CTxInWitness, CTxOut, tx_from_hex
from test_framework.psbt import PSBT, PSBT_IN_WITNESS_UTXO
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class MercaWeaveTest(BitcoinTestFramework):
    def set_test_params(self):
        # Full level-4 replay hashes the real MercaHash history; allow bounded
        # replay time on small and instrumented workers without reducing checks.
        self.rpc_timeout = 240
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.uses_wallet = True
        self.wallet_names = [False] * 3
        self.extra_args = [["-mercaweaveheight=110", "-pqp2p=required"] for _ in range(2)]
        self.extra_args.append(["-mercaweaveheight=0", "-pqp2p=required"])

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)

    def sync_all(self, nodes=None):
        super().sync_all(nodes=self.nodes[:2] if nodes is None else nodes)

    def mine(self, count, address):
        hashes = []
        while count:
            batch = min(10, count)
            hashes.extend(self.generatetoaddress(self.nodes[0], batch, address))
            count -= batch
        return hashes

    def block(self, node, outputs, txs=(), fee=0):
        template = node.getblocktemplate({"rules": ["segwit"]})
        coinbase = create_coinbase(template["height"])
        value = template["coinbasevalue"] + fee
        coinbase.vout = [CTxOut(value // len(outputs), script) for script in outputs]
        coinbase.vout[0].nValue += value % len(outputs)
        block = create_block(coinbase=coinbase, tmpl=template, txlist=list(txs))
        if txs:
            add_witness_commitment(block)
        self.solve_mercatura_block(node, block)
        return block

    @staticmethod
    def corrupt_signature(tx):
        changed = deepcopy(tx)
        for witness in changed.wit.vtxinwit:
            if len(witness.scriptWitness.stack) == 3:
                signature = bytearray(witness.scriptWitness.stack[0])
                signature[0] ^= 1
                witness.scriptWitness.stack[0] = bytes(signature)
                return changed
        raise AssertionError("Expected a grouped anchor")

    def finish(self, wallet, psbt):
        processed = wallet.walletprocesspsbt(psbt)
        assert processed["complete"]
        finalized = self.nodes[0].finalizepsbt(processed["psbt"])
        assert finalized["complete"]
        return finalized["hex"], processed["psbt"]

    def run_test(self):
        node, relay, immediate = self.nodes
        node.createwallet("sender")
        relay.createwallet("receiver")
        immediate.createwallet("immediate")
        sender, receiver = node.get_wallet_rpc("sender"), relay.get_wallet_rpc("receiver")
        owner = immediate.get_wallet_rpc("immediate")
        standard, recipient = sender.getnewaddress(), receiver.getnewaddress()
        assert standard.startswith("mcrt1z")
        grouped = owner.getnewaddress("immediate", "mercaweave")
        info = owner.getaddressinfo(grouped)
        assert_equal(info["witness_version"], 3)
        assert info["pq_group_authenticated"] and info["ismine"]
        assert_equal(len(bytes.fromhex(info["pq_group_public_key"])), 1952)
        for wallet in (sender, receiver):
            assert_raises_rpc_error(-4, "not active", wallet.getnewaddress, "", "mercaweave")
            assert_raises_rpc_error(-4, "not active", wallet.getnewmercaweaveaddress)
        assert_raises_rpc_error(-1, "not protected", self.generatetoaddress, node, 1, grouped)

        self.log.info("Raw historical control: v3 before activation is not protected ownership")
        group_script = address_to_scriptpubkey(grouped)
        first = self.block(node, [group_script, group_script])
        first_raw = first.serialize().hex()
        assert_equal(node.submitblock(first_raw), None)
        assert_equal(immediate.submitblock(first_raw), None)
        self.sync_all()
        prefix = self.mine(101, standard)
        for block_hash in prefix:
            assert_equal(immediate.submitblock(node.getblock(block_hash, 0)), None)
        assert_equal(node.getblockcount(), 102)
        assert_raises_rpc_error(-6, "MercaWeave", sender.sendtoaddress, grouped, Decimal("1"))
        coinbase = first.vtx[0]
        bad = CTransaction()
        bad.vin = [CTxIn(COutPoint(coinbase.txid_int, 0))]
        bad.vout = [CTxOut(coinbase.vout[0].nValue - 2, address_to_scriptpubkey(standard))]
        bad.wit.vtxinwit = [CTxInWitness()]
        bad.wit.vtxinwit[0].scriptWitness.stack = [bytes(3309), bytes.fromhex(info["pq_group_public_key"]), bytes.fromhex(info["pq_address_nonce"])]
        unsafe_block = self.block(node, [address_to_scriptpubkey(standard)], [bad], 2)
        assert_equal(node.submitblock(unsafe_block.serialize().hex()), None)
        assert immediate.submitblock(unsafe_block.serialize().hex()) is not None
        assert_equal(immediate.getblockcount(), 102)
        self.sync_all()

        self.log.info("A genuine owner signs on the immediate chain; delayed admission remains gated")
        point = {"txid": coinbase.txid_hex, "vout": 1}
        amount = Decimal(coinbase.vout[1].nValue - 10) / COIN
        raw = immediate.createrawtransaction([point], {standard: amount})
        processed = owner.walletprocesspsbt(immediate.converttopsbt(raw))
        assert processed["complete"]
        final = immediate.finalizepsbt(processed["psbt"])
        assert final["complete"]
        valid = tx_from_hex(final["hex"])
        admission = node.testmempoolaccept([final["hex"]])[0]
        assert not admission["allowed"]
        assert_equal(admission["reject-reason"], "mercaweave-inactive")
        self.mine(6, standard)
        assert_equal(node.getblockcount(), 109)
        assert node.testmempoolaccept([final["hex"]])[0]["allowed"]
        invalid = self.corrupt_signature(valid)
        boundary = self.block(node, [address_to_scriptpubkey(standard)], [invalid], 10)
        assert node.submitblock(boundary.serialize().hex()) is not None
        assert_equal(node.getblockcount(), 109)
        assert not node.testmempoolaccept([invalid.serialize().hex()])[0]["allowed"]
        assert_raises_rpc_error(-4, "not active", sender.getnewaddress, "", "mercaweave")
        node.sendrawtransaction(final["hex"])
        self.sync_all()
        self.mine(1, standard)
        assert_equal(node.getblockcount(), 110)
        group0 = sender.getnewaddress("group zero", "mercaweave")
        group1 = sender.getnewmercaweaveaddress("group one")
        assert_equal(sender.getaddressinfo(group0)["pq_address_index"], 0)
        assert_equal(sender.getaddressinfo(group1)["pq_address_index"], 1)
        change = sender.getrawchangeaddress("mercaweave")
        mining = sender.getnewmercaweaveaddress("mining", 0, "mining")
        assert_equal(sender.getaddressinfo(change)["pq_group_purpose"], 1)
        assert_equal(sender.getaddressinfo(mining)["pq_group_purpose"], 2)
        assert_raises_rpc_error(-8, "purpose", sender.getnewmercaweaveaddress, "", 0, "invalid")

        self.log.info("Reorg below the activation boundary removes unsafe v3 funding from the mempool")
        pending = sender.sendtoaddress(group0, Decimal("5"))
        self.sync_all()
        ancestor = node.getblockhash(109)
        for peer in (node, relay):
            peer.invalidateblock(ancestor)
            assert_equal(peer.getblockcount(), 108)
            assert pending not in peer.getrawmempool()
        assert not sender.getaddressinfo(group0)["mercaweave_active"]
        assert_raises_rpc_error(-4, "not active", sender.getnewaddress, "", "mercaweave")
        for peer in (node, relay):
            peer.reconsiderblock(ancestor)
        self.sync_all()
        if pending not in node.getrawmempool():
            node.sendrawtransaction(sender.gettransaction(pending)["hex"])
        self.mine(1, standard)
        funding = sender.sendtoaddress(group1, Decimal("6"))
        self.mine(1, standard)
        assert_equal(sender.gettransaction(funding)["confirmations"], 1)

        self.log.info("Ordinary funding, mixed v2/v3 signing, full-byte fees, combine and finalization")
        coins = sender.listunspent()
        grouped_coins = [coin for coin in coins if coin.get("address") in (group0, group1)]
        native = next(coin for coin in coins if coin.get("address") == standard)
        assert_equal(len(grouped_coins), 2)
        selected = grouped_coins + [native]
        inputs = [{"txid": coin["txid"], "vout": coin["vout"]} for coin in selected]
        funded = sender.walletcreatefundedpsbt(inputs, [{recipient: Decimal("1")}, {standard: Decimal("1")}], 0,
                                             {"add_inputs": False, "fee_rate": Decimal("0.001")})
        signed, psbt = self.finish(sender, funded["psbt"])
        assert_equal(node.combinepsbt([psbt, psbt]), psbt)
        assert_equal(sender.walletprocesspsbt(psbt)["psbt"], psbt)
        transaction = tx_from_hex(signed)
        stacks = [w.scriptWitness.stack for w in transaction.wit.vtxinwit]
        assert_equal(sorted(len(stack) for stack in stacks), [2, 2, 3])
        follower = next(stack for stack in stacks if len(stack) == 2 and len(stack[1]) == 4)
        assert_equal(int.from_bytes(follower[1], "little"), 0)
        fee = sum(coin["amount"] for coin in selected) - sum(Decimal(output.nValue) / COIN for output in transaction.vout)
        assert_equal(fee, funded["fee"])
        assert_equal(fee, Decimal((len(bytes.fromhex(signed)) + 999) // 1000) / COIN)
        mutations = [self.corrupt_signature(transaction)]
        for item in (1, 2):
            changed = deepcopy(transaction)
            anchor = next(w.scriptWitness.stack for w in changed.wit.vtxinwit if len(w.scriptWitness.stack) == 3)
            data = bytearray(anchor[item])
            data[0] ^= 1
            anchor[item] = bytes(data)
            mutations.append(changed)
        changed = deepcopy(transaction)
        follower = next(w.scriptWitness.stack for w in changed.wit.vtxinwit if len(w.scriptWitness.stack) == 2 and len(w.scriptWitness.stack[1]) == 4)
        follower[1] = (2).to_bytes(4, "little")
        mutations.append(changed)
        changed = deepcopy(transaction)
        changed.vout[0].nValue -= 1
        mutations.append(changed)
        for changed in mutations:
            assert not node.testmempoolaccept([changed.serialize().hex()])[0]["allowed"]
            assert not relay.testmempoolaccept([changed.serialize().hex()])[0]["allowed"]
        txid = node.sendrawtransaction(signed)
        self.sync_all()
        assert txid in relay.getrawmempool()
        self.mine(1, standard)
        assert_equal(receiver.gettransaction(txid)["confirmations"], 1)
        for peer in (node, relay):
            assert all(p["pq_encrypted"] and p["pq_authenticated"] and p["transport_protocol_type"] == "pq-v1" for p in peer.getpeerinfo())

        self.log.info("Two independent wallets sign only their groups and combine genuine partial PSBTs")
        foreign = receiver.getnewmercaweaveaddress("independent signer")
        sender.sendtoaddress(group0, Decimal("3"))
        sender.sendtoaddress(foreign, Decimal("4"))
        self.mine(1, standard)
        own_coin = next(coin for coin in sender.listunspent() if coin.get("address") == group0)
        foreign_coin = next(coin for coin in receiver.listunspent() if coin.get("address") == foreign)
        points = [{"txid": coin["txid"], "vout": coin["vout"]} for coin in (own_coin, foreign_coin)]
        partial = PSBT.from_base64(node.createpsbt(points, {standard: Decimal("6.80")}))
        for index, (wallet, address, coin) in enumerate(((sender, group0, own_coin), (receiver, foreign, foreign_coin))):
            public = wallet.getaddressinfo(address)
            partial.i[index].map[PSBT_IN_WITNESS_UTXO] = CTxOut(int(coin["amount"] * COIN), address_to_scriptpubkey(address)).serialize()
            identifier = b"MercaWeave"
            prefix = b"\xfc" + bytes([len(identifier)]) + identifier
            partial.i[index].map[prefix + b"\x01"] = bytes.fromhex(public["pq_address_nonce"])
            partial.i[index].map[prefix + b"\x03"] = bytes.fromhex(public["pq_group_public_key"])
        blank = partial.to_base64()
        left = sender.walletprocesspsbt(blank, True, "DEFAULT", False, False)
        right = receiver.walletprocesspsbt(blank, True, "DEFAULT", False, False)
        assert not left["complete"] and not right["complete"]
        merged = node.combinepsbt([left["psbt"], right["psbt"]])
        extracted = node.finalizepsbt(merged)
        assert extracted["complete"]
        assert node.testmempoolaccept([extracted["hex"]])[0]["allowed"]
        hostile = PSBT.from_base64(right["psbt"])
        hostile.i[1].map[prefix + b"\x02"] = (0).to_bytes(4, "little")
        assert_raises_rpc_error(-8, "not compatible", node.combinepsbt, [left["psbt"], hostile.to_base64()])
        oversized = PSBT.from_base64(left["psbt"])
        for index in range(257):
            oversized.i[0].map[b"\x80" + index.to_bytes(2, "little")] = b"opaque"
        assert_raises_rpc_error(-8, "MercaWeave", node.finalizepsbt, oversized.to_base64())
        node.sendrawtransaction(extracted["hex"])
        self.mine(1, standard)

        self.log.info("Account/purpose separation and automatic group-key rotation at 64 addresses")
        first_key = sender.getaddressinfo(group0)["pq_group_public_key"]
        for _ in range(62):
            last = sender.getnewmercaweaveaddress()
        assert_equal(sender.getaddressinfo(last)["pq_address_index"], 63)
        rotated = sender.getnewmercaweaveaddress()
        rotation = sender.getaddressinfo(rotated)
        assert_equal(rotation["pq_group_index"], 1)
        assert_equal(rotation["pq_address_index"], 0)
        assert rotation["pq_group_public_key"] != first_key
        account = sender.getnewmercaweaveaddress("account", 1)
        assert_equal(sender.getaddressinfo(account)["pq_group_account"], 1)
        assert sender.getaddressinfo(account)["pq_group_public_key"] != first_key

        self.log.info("Activated native mining payout and encrypted cold restart/backup/restore")
        self.mine(1, mining)
        reward_tx = node.getblock(node.getbestblockhash(), 2)["tx"][0]
        reward = reward_tx["vout"][0]["scriptPubKey"]
        assert_equal(reward["type"], "witness_v3_mercaweave")
        assert_equal(reward["address"], mining)
        reward_point = {"txid": reward_tx["txid"], "vout": 0}
        assert not any(coin["txid"] == reward_tx["txid"] for coin in sender.listunspent())
        self.mine(100, standard)
        assert any(coin["txid"] == reward_tx["txid"] for coin in sender.listunspent())
        funded_reward = sender.walletcreatefundedpsbt([reward_point], [{recipient: Decimal("1")}], 0,
                                                    {"add_inputs": False, "fee_rate": Decimal("0.001")})
        signed_reward, _ = self.finish(sender, funded_reward["psbt"])
        assert node.testmempoolaccept([signed_reward])[0]["allowed"]
        node.sendrawtransaction(signed_reward)
        self.mine(1, standard)
        sender.sendtoaddress(group0, Decimal("3"))
        self.mine(1, standard)
        sender.encryptwallet("public functional-test password")
        self.restart_node(0)
        self.connect_nodes(0, 1)
        if "sender" not in node.listwallets():
            node.loadwallet("sender")
        sender = node.get_wallet_rpc("sender")
        assert_raises_rpc_error(-13, "walletpassphrase", sender.getnewmercaweaveaddress)
        sender.walletpassphrase("public functional-test password", 600)
        sender.rescanblockchain()
        assert sender.getaddressinfo(group0)["pq_group_authenticated"]
        assert any(coin.get("address") == group0 for coin in sender.listunspent())
        balance = sender.getbalance()
        next_address = sender.getnewmercaweaveaddress()
        assert_equal(sender.getaddressinfo(next_address)["pq_address_index"], 1)
        backup = Path(self.options.tmpdir) / "grouped-wallet-backup.sqlite"
        sender.backupwallet(str(backup))
        node.unloadwallet("sender")
        node.restorewallet("restored", str(backup))
        restored = node.get_wallet_rpc("restored")
        restored.walletpassphrase("public functional-test password", 600)
        restored.rescanblockchain()
        assert_equal(restored.getbalance(), balance)
        assert restored.getaddressinfo(group0)["pq_group_authenticated"]
        issued = restored.getnewmercaweaveaddress()
        assert_equal(restored.getaddressinfo(issued)["pq_address_index"], 2)
        assert_equal(restored.getaddressinfo(issued)["pq_group_index"], 1)
        assert_equal(node.verifychain(4, 0), True)
        self.log.info("Native MercaWeave activation, wallet, PSBT, mining and recovery passed")


if __name__ == "__main__":
    MercaWeaveTest(__file__).main()
