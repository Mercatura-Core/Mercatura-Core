#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license.

"""End-to-end Mercatura PQ wallet/RPC functional test."""

from decimal import Decimal

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


PQ_ADDRESS_PREFIX = "mcrt1z"
PQ_SCRIPT_TYPE = "witness_v2_mercatura_pq"

MERCATURA_PSBT_IDENTIFIER = "4d6572636174757261"
MERCATURA_PQ_PUBKEY_SUBTYPE = 1
MERCATURA_PQ_SIGNATURE_SUBTYPE = 2
MERCATURA_PQ_PUBKEY_HEX_LEN = 1952 * 2
MERCATURA_PQ_SIGNATURE_HEX_LEN = 3309 * 2


class MercaturaPQWalletTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

        # Mercatura's wallet needs a usable fallback fee before fee
        # estimation has enough chain history.
        self.extra_args = [
            ["-fallbackfee=0.01"],
            ["-fallbackfee=0.01"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def assert_pq_address(self, wallet, address):
        assert address.startswith(PQ_ADDRESS_PREFIX), address

        info = wallet.getaddressinfo(address)

        assert_equal(info["ismine"], True)
        assert_equal(info["iswitness"], True)
        assert_equal(info["witness_version"], 2)
        assert_equal(len(info["witness_program"]), 64)

        assert_equal(
            info["scriptPubKey"],
            "5220" + info["witness_program"],
        )

    def assert_transaction_outputs_are_pq(self, wallet, txid):
        wallet_tx = wallet.gettransaction(txid)
        decoded = wallet.decoderawtransaction(wallet_tx["hex"])

        assert len(decoded["vout"]) > 0

        for output in decoded["vout"]:
            assert_equal(
                output["scriptPubKey"]["type"],
                PQ_SCRIPT_TYPE,
            )

        return decoded

    def mine_to_wallet(self, node, wallet, blocks):
        address = wallet.getnewaddress("mining")
        self.assert_pq_address(wallet, address)

        remaining = blocks

        # MercaHash regtest mining is substantially heavier than
        # vanilla Bitcoin regtest. Mine in bounded batches so one
        # RPC call stays below the functional framework timeout.
        while remaining > 0:
            batch = min(remaining, 20)

            node.generatetoaddress(
                batch,
                address,
                called_by_framework=True,
            )

            remaining -= batch

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]

        # ----------------------------------------------------------
        # Create normal Mercatura wallets through the public RPC.
        # ----------------------------------------------------------

        self.log.info("Creating Mercatura PQ wallets")

        node0.createwallet(
            wallet_name="sender",
        )

        node1.createwallet(
            wallet_name="receiver",
        )

        sender = node0.get_wallet_rpc("sender")
        receiver = node1.get_wallet_rpc("receiver")

        # ----------------------------------------------------------
        # Public receive and change RPCs must produce PQ v2 outputs.
        # ----------------------------------------------------------

        self.log.info("Testing PQ receive/change address RPCs")

        receive0 = sender.getnewaddress("receive0")
        self.assert_pq_address(sender, receive0)

        change0 = sender.getrawchangeaddress()
        self.assert_pq_address(sender, change0)

        assert receive0 != change0

        # Even an inherited classical address-type request must never
        # escape Mercatura's PQ-only ownership model.
        requested_legacy = sender.getnewaddress(
            "legacy-request",
            "legacy",
        )

        self.assert_pq_address(
            sender,
            requested_legacy,
        )

        requested_legacy_change = sender.getrawchangeaddress(
            "legacy",
        )

        self.assert_pq_address(
            sender,
            requested_legacy_change,
        )

        # ----------------------------------------------------------
        # Fund the sender with mature regtest coinbase outputs.
        # ----------------------------------------------------------

        self.log.info("Mining mature funds to PQ wallet")

        self.mine_to_wallet(
            node0,
            sender,
            COINBASE_MATURITY + 1,
        )

        self.sync_blocks()

        assert sender.getbalance() > Decimal("0")

        # ----------------------------------------------------------
        # Normal sendtoaddress workflow.
        # ----------------------------------------------------------

        self.log.info("Testing PQ sendtoaddress and PQ change")

        receiver_address = receiver.getnewaddress(
            "receive-from-sender"
        )

        self.assert_pq_address(
            receiver,
            receiver_address,
        )

        txid = sender.sendtoaddress(
            receiver_address,
            Decimal("1.00"),
        )

        self.sync_mempools()

        # Recipient and wallet change must both remain native
        # Mercatura PQ witness-v2 outputs.
        self.assert_transaction_outputs_are_pq(
            sender,
            txid,
        )

        self.mine_to_wallet(
            node0,
            sender,
            1,
        )

        self.sync_blocks()

        assert_equal(
            receiver.getbalance(),
            Decimal("1.00"),
        )

        # ----------------------------------------------------------
        # Multi-output batching / sendmany.
        # ----------------------------------------------------------

        self.log.info("Testing PQ multi-output sendmany")

        multi_a = receiver.getnewaddress("multi-a")
        multi_b = receiver.getnewaddress("multi-b")

        self.assert_pq_address(
            receiver,
            multi_a,
        )

        self.assert_pq_address(
            receiver,
            multi_b,
        )

        balance_before = receiver.getbalance()

        multi_txid = sender.sendmany(
            "",
            {
                multi_a: Decimal("0.50"),
                multi_b: Decimal("0.75"),
            },
        )

        self.sync_mempools()

        self.assert_transaction_outputs_are_pq(
            sender,
            multi_txid,
        )

        self.mine_to_wallet(
            node0,
            sender,
            1,
        )

        self.sync_blocks()

        assert_equal(
            receiver.getbalance(),
            balance_before + Decimal("1.25"),
        )

        # ----------------------------------------------------------
        # Real wallet PSBT RPC workflow.
        # ----------------------------------------------------------

        self.log.info("Testing Mercatura PQ PSBT RPC workflow")

        psbt_receive = receiver.getnewaddress(
            "psbt-receive"
        )

        self.assert_pq_address(
            receiver,
            psbt_receive,
        )

        balance_before_psbt = receiver.getbalance()

        funded_psbt = sender.walletcreatefundedpsbt(
            [],
            {
                psbt_receive: Decimal("0.40"),
            },
        )

        unsigned_psbt = funded_psbt["psbt"]

        decoded_unsigned = node0.decodepsbt(
            unsigned_psbt
        )

        assert len(decoded_unsigned["inputs"]) > 0

        # Sign using the real Mercatura wallet path, but deliberately
        # leave the PSBT unfinalized so the proprietary PQ fields
        # remain directly inspectable through decodepsbt.
        processed = sender.walletprocesspsbt(
            psbt=unsigned_psbt,
            finalize=False,
        )

        assert "psbt" in processed
        assert "hex" not in processed

        signed_psbt = processed["psbt"]

        decoded_signed = node0.decodepsbt(
            signed_psbt
        )

        assert_equal(
            len(decoded_signed["inputs"]),
            len(decoded_unsigned["inputs"]),
        )

        # Every wallet-selected input is native Mercatura PQ and must
        # carry both proprietary authorization fields:
        #
        #   subtype 1 -> 1952-byte ML-DSA-65 public key
        #   subtype 2 -> 3309-byte ML-DSA-65 signature
        #
        # Both use identifier "Mercatura" and empty key-data.
        for psbt_input in decoded_signed["inputs"]:
            mercatura_fields = {
                field["subtype"]: field
                for field in psbt_input.get(
                    "proprietary",
                    [],
                )
                if field["identifier"] ==
                MERCATURA_PSBT_IDENTIFIER
            }

            assert (
                MERCATURA_PQ_PUBKEY_SUBTYPE
                in mercatura_fields
            )

            assert (
                MERCATURA_PQ_SIGNATURE_SUBTYPE
                in mercatura_fields
            )

            pq_pubkey = mercatura_fields[
                MERCATURA_PQ_PUBKEY_SUBTYPE
            ]

            pq_signature = mercatura_fields[
                MERCATURA_PQ_SIGNATURE_SUBTYPE
            ]

            # decodepsbt exposes the complete serialized
            # proprietary PSBT key:
            #
            #   0xfc || CompactSize(identifier length)
            #        || "Mercatura"
            #        || CompactSize(subtype)
            #        || key-data
            #
            # Mercatura PQ v1 requires empty key-data, so the
            # serialized key ends immediately after the subtype.
            assert_equal(
                pq_pubkey["key"],
                "fc09" +
                MERCATURA_PSBT_IDENTIFIER +
                "01",
            )

            assert_equal(
                pq_signature["key"],
                "fc09" +
                MERCATURA_PSBT_IDENTIFIER +
                "02",
            )

            assert_equal(
                len(pq_pubkey["value"]),
                MERCATURA_PQ_PUBKEY_HEX_LEN,
            )

            assert_equal(
                len(pq_signature["value"]),
                MERCATURA_PQ_SIGNATURE_HEX_LEN,
            )

        # Finalization must convert the proprietary PQ authorization
        # data into the exact native witness-v2 spend.
        finalized = node0.finalizepsbt(
            signed_psbt
        )

        assert_equal(
            finalized["complete"],
            True,
        )

        assert "hex" in finalized

        final_hex = finalized["hex"]

        acceptance = node0.testmempoolaccept(
            [final_hex]
        )[0]

        assert_equal(
            acceptance["allowed"],
            True,
        )

        psbt_txid = node0.sendrawtransaction(
            final_hex
        )

        self.sync_mempools()

        self.assert_transaction_outputs_are_pq(
            sender,
            psbt_txid,
        )

        self.mine_to_wallet(
            node0,
            sender,
            1,
        )

        self.sync_blocks()

        assert_equal(
            receiver.getbalance(),
            balance_before_psbt + Decimal("0.40"),
        )

        # ----------------------------------------------------------
        # PQ transaction state must survive block disconnect/reconnect.
        # ----------------------------------------------------------

        self.log.info(
            "Testing PQ transaction handling across block reorg"
        )

        psbt_block_hash = node0.getbestblockhash()

        assert_equal(
            receiver.gettransaction(
                psbt_txid
            )["confirmations"],
            1,
        )

        # Invalidate the containing block on both connected nodes so
        # they agree on the temporary shorter chain.
        node0.invalidateblock(
            psbt_block_hash
        )

        node1.invalidateblock(
            psbt_block_hash
        )

        receiver.syncwithvalidationinterfacequeue()

        assert_equal(
            receiver.gettransaction(
                psbt_txid
            )["confirmations"],
            0,
        )

        # Ownership information is wallet metadata and must survive
        # the chain disconnect.
        self.assert_pq_address(
            receiver,
            psbt_receive,
        )

        # The disconnected valid transaction should return to the
        # mempool rather than becoming unusable.
        self.sync_mempools()

        assert psbt_txid in node0.getrawmempool()
        assert psbt_txid in node1.getrawmempool()

        # Restore the exact same block and ensure wallet confirmation
        # state follows the chain back to the original tip.
        node0.reconsiderblock(
            psbt_block_hash
        )

        node1.reconsiderblock(
            psbt_block_hash
        )

        self.sync_blocks()
        self.sync_mempools()

        receiver.syncwithvalidationinterfacequeue()

        assert_equal(
            node0.getbestblockhash(),
            psbt_block_hash,
        )

        assert_equal(
            node1.getbestblockhash(),
            psbt_block_hash,
        )

        assert_equal(
            receiver.gettransaction(
                psbt_txid
            )["confirmations"],
            1,
        )

        self.assert_pq_address(
            receiver,
            psbt_receive,
        )

        # ----------------------------------------------------------
        # Wallet unload/reload must preserve PQ ownership metadata.
        # ----------------------------------------------------------

        self.log.info("Testing PQ wallet unload/reload persistence")

        old_receiver_address = receiver_address

        node1.unloadwallet("receiver")
        node1.loadwallet("receiver")

        receiver = node1.get_wallet_rpc("receiver")

        self.assert_pq_address(
            receiver,
            old_receiver_address,
        )

        after_reload = receiver.getnewaddress(
            "after-wallet-reload"
        )

        self.assert_pq_address(
            receiver,
            after_reload,
        )

        assert after_reload != old_receiver_address

        # ----------------------------------------------------------
        # Full node restart must preserve PQ state and derivation
        # counters without regenerating the master seed.
        # ----------------------------------------------------------

        self.log.info("Testing PQ wallet persistence across node restart")

        self.restart_node(1)

        self.connect_nodes(0, 1)
        self.sync_blocks()

        # The createwallet RPC in this Mercatura/Bitcoin Core
        # baseline does not expose load_on_start. Explicitly reload
        # the wallet after node restart so this test exercises PQ
        # database persistence rather than startup-wallet settings.
        if "receiver" not in self.nodes[1].listwallets():
            self.nodes[1].loadwallet("receiver")

        receiver = self.nodes[1].get_wallet_rpc(
            "receiver"
        )

        self.assert_pq_address(
            receiver,
            old_receiver_address,
        )

        self.assert_pq_address(
            receiver,
            after_reload,
        )

        after_restart = receiver.getnewaddress(
            "after-node-restart"
        )

        self.assert_pq_address(
            receiver,
            after_restart,
        )

        assert after_restart != old_receiver_address
        assert after_restart != after_reload

        # ----------------------------------------------------------
        # Full chain reindex must preserve PQ wallet ownership,
        # transaction recognition, derivation state, and spendability.
        # ----------------------------------------------------------

        self.log.info(
            "Testing PQ wallet persistence across -reindex"
        )

        balance_before_reindex = receiver.getbalance()

        assert (
            receiver.gettransaction(
                psbt_txid
            )["confirmations"] > 0
        )

        self.restart_node(
            1,
            extra_args=["-reindex"],
        )

        self.connect_nodes(0, 1)
        self.sync_blocks()

        if "receiver" not in self.nodes[1].listwallets():
            self.nodes[1].loadwallet(
                "receiver"
            )

        receiver = self.nodes[1].get_wallet_rpc(
            "receiver"
        )

        receiver.syncwithvalidationinterfacequeue()

        # Previously derived PQ destinations must remain owned after
        # rebuilding chain state.
        for known_address in [
            old_receiver_address,
            after_reload,
            after_restart,
            psbt_receive,
        ]:
            self.assert_pq_address(
                receiver,
                known_address,
            )

        assert_equal(
            receiver.getbalance(),
            balance_before_reindex,
        )

        assert (
            receiver.gettransaction(
                psbt_txid
            )["confirmations"] > 0
        )

        # Derivation counters must also continue forward after reindex.
        after_reindex = receiver.getnewaddress(
            "after-reindex"
        )

        self.assert_pq_address(
            receiver,
            after_reindex,
        )

        assert after_reindex not in {
            old_receiver_address,
            after_reload,
            after_restart,
            psbt_receive,
        }

        # Finally prove the reindexed wallet can still create and sign
        # a fresh ML-DSA-65 spend, not merely recognize old outputs.
        self.log.info(
            "Testing PQ spendability after -reindex"
        )

        post_reindex_destination = sender.getnewaddress(
            "post-reindex-receive"
        )

        self.assert_pq_address(
            sender,
            post_reindex_destination,
        )

        received_before = sender.getreceivedbyaddress(
            post_reindex_destination
        )

        post_reindex_txid = receiver.sendtoaddress(
            post_reindex_destination,
            Decimal("0.10"),
        )

        self.sync_mempools()

        self.assert_transaction_outputs_are_pq(
            receiver,
            post_reindex_txid,
        )

        self.mine_to_wallet(
            node0,
            sender,
            1,
        )

        self.sync_blocks()

        assert (
            receiver.gettransaction(
                post_reindex_txid
            )["confirmations"] > 0
        )

        assert_equal(
            sender.getreceivedbyaddress(
                post_reindex_destination
            ),
            received_before + Decimal("0.10"),
        )

        self.log.info(
            "Mercatura PQ wallet/RPC workflow passed"
        )


if __name__ == "__main__":
    MercaturaPQWalletTest(__file__).main()