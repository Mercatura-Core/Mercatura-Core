#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Prove direct multi-wallet native PQ coinbase payouts without consensus changes."""

from copy import deepcopy
from decimal import Decimal

from test_framework.blocktools import (
    COINBASE_MATURITY,
    NORMAL_GBT_REQUEST_PARAMS,
    add_witness_commitment,
    create_block,
    create_coinbase,
)
from test_framework.messages import CTxOut, tx_from_hex
from test_framework.script import CScript
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import MCA_BASE_UNITS_PER_COIN, assert_equal


SPEND_FEE = 20  # 0.20 MCA, sufficient for one native ML-DSA-65 input.
PQ_SIGNATURE_SIZE = 3309
PQ_PUBLIC_KEY_SIZE = 1952


def to_mca(base_units):
    return Decimal(base_units) / Decimal(MCA_BASE_UNITS_PER_COIN)


def to_base_units(amount):
    units = amount * MCA_BASE_UNITS_PER_COIN
    assert_equal(units, units.to_integral_value())
    return int(units)


class MercaturaPoolCoinbaseTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True
        # Create independent native PQ wallets explicitly, without importing
        # the framework's inherited deterministic classical coinbase keys.
        self.wallet_names = []
        self.rpc_timeout = 120

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def make_template_block(self, template, scripts):
        reward = template["coinbasevalue"]
        assert isinstance(reward, int)
        share, remainder = divmod(reward, len(scripts))
        allocations = [share + (index < remainder) for index in range(len(scripts))]
        assert all(amount > SPEND_FEE for amount in allocations)
        assert_equal(sum(allocations), reward)

        # nValue=0 avoids inherited Bitcoin subsidy calculations. Leave the
        # framework's height-prefixed input, sequence and locktime intact.
        coinbase = create_coinbase(template["height"], nValue=0)
        coinbase.vout = [CTxOut(amount, script) for amount, script in zip(allocations, scripts)]
        assert all(len(output.serialize()) == 43 for output in coinbase.vout)
        assert_equal(template["coinbaseaux"], {})
        block = create_block(
            coinbase=coinbase,
            tmpl=template,
            txlist=[entry["data"] for entry in template["transactions"]],
        )

        # Preserve all selected transactions in template order, including PQ
        # witnesses. The coinbase witness leaf is zero, so changing its payout
        # outputs does not change the template's witness commitment.
        add_witness_commitment(block, nonce=0)
        assert_equal(coinbase.wit.vtxinwit[0].scriptWitness.stack, [bytes(32)])
        assert_equal(coinbase.vout[-1].nValue, 0)
        assert_equal(coinbase.vout[-1].scriptPubKey.hex(), template["default_witness_commitment"])
        assert_equal(sum(output.nValue for output in coinbase.vout), reward)
        for tx, entry in zip(block.vtx[1:], template["transactions"]):
            assert_equal(tx.serialize().hex(), entry["data"])
            assert_equal(tx.txid_hex, entry["txid"])
            assert_equal(tx.wtxid_hex, entry["hash"])
        assert_equal(block.hashMerkleRoot, block.calc_merkle_root())
        assert len(block.serialize()) <= template["sizelimit"]
        return block, allocations

    def check_proposal(self, node, template, block, expected):
        if "proposal" in template["capabilities"]:
            assert_equal(node.getblocktemplate({
                "mode": "proposal",
                "data": block.serialize().hex(),
            }), expected)

    def submit_valid_block(self, node, template, block):
        assert_equal(node.getbestblockhash(), template["previousblockhash"])
        self.check_proposal(node, template, block, None)
        self.solve_mercatura_block(node, block)
        assert_equal(node.submitblock(block.serialize().hex()), None)
        # A null submitblock result alone does not prove active-chain inclusion.
        assert_equal(node.getbestblockhash(), block.hash_hex)
        assert_equal(node.getblockcount(), template["height"])
        accepted = node.getblock(block.hash_hex, 2)
        assert_equal(accepted["confirmations"], 1)
        assert_equal(accepted["tx"][0]["txid"], block.vtx[0].txid_hex)
        assert_equal([tx["txid"] for tx in accepted["tx"][1:]],
                     [entry["txid"] for entry in template["transactions"]])
        assert_equal(sum(to_base_units(output["value"]) for output in accepted["tx"][0]["vout"]),
                     template["coinbasevalue"])
        node.syncwithvalidationinterfacequeue()

    def assert_allocations(self, wallets, addresses, block, allocations, *, confirmations, mature):
        category = "generate" if mature else "immature"
        for index, (wallet, address, amount) in enumerate(zip(wallets, addresses, allocations)):
            info = wallet.gettransaction(block.vtx[0].txid_hex)
            assert_equal(info["blockhash"], block.hash_hex)
            assert_equal(info["confirmations"], confirmations)
            assert_equal(info["generated"], True)
            # gettransaction's top-level credit excludes immature coinbases;
            # its details retain the exact allocation throughout maturation.
            assert_equal(to_base_units(info["amount"]), amount if mature else 0)
            assert_equal(len(info["details"]), 1)
            detail = info["details"][0]
            assert_equal(detail["address"], address)
            assert_equal(detail["vout"], index)
            assert_equal(detail["category"], category)
            assert_equal(to_base_units(detail["amount"]), amount)
            balances = wallet.getbalances()["mine"]
            assert_equal(to_base_units(balances["immature"]), 0 if mature else amount)
            assert_equal(to_base_units(balances["trusted"]), amount if mature else 0)
            assert_equal(to_base_units(balances["untrusted_pending"]), 0)

    def mine_blocks_batched(self, node, count):
        # Keep maturation rewards out of all recipient wallets, so their
        # balances and subsequent explicitly selected spends stay unambiguous.
        while count:
            batch = min(10, count)
            assert_equal(len(self.generatetodescriptor(node, batch, "raw(51)")), batch)
            count -= batch

    def run_test(self):
        node = self.nodes[0]
        wallets = []
        addresses = []
        scripts = []
        self.log.info("Creating three independently controlled native PQ recipients")
        for index in range(3):
            name = f"recipient{index}"
            node.createwallet(wallet_name=name)
            wallet = node.get_wallet_rpc(name)
            address = wallet.getnewaddress("pool-coinbase")
            assert address.startswith("mcrt1z")
            info = wallet.getaddressinfo(address)
            assert_equal(info["ismine"], True)
            assert_equal(info["iswitness"], True)
            assert_equal(info["witness_version"], 2)
            assert_equal(len(info["witness_program"]), 64)
            assert_equal(info["scriptPubKey"], "5220" + info["witness_program"])
            wallets.append(wallet)
            addresses.append(address)
            scripts.append(CScript(bytes.fromhex(info["scriptPubKey"])))
        assert_equal(len(set(addresses)), 3)
        for owner, address in enumerate(addresses):
            for index, wallet in enumerate(wallets):
                assert_equal(wallet.getaddressinfo(address)["ismine"], index == owner)

        template = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        assert_equal(template["height"], 1)
        assert_equal(template["transactions"], [])
        block, allocations = self.make_template_block(template, scripts)

        self.log.info("Rejecting a multi-output coinbase overclaim of exactly 0.01 MCA")
        overclaim = deepcopy(block)
        overclaim.vtx[0].vout[0].nValue += 1
        overclaim.hashMerkleRoot = overclaim.calc_merkle_root()
        assert_equal(sum(output.nValue for output in overclaim.vtx[0].vout),
                     template["coinbasevalue"] + 1)
        parent_tip = node.getbestblockhash()
        parent_height = node.getblockcount()
        self.check_proposal(node, template, overclaim, "bad-cb-amount")
        self.solve_mercatura_block(node, overclaim)
        assert_equal(node.submitblock(overclaim.serialize().hex()), "bad-cb-amount")
        assert_equal(node.getbestblockhash(), parent_tip)
        assert_equal(node.getblockcount(), parent_height)

        self.log.info(f"Submitting exact split coinbase allocations: {allocations} base units")
        self.submit_valid_block(node, template, block)
        self.assert_allocations(wallets, addresses, block, allocations, confirmations=1, mature=False)

        self.log.info("Checking maturity at depths 100 and 101")
        self.mine_blocks_batched(node, COINBASE_MATURITY - 1)
        assert_equal(node.getblockcount(), template["height"] + COINBASE_MATURITY - 1)
        self.assert_allocations(wallets, addresses, block, allocations,
                                confirmations=COINBASE_MATURITY, mature=False)
        self.mine_blocks_batched(node, 1)
        assert_equal(node.getblockcount(), template["height"] + COINBASE_MATURITY)
        self.assert_allocations(wallets, addresses, block, allocations,
                                confirmations=COINBASE_MATURITY + 1, mature=True)

        # An auxiliary PQ receiver keeps all three recipients' balances isolated.
        node.createwallet(wallet_name="receiver")
        receiver = node.get_wallet_rpc("receiver")
        receive_addresses = [receiver.getnewaddress(f"from-recipient{index}") for index in range(3)]
        assert all(address.startswith("mcrt1z") for address in receive_addresses)
        no_fee_template = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        assert_equal(no_fee_template["transactions"], [])
        spend_txids = []
        self.log.info("Independently signing and broadcasting each matured PQ coinbase output")
        for index, (wallet, amount, destination) in enumerate(zip(wallets, allocations, receive_addresses)):
            utxos = wallet.listunspent()
            assert_equal(len(utxos), 1)
            utxo = utxos[0]
            assert_equal(utxo["txid"], block.vtx[0].txid_hex)
            assert_equal(utxo["vout"], index)
            assert_equal(to_base_units(utxo["amount"]), amount)
            assert_equal(utxo["spendable"], True)
            raw = wallet.createrawtransaction(
                [{"txid": utxo["txid"], "vout": index}],
                [{destination: to_mca(amount - SPEND_FEE)}],
            )
            signed = wallet.signrawtransactionwithwallet(raw)
            assert_equal(signed["complete"], True)
            spend = tx_from_hex(signed["hex"])
            assert_equal(len(spend.vin), 1)
            assert_equal(spend.vin[0].prevout.hash, block.vtx[0].txid_int)
            assert_equal(spend.vin[0].prevout.n, index)
            assert_equal(spend.vin[0].scriptSig, b"")
            assert_equal(len(spend.wit.vtxinwit), 1)
            stack = spend.wit.vtxinwit[0].scriptWitness.stack
            assert_equal(len(stack), 2)
            assert_equal(len(stack[0]), PQ_SIGNATURE_SIZE)
            assert_equal(len(stack[1]), PQ_PUBLIC_KEY_SIZE)
            assert_equal(amount - sum(output.nValue for output in spend.vout), SPEND_FEE)
            acceptance = node.testmempoolaccept([signed["hex"]])[0]
            assert_equal(acceptance["allowed"], True)
            assert_equal(to_base_units(acceptance["fees"]["base"]), SPEND_FEE)
            txid = node.sendrawtransaction(signed["hex"])
            assert_equal(txid, spend.txid_hex)
            assert txid in node.getrawmempool()
            spend_txids.append(txid)

        # GBT caches a template for up to five seconds after mempool changes.
        # Wait for the actual selected transactions rather than using stale fees.
        self.wait_until(lambda: {entry["txid"] for entry in node.getblocktemplate(
            NORMAL_GBT_REQUEST_PARAMS)["transactions"]} == set(spend_txids), timeout=120)
        fee_template = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        assert_equal(fee_template["previousblockhash"], no_fee_template["previousblockhash"])
        assert_equal(fee_template["height"], no_fee_template["height"])
        assert_equal({entry["txid"] for entry in fee_template["transactions"]}, set(spend_txids))
        fees = sum(entry["fee"] for entry in fee_template["transactions"])
        assert_equal(fees, 3 * SPEND_FEE)
        assert_equal(fee_template["coinbasevalue"], no_fee_template["coinbasevalue"] + fees)

        self.log.info("Splitting the full subsidy plus three known PQ transaction fees")
        fee_block, fee_allocations = self.make_template_block(fee_template, scripts)
        self.submit_valid_block(node, fee_template, fee_block)
        self.assert_allocations(wallets, addresses, fee_block, fee_allocations, confirmations=1, mature=False)
        for wallet, txid, destination, amount in zip(wallets, spend_txids, receive_addresses, allocations):
            assert txid not in node.getrawmempool()
            assert_equal(wallet.gettransaction(txid)["confirmations"], 1)
            assert_equal(receiver.gettransaction(txid)["confirmations"], 1)
            assert_equal(to_base_units(receiver.getreceivedbyaddress(destination)), amount - SPEND_FEE)
        self.log.info("Direct multi-recipient PQ coinbase and fee payouts passed")


if __name__ == "__main__":
    MercaturaPoolCoinbaseTest(__file__).main()
