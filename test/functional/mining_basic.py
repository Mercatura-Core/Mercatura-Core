#!/usr/bin/env python3
# Copyright (c) 2014-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mining RPCs

- getmininginfo
- getblocktemplate
- submitblock

mining_template_verification.py tests getblocktemplate in proposal mode"""

import copy
from decimal import Decimal

from test_framework.blocktools import (
    create_coinbase,
    get_witness_script,
    NORMAL_GBT_REQUEST_PARAMS,
    TIME_GENESIS_BLOCK,
    REGTEST_N_BITS,
    REGTEST_TARGET,
    nbits_str,
    target_str,
)
from test_framework.messages import (
    BLOCK_HEADER_SIZE,
    CBlock,
    CBlockHeader,
    COIN,
    DEFAULT_BLOCK_RESERVED_SIZE,
    MERCATURA_INITIAL_BLOCK_CAPACITY,
    MERCATURA_MAX_BLOCK_CAPACITY,
    MAX_SEQUENCE_NONFINAL,
    MINIMUM_BLOCK_RESERVED_SIZE,
    ser_uint256,
    WITNESS_SCALE_FACTOR,
)
from test_framework.p2p import P2PDataStore
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
    get_fee,
)
from test_framework.wallet import (
    MiniWallet,
    MiniWalletMode,
)


BIP94_TIMEWARP_INTERVAL = 144
MAX_FUTURE_BLOCK_TIME = 2 * 3600
MAX_TIMEWARP = 600
VERSIONBITS_TOP_BITS = 0x20000000
VERSIONBITS_DEPLOYMENT_TESTDUMMY_BIT = 28
DEFAULT_BLOCK_MIN_TX_FEE = 1 # default `-blockmintxfee` setting [sat/kvB]

class MiningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.extra_args = [
            [],
            [],
            ["-fastprune", "-prune=1"]
        ]
        self.setup_clean_chain = True

    def mine_chain(self):
        self.log.info('Create some old blocks')
        for t in range(TIME_GENESIS_BLOCK, TIME_GENESIS_BLOCK + 200 * 600, 600):
            self.nodes[0].setmocktime(t)
            self.generate(self.wallet, 1, sync_fun=self.no_op)
        mining_info = self.nodes[0].getmininginfo()
        assert_equal(mining_info['blocks'], 200)
        assert_equal(mining_info['currentblocktx'], 0)
        # currentblockweight remains a genuine BIP141 weight statistic.
        # Mercatura's mining capacity is accounted separately in serialized bytes.
        assert_greater_than(mining_info['currentblockweight'], 0)

        self.log.info('test blockversion')
        self.restart_node(0, extra_args=[f'-mocktime={t}', '-blockversion=1337'])
        self.connect_nodes(0, 1)
        assert_equal(1337, self.nodes[0].getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)['version'])
        self.restart_node(0, extra_args=[f'-mocktime={t}'])
        self.connect_nodes(0, 1)
        assert_equal(VERSIONBITS_TOP_BITS + (1 << VERSIONBITS_DEPLOYMENT_TESTDUMMY_BIT), self.nodes[0].getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)['version'])
        self.restart_node(0)
        self.connect_nodes(0, 1)

    def test_fees_and_sigops(self):
        self.log.info("Test fees and sigops in getblocktemplate result")
        node = self.nodes[0]

        # Generate a coinbases with p2pk transactions for its sigops.
        wallet_sigops = MiniWallet(node, mode=MiniWalletMode.RAW_P2PK)
        self.generate(wallet_sigops, 1, sync_fun=self.no_op)

        # Mature with regular coinbases to prevent interference with other tests
        self.generate(self.wallet, 100, sync_fun=self.no_op)

        # Generate three transactions that must be mined in sequence.
        # Mercatura fee rates use MCA per 1,000 effective serialized bytes.
        #
        #      tx_a (0.10 MCA/kB)
        #        |
        #        |
        #      tx_b (0.20 MCA/kB)
        #        |
        #        |
        #      tx_c (0.30 MCA/kB)
        #
        # These rates are deliberately separated enough that deterministic
        # integer base-unit rounding preserves their fee ordering.
        tx_a = wallet_sigops.send_self_transfer(from_node=node,
                                                fee_rate=Decimal("0.10"))
        tx_b = wallet_sigops.send_self_transfer(from_node=node,
                                                fee_rate=Decimal("0.20"),
                                                utxo_to_spend=tx_a["new_utxo"])
        tx_c = wallet_sigops.send_self_transfer(from_node=node,
                                                fee_rate=Decimal("0.30"),
                                                utxo_to_spend=tx_b["new_utxo"])

        # Generate a transaction without sigops. It will go first because it
        # pays a substantially higher fee and descends from a different
        # coinbase.
        tx_d = self.wallet.send_self_transfer(from_node=node,
                                              fee_rate=Decimal("1.00"))

        block_template_txs = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)['transactions']

        block_template_fees = [tx['fee'] for tx in block_template_txs]
        assert_equal(block_template_fees, [
            tx_d["fee"] * COIN,
            tx_a["fee"] * COIN,
            tx_b["fee"] * COIN,
            tx_c["fee"] * COIN
        ])

        block_template_sigops = [tx['sigops'] for tx in block_template_txs]
        assert_equal(block_template_sigops, [0, 4, 4, 4])

        # Clear mempool
        self.generate(self.wallet, 1, sync_fun=self.no_op)

    def test_blockmintxfee_parameter(self):
        self.log.info("Test -blockmintxfee setting")
        self.restart_node(0, extra_args=['-minrelaytxfee=0', '-persistmempool=0'])
        node = self.nodes[0]

        # Test the default, zero, and a range of Mercatura fee rates.
        # Values here are integer MCA base units per 1,000 bytes.
        for blockmintxfee_base_units_kvb in (
            DEFAULT_BLOCK_MIN_TX_FEE,
            0,
            2,
            5,
            10,
            25,
            50,
            100,
            250,
            500,
            1000,
            2500,
            5000,
            10000,
            25000,
        ):
            blockmintxfee_mca_kvb = (
                Decimal(blockmintxfee_base_units_kvb) / Decimal(COIN)
            )
            if blockmintxfee_base_units_kvb == DEFAULT_BLOCK_MIN_TX_FEE:
                self.log.info(f"-> Default -blockmintxfee setting ({blockmintxfee_base_units_kvb} base units/kB)...")
            else:
                blockmintxfee_parameter = f"-blockmintxfee={blockmintxfee_mca_kvb:.2f}"
                self.log.info(f"-> Test {blockmintxfee_parameter} ({blockmintxfee_base_units_kvb} base units/kB)...")
                self.restart_node(0, extra_args=[blockmintxfee_parameter, '-minrelaytxfee=0', '-persistmempool=0'])
            assert_equal(node.getmininginfo()['blockmintxfee'], blockmintxfee_mca_kvb)

            # submit one tx with exactly the blockmintxfee rate, and one slightly below
            tx_with_min_feerate = self.wallet.send_self_transfer(from_node=node, fee_rate=blockmintxfee_mca_kvb, confirmed_only=True)
            assert_equal(tx_with_min_feerate["fee"], get_fee(tx_with_min_feerate["tx"].get_fee_size(), blockmintxfee_mca_kvb))
            if blockmintxfee_base_units_kvb >= 10:
                # Ten base units/kB lower is enough to make the
                # resulting transaction fee measurably lower at this size.
                lowerfee_mca_kvb = (
                    blockmintxfee_mca_kvb
                    - Decimal(10) / Decimal(COIN)
                )
                assert_greater_than(blockmintxfee_mca_kvb, lowerfee_mca_kvb)
                assert_greater_than_or_equal(lowerfee_mca_kvb, 0)
                tx_below_min_feerate = self.wallet.send_self_transfer(from_node=node, fee_rate=lowerfee_mca_kvb, confirmed_only=True)
                assert_equal(tx_below_min_feerate["fee"], get_fee(tx_below_min_feerate["tx"].get_fee_size(), lowerfee_mca_kvb))
            else:  # go below zero fee by using modified fees
                tx_below_min_feerate = self.wallet.send_self_transfer(from_node=node, fee_rate=blockmintxfee_mca_kvb, confirmed_only=True)
                node.prioritisetransaction(tx_below_min_feerate["txid"], 0, -11)

            # check that tx below specified fee-rate is neither in template nor in the actual block
            block_template = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
            block_template_txids = [tx['txid'] for tx in block_template['transactions']]

            # Unless blockmintxfee is 0, the template shouldn't contain free transactions.
            # Note that the real block assembler uses package feerates, but we didn't create dependent transactions so it's ok to use base feerate.
            if blockmintxfee_mca_kvb > 0:
                for txid in block_template_txids:
                    tx = node.getmempoolentry(txid)
                    assert_greater_than(tx['fees']['base'], 0)

            self.generate(self.wallet, 1, sync_fun=self.no_op)
            block = node.getblock(node.getbestblockhash(), verbosity=2)
            block_txids = [tx['txid'] for tx in block['tx']]

            assert tx_with_min_feerate['txid'] in block_template_txids
            assert tx_with_min_feerate['txid'] in block_txids
            assert tx_below_min_feerate['txid'] not in block_template_txids
            assert tx_below_min_feerate['txid'] not in block_txids

            # Restart node to clear mempool for the next test
            self.restart_node(0)

    def test_timewarp(self):
        self.log.info("Test timewarp attack mitigation (BIP94)")
        node = self.nodes[0]
        self.restart_node(0, extra_args=['-test=bip94'])

        self.log.info("Mine until the last block before the BIP94 timewarp boundary")
        blockchain_info = self.nodes[0].getblockchaininfo()
        n = BIP94_TIMEWARP_INTERVAL - blockchain_info['blocks'] % BIP94_TIMEWARP_INTERVAL - 2
        t = blockchain_info['time']

        for _ in range(n):
            t += 600
            self.nodes[0].setmocktime(t)
            self.generate(self.wallet, 1, sync_fun=self.no_op)

        self.log.info("Create block two hours in the future")
        self.nodes[0].setmocktime(t + MAX_FUTURE_BLOCK_TIME)
        self.generate(self.wallet, 1, sync_fun=self.no_op)
        assert_equal(node.getblock(node.getbestblockhash())['time'], t + MAX_FUTURE_BLOCK_TIME)

        self.log.info("Block template at BIP94 timewarp boundary can't use wall clock time")
        self.nodes[0].setmocktime(t)
        # The template will have an adjusted timestamp, which we then modify
        tmpl = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        assert_greater_than_or_equal(tmpl['curtime'], t + MAX_FUTURE_BLOCK_TIME - MAX_TIMEWARP)
        # mintime and curtime should match
        assert_equal(tmpl['mintime'], tmpl['curtime'])

        block = CBlock()
        block.nVersion = tmpl["version"]
        block.hashPrevBlock = int(tmpl["previousblockhash"], 16)
        block.nTime = tmpl["curtime"]
        block.nBits = int(tmpl["bits"], 16)
        block.nNonce = 0
        block.vtx = [create_coinbase(height=int(tmpl["height"]))]
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        assert_equal(node.getblocktemplate(template_request={
            'data': block.serialize().hex(),
            'mode': 'proposal',
            'rules': ['segwit'],
        }), None)

        bad_block = copy.deepcopy(block)
        bad_block.nTime = t
        bad_block.solve()
        assert_raises_rpc_error(-25, 'time-timewarp-attack', lambda: node.submitheader(hexdata=CBlockHeader(bad_block).serialize().hex()))

        self.log.info("Test timewarp protection boundary")
        bad_block.nTime = t + MAX_FUTURE_BLOCK_TIME - MAX_TIMEWARP - 1
        bad_block.solve()
        assert_raises_rpc_error(-25, 'time-timewarp-attack', lambda: node.submitheader(hexdata=CBlockHeader(bad_block).serialize().hex()))

        bad_block.nTime = t + MAX_FUTURE_BLOCK_TIME - MAX_TIMEWARP
        bad_block.solve()
        node.submitheader(hexdata=CBlockHeader(bad_block).serialize().hex())

    def test_pruning(self):
        self.log.info("Test that submitblock stores previously pruned block")
        prune_node = self.nodes[2]
        self.generate(prune_node, 400, sync_fun=self.no_op)
        pruned_block = prune_node.getblock(prune_node.getblockhash(2), verbosity=0)
        pruned_height = prune_node.pruneblockchain(400)
        assert_greater_than_or_equal(pruned_height, 2)
        pruned_blockhash = prune_node.getblockhash(2)

        assert_raises_rpc_error(-1, 'Block not available (pruned data)', prune_node.getblock, pruned_blockhash)

        result = prune_node.submitblock(pruned_block)
        assert_equal(result, "inconclusive")
        assert_equal(prune_node.getblock(pruned_blockhash, verbosity=0), pruned_block)


    def send_transactions(self, utxos, fee_rate, target_vsize):
        """
        Create and send transactions with the specified target virtual size
        and fee rate. Return their txids so the test can measure their actual
        witness-inclusive serialized sizes.
        """
        txids = []
        for utxo in utxos:
            result = self.wallet.send_self_transfer(
                from_node=self.nodes[0],
                utxo_to_spend=utxo,
                target_vsize=target_vsize,
                fee_rate=fee_rate,
            )
            txids.append(result["txid"])
        return txids

    def verify_block_template_size(self, max_transaction_bytes):
        """
        Create a block template and verify that its non-coinbase transactions
        fit within the serialized-byte budget left after the miner reservation.
        """
        response = self.nodes[0].getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        total_serialized_bytes = sum(
            len(bytes.fromhex(transaction["data"]))
            for transaction in response["transactions"]
        )

        self.log.info(
            f"Block template contains {len(response['transactions'])} "
            f"transactions totaling {total_serialized_bytes} serialized bytes; "
            f"limit is below {max_transaction_bytes} bytes"
        )

        # BlockAssembler rejects a chunk when adding it would make the capacity
        # counter equal to or exceed the configured maximum.
        assert_greater_than(max_transaction_bytes, total_serialized_bytes)

        return response, total_serialized_bytes

    def test_block_max_size(self):
        self.log.info("Testing Mercatura serialized-byte block creation limits.")

        LARGE_TXS_COUNT = 12
        NORMAL_TXS_COUNT = 4
        LARGE_VSIZE = 90_000
        NORMAL_VSIZE = 500
        HIGH_FEERATE = Decimal("0.10")
        NORMAL_FEERATE = Decimal("0.02")

        # Ensure the mempool is empty.
        assert_equal(len(self.nodes[0].getrawmempool()), 0)

        # Twelve independent near-standard-size transactions guarantee that
        # the mempool contains more transaction data than the initial 1 MiB
        # Mercatura block-capacity era can mine in one template.
        utxos = [
            self.wallet.get_utxo(confirmed_only=True)
            for _ in range(LARGE_TXS_COUNT + NORMAL_TXS_COUNT)
        ]

        large_txids = self.send_transactions(
            utxos[:LARGE_TXS_COUNT],
            HIGH_FEERATE,
            LARGE_VSIZE,
        )
        normal_txids = self.send_transactions(
            utxos[LARGE_TXS_COUNT:],
            NORMAL_FEERATE,
            NORMAL_VSIZE,
        )

        all_txids = large_txids + normal_txids
        assert_equal(len(self.nodes[0].getrawmempool()), len(all_txids))

        mempool_serialized_bytes = sum(
            len(bytes.fromhex(self.nodes[0].getrawtransaction(txid)))
            for txid in all_txids
        )

        self.log.info(
            f"Mempool transactions total {mempool_serialized_bytes} "
            "witness-inclusive serialized bytes."
        )
        assert_greater_than(
            mempool_serialized_bytes,
            MERCATURA_INITIAL_BLOCK_CAPACITY,
        )

        # Default mining policy must automatically use the current consensus
        # capacity: 1 MiB at these low regtest heights.
        default_tx_budget = (
            MERCATURA_INITIAL_BLOCK_CAPACITY - DEFAULT_BLOCK_RESERVED_SIZE
        )

        default_template, default_template_bytes = self.verify_block_template_size(
            default_tx_budget
        )

        # The mempool deliberately exceeds one block, so at least one
        # transaction must remain unmined.
        assert_greater_than(
            len(all_txids),
            len(default_template["transactions"]),
        )

        # A miner may voluntarily choose a smaller serialized block ceiling.
        custom_block_size = 600_000
        self.restart_node(
            0,
            extra_args=[f"-blockmaxsize={custom_block_size}"],
        )

        custom_template, custom_template_bytes = self.verify_block_template_size(
            custom_block_size - DEFAULT_BLOCK_RESERVED_SIZE
        )

        # The materially smaller local ceiling must reduce the amount of
        # serialized transaction data selected.
        assert_greater_than(
            default_template_bytes,
            custom_template_bytes,
        )

        # A configured miner maximum may be as high as Mercatura's final
        # scheduled 1024 MiB ceiling, but at the current height the template
        # must still be clamped to the active 1 MiB consensus capacity.
        self.restart_node(
            0,
            extra_args=[f"-blockmaxsize={MERCATURA_MAX_BLOCK_CAPACITY}"],
        )

        self.verify_block_template_size(default_tx_budget)

        # Reserved template space is also expressed directly in bytes.
        custom_reserved_size = 20_000
        self.restart_node(
            0,
            extra_args=[
                f"-blockmaxsize={custom_block_size}",
                f"-blockreservedsize={custom_reserved_size}",
            ],
        )

        self.verify_block_template_size(
            custom_block_size - custom_reserved_size
        )

        # Values above Mercatura's final scheduled consensus ceiling are
        # configuration errors.
        self.stop_node(0)
        self.nodes[0].assert_start_raises_init_error(
            extra_args=[f"-blockmaxsize={MERCATURA_MAX_BLOCK_CAPACITY + 1}"],
            expected_msg=(
                f"Error: Specified -blockmaxsize "
                f"({MERCATURA_MAX_BLOCK_CAPACITY + 1}) exceeds Mercatura's "
                f"maximum scheduled block capacity "
                f"({MERCATURA_MAX_BLOCK_CAPACITY} bytes)"
            ),
        )

        self.nodes[0].assert_start_raises_init_error(
            extra_args=[
                f"-blockreservedsize={MERCATURA_MAX_BLOCK_CAPACITY + 1}"
            ],
            expected_msg=(
                f"Error: Specified -blockreservedsize "
                f"({MERCATURA_MAX_BLOCK_CAPACITY + 1}) exceeds Mercatura's "
                f"maximum scheduled block capacity "
                f"({MERCATURA_MAX_BLOCK_CAPACITY} bytes)"
            ),
        )

        self.nodes[0].assert_start_raises_init_error(
            extra_args=[f"-blockreservedsize={MINIMUM_BLOCK_RESERVED_SIZE - 1}"],
            expected_msg=(
                f"Error: Specified -blockreservedsize "
                f"({MINIMUM_BLOCK_RESERVED_SIZE - 1}) is lower than "
                f"minimum safety value of ({MINIMUM_BLOCK_RESERVED_SIZE} bytes)"
            ),
        )

        # Restore the node for the remainder of mining_basic.py.
        self.start_node(0)

    def test_height_in_locktime(self):
        self.log.info("Sanity check generated blocks have their coinbase timelocked to their height.")
        self.generate(self.nodes[0], 1, sync_fun=self.no_op)
        block = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 2)
        assert_equal(block["tx"][0]["locktime"], block["height"] - 1)
        assert_equal(block["tx"][0]["vin"][0]["sequence"], MAX_SEQUENCE_NONFINAL)

    def run_test(self):
        node = self.nodes[0]
        self.wallet = MiniWallet(node)
        self.mine_chain()

        self.log.info('getmininginfo')
        mining_info = node.getmininginfo()
        assert_equal(mining_info['blocks'], 200)
        assert_equal(mining_info['chain'], self.chain)
        assert 'currentblocktx' not in mining_info
        assert 'currentblockweight' not in mining_info
        assert_equal(mining_info['bits'], nbits_str(REGTEST_N_BITS))
        assert_equal(mining_info['target'], target_str(REGTEST_TARGET))
        # We don't care about precision, round to avoid mismatch under Valgrind:
        assert_equal(round(mining_info['difficulty'], 10), Decimal('0.0000000005'))
        assert_equal(mining_info['next']['height'], 201)
        assert_equal(mining_info['next']['target'], target_str(REGTEST_TARGET))
        assert_equal(mining_info['next']['bits'], nbits_str(REGTEST_N_BITS))
        assert_equal(round(mining_info['next']['difficulty'], 10), Decimal('0.0000000005'))
        assert_equal(round(mining_info['networkhashps'], 5), Decimal('0.00333'))
        assert_equal(mining_info['pooledtx'], 0)

        self.log.info("getblocktemplate: Test default witness commitment")
        txid = int(self.wallet.send_self_transfer(from_node=node)['wtxid'], 16)
        tmpl = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)

        # Check that default_witness_commitment is present.
        assert 'default_witness_commitment' in tmpl
        witness_commitment = tmpl['default_witness_commitment']

        # Check that default_witness_commitment is correct.
        witness_root = CBlock.get_merkle_root([ser_uint256(0),
                                               ser_uint256(txid)])
        script = get_witness_script(witness_root, 0)
        assert_equal(witness_commitment, script.hex())

        # Mine a block to leave initial block download and clear the mempool
        self.generatetoaddress(node, 1, node.get_deterministic_priv_key().address)
        tmpl = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        self.log.info("getblocktemplate: Test capability advertised")
        assert 'proposal' in tmpl['capabilities']
        assert 'coinbasetxn' not in tmpl

        next_height = int(tmpl["height"])
        coinbase_tx = create_coinbase(height=next_height)
        # sequence numbers must not be max for nLockTime to have effect
        coinbase_tx.vin[0].nSequence = 2**32 - 2

        block = CBlock()
        block.nVersion = tmpl["version"]
        block.hashPrevBlock = int(tmpl["previousblockhash"], 16)
        block.nTime = tmpl["curtime"]
        block.nBits = int(tmpl["bits"], 16)
        block.nNonce = 0
        block.vtx = [coinbase_tx]
        block.hashMerkleRoot = block.calc_merkle_root()

        self.log.info("getblocktemplate: segwit rule must be set")
        assert_raises_rpc_error(-8, "getblocktemplate must be called with the segwit rule set", node.getblocktemplate, {})

        self.log.info("submitblock: Test block decode failure")
        assert_raises_rpc_error(-22, "Block decode failed", node.submitblock, block.serialize()[:-15].hex())

        self.log.info("submitblock: Test empty block")
        assert_equal('high-hash', node.submitblock(hexdata=CBlock().serialize().hex()))

        self.log.info('submitheader tests')
        assert_raises_rpc_error(-22, 'Block header decode failed', lambda: node.submitheader(hexdata='xx' * BLOCK_HEADER_SIZE))
        assert_raises_rpc_error(-22, 'Block header decode failed', lambda: node.submitheader(hexdata='ff' * (BLOCK_HEADER_SIZE-2)))

        missing_ancestor_block = copy.deepcopy(block)
        missing_ancestor_block.hashPrevBlock = 123
        assert_raises_rpc_error(-25, 'Must submit previous header', lambda: node.submitheader(hexdata=super(CBlock, missing_ancestor_block).serialize().hex()))

        block.nTime += 1
        block.solve()

        def chain_tip(b_hash, *, status='headers-only', branchlen=1):
            return {'hash': b_hash, 'height': 202, 'branchlen': branchlen, 'status': status}

        assert chain_tip(block.hash_hex) not in node.getchaintips()
        node.submitheader(hexdata=block.serialize().hex())
        assert chain_tip(block.hash_hex) in node.getchaintips()
        node.submitheader(hexdata=CBlockHeader(block).serialize().hex())  # Noop
        assert chain_tip(block.hash_hex) in node.getchaintips()

        bad_block_root = copy.deepcopy(block)
        bad_block_root.hashMerkleRoot += 2
        bad_block_root.solve()
        assert chain_tip(bad_block_root.hash_hex) not in node.getchaintips()
        node.submitheader(hexdata=CBlockHeader(bad_block_root).serialize().hex())
        assert chain_tip(bad_block_root.hash_hex) in node.getchaintips()
        # Should still reject invalid blocks, even if we have the header:
        assert_equal(node.submitblock(hexdata=bad_block_root.serialize().hex()), 'bad-txnmrklroot')
        assert_equal(node.submitblock(hexdata=bad_block_root.serialize().hex()), 'bad-txnmrklroot')
        assert chain_tip(bad_block_root.hash_hex) in node.getchaintips()
        # We know the header for this invalid block, so should just return early without error:
        node.submitheader(hexdata=CBlockHeader(bad_block_root).serialize().hex())
        assert chain_tip(bad_block_root.hash_hex) in node.getchaintips()

        bad_block_lock = copy.deepcopy(block)
        bad_block_lock.vtx[0].nLockTime = 2**32 - 1
        bad_block_lock.hashMerkleRoot = bad_block_lock.calc_merkle_root()
        bad_block_lock.solve()
        assert_equal(node.submitblock(hexdata=bad_block_lock.serialize().hex()), 'bad-txns-nonfinal')
        assert_equal(node.submitblock(hexdata=bad_block_lock.serialize().hex()), 'duplicate-invalid')
        # Build a "good" block on top of the submitted bad block
        bad_block2 = copy.deepcopy(block)
        bad_block2.hashPrevBlock = bad_block_lock.hash_int
        bad_block2.solve()
        assert_raises_rpc_error(-25, 'bad-prevblk', lambda: node.submitheader(hexdata=CBlockHeader(bad_block2).serialize().hex()))

        # Should reject invalid header right away
        bad_block_time = copy.deepcopy(block)
        bad_block_time.nTime = 1
        bad_block_time.solve()
        assert_raises_rpc_error(-25, 'time-too-old', lambda: node.submitheader(hexdata=CBlockHeader(bad_block_time).serialize().hex()))

        # Should ask for the block from a p2p node, if they announce the header as well:
        peer = node.add_p2p_connection(P2PDataStore())
        peer.wait_for_getheaders(timeout=5, block_hash=block.hashPrevBlock)
        peer.send_blocks_and_test(blocks=[block], node=node)
        # Must be active now:
        assert chain_tip(block.hash_hex, status='active', branchlen=0) in node.getchaintips()

        # Building a few blocks should give the same results
        self.generatetoaddress(node, 10, node.get_deterministic_priv_key().address)
        assert_raises_rpc_error(-25, 'time-too-old', lambda: node.submitheader(hexdata=CBlockHeader(bad_block_time).serialize().hex()))
        assert_raises_rpc_error(-25, 'bad-prevblk', lambda: node.submitheader(hexdata=CBlockHeader(bad_block2).serialize().hex()))
        node.submitheader(hexdata=CBlockHeader(block).serialize().hex())
        node.submitheader(hexdata=CBlockHeader(bad_block_root).serialize().hex())
        assert_equal(node.submitblock(hexdata=block.serialize().hex()), 'duplicate')  # valid

        self.test_fees_and_sigops()
        self.test_blockmintxfee_parameter()
        self.test_block_max_size()
        self.test_timewarp()
        self.test_pruning()
        self.test_height_in_locktime()


if __name__ == '__main__':
    MiningTest(__file__).main()
