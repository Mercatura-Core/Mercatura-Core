#!/usr/bin/env python3
# Copyright (c) 2014-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the -alertnotify, -blocknotify and -walletnotify options."""
import os
import platform

from test_framework.address import ADDRESS_MCRT1_UNSPENDABLE
from test_framework.blocktools import (
    create_block,
    create_coinbase,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
)

# Linux allow all characters other than \x00
# Windows disallow control characters (0-31) and /\?%:|"<>
FILE_CHAR_START = 32 if platform.system() == 'Windows' else 1
FILE_CHAR_END = 128
FILE_CHARS_DISALLOWED = '/\\?%*:|"<>' if platform.system() == 'Windows' else '/'
UNCONFIRMED_HASH_STRING = 'unconfirmed'

LARGE_WORK_INVALID_CHAIN_WARNING = (
    "Warning: Found invalid chain more than 6 blocks longer than our best chain. This could be due to database corruption or consensus incompatibility with peers."
)


def notify_outputname(walletname, txid):
    return txid if platform.system() == 'Windows' else f'{walletname}_{txid}'


class NotificationsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.uses_wallet = None

    def setup_network(self):
        self.wallet = ''.join(chr(i) for i in range(FILE_CHAR_START, FILE_CHAR_END) if chr(i) not in FILE_CHARS_DISALLOWED)
        self.alertnotify_dir = os.path.join(self.options.tmpdir, "alertnotify")
        self.alertnotify_file = os.path.join(self.alertnotify_dir, "alertnotify.txt")
        self.blocknotify_dir = os.path.join(self.options.tmpdir, "blocknotify")
        self.walletnotify_dir = os.path.join(self.options.tmpdir, "walletnotify")
        self.shutdownnotify_dir = os.path.join(self.options.tmpdir, "shutdownnotify")
        self.shutdownnotify_file = os.path.join(self.shutdownnotify_dir, "shutdownnotify.txt")
        os.mkdir(self.alertnotify_dir)
        os.mkdir(self.blocknotify_dir)
        os.mkdir(self.walletnotify_dir)
        os.mkdir(self.shutdownnotify_dir)

        # -alertnotify and -blocknotify on node0, walletnotify on node1
        self.extra_args = [[
            f"-alertnotify=echo %s >> {self.alertnotify_file}",
            f"-blocknotify=echo > {os.path.join(self.blocknotify_dir, '%s')}",
            f"-shutdownnotify=echo > {self.shutdownnotify_file}",
        ], [
            f"-walletnotify=echo %h_%b > {os.path.join(self.walletnotify_dir, notify_outputname('%w', '%s'))}",
        ]]
        self.wallet_names = [self.default_wallet_name, self.wallet]
        super().setup_network()

    def run_test(self):
        if self.is_wallet_compiled():
            # The original Bitcoin fixture imported the same WPKH xpriv
            # descriptors into both nodes. Mercatura disables classical
            # ownership, so instead create one native PQ wallet and restore
            # its backup on node 1. This preserves the test's requirement
            # that both nodes share the same wallet ownership.
            self.nodes[0].createwallet(
                wallet_name=self.wallet_names[0],
                load_on_startup=True,
            )
            source_wallet = self.nodes[0].get_wallet_rpc(
                self.wallet_names[0]
            )

            # Derive the mining destination before taking the backup so both
            # copies explicitly know the same PQ destination.
            self.shared_wallet_address = source_wallet.getnewaddress(
                "shared-notification-mining"
            )

            backup_file = (
                self.nodes[0].datadir_path /
                "feature_notifications_pq_shared.dat"
            )
            source_wallet.backupwallet(backup_file)

            restore_result = self.nodes[1].restorewallet(
                self.wallet_names[1],
                backup_file,
            )
            assert_equal(
                restore_result["name"],
                self.wallet_names[1],
            )

            restored_wallet = self.nodes[1].get_wallet_rpc(
                self.wallet_names[1]
            )
            restored_wallet.syncwithvalidationinterfacequeue()

            assert_equal(
                source_wallet.getaddressinfo(
                    self.shared_wallet_address
                )["ismine"],
                True,
            )
            assert_equal(
                restored_wallet.getaddressinfo(
                    self.shared_wallet_address
                )["ismine"],
                True,
            )

        self.log.info("test -blocknotify")
        block_count = 10
        blocks = self.generatetoaddress(
            self.nodes[1],
            block_count,
            self.shared_wallet_address if self.is_wallet_compiled() else ADDRESS_MCRT1_UNSPENDABLE,
        )

        # wait at most 10 seconds for expected number of files before reading the content
        self.wait_until(lambda: len(os.listdir(self.blocknotify_dir)) == block_count, timeout=10)

        # directory content should equal the generated blocks hashes
        assert_equal(sorted(blocks), sorted(os.listdir(self.blocknotify_dir)))

        if self.is_wallet_compiled():
            self.log.info("test -walletnotify")
            # wait at most 10 seconds for expected number of files before reading the content
            self.wait_until(lambda: len(os.listdir(self.walletnotify_dir)) == block_count, timeout=10)

            # directory content should equal the generated transaction hashes
            tx_details = list(map(lambda t: (t['txid'], t['blockheight'], t['blockhash']), self.nodes[1].listtransactions("*", block_count)))
            self.expect_wallet_notify(tx_details)

            self.log.info("test -walletnotify after rescan")
            # rescan to force wallet notifications
            self.nodes[1].rescanblockchain()
            self.wait_until(lambda: len(os.listdir(self.walletnotify_dir)) == block_count, timeout=10)

            self.connect_nodes(0, 1)

            # directory content should equal the generated transaction hashes
            tx_details = list(map(lambda t: (t['txid'], t['blockheight'], t['blockhash']), self.nodes[1].listtransactions("*", block_count)))
            self.expect_wallet_notify(tx_details)

            # Conflicting transactions tests.
            # Generate spends from node 0, and check notifications
            # triggered by node 1
            self.log.info("test -walletnotify with conflicting transactions")
            self.nodes[0].rescanblockchain()
            self.generatetoaddress(self.nodes[0], 100, ADDRESS_MCRT1_UNSPENDABLE)

            # Generate transaction on node 0, sync mempools, and check for
            # notification on node 1.
            # The restored PQ wallet has the same master seed as node 0,
            # but unlike the inherited ranged WPKH descriptor fixture it
            # cannot discover arbitrary future PQ change commitments from
            # public derivation. Advance its internal derivation in lockstep
            # so both wallet copies know node 0's next change destination.
            tx1_expected_change = restored_wallet.getrawchangeaddress()

            tx1 = self.nodes[0].sendtoaddress(
                address=ADDRESS_MCRT1_UNSPENDABLE,
                amount=1,
                replaceable=True,
            )

            tx1_decoded = source_wallet.decoderawtransaction(
                source_wallet.gettransaction(tx1)["hex"]
            )
            assert tx1_expected_change in [
                output["scriptPubKey"].get("address")
                for output in tx1_decoded["vout"]
            ]

            assert_equal(tx1 in self.nodes[0].getrawmempool(), True)
            self.sync_mempools()
            self.expect_wallet_notify([(tx1, -1, UNCONFIRMED_HASH_STRING)])

            # Generate bump transaction, sync mempools, and check for bump1
            # notification. In the future, per
            # https://github.com/bitcoin/bitcoin/pull/9371, it might be better
            # to have notifications for both tx1 and bump1.
            bump1 = self.nodes[0].bumpfee(tx1)["txid"]
            assert_equal(bump1 in self.nodes[0].getrawmempool(), True)
            self.sync_mempools()
            self.expect_wallet_notify([(bump1, -1, UNCONFIRMED_HASH_STRING)])

            # Add bump1 transaction to new block, checking for a notification
            # and the correct number of confirmations.
            blockhash1 = self.generatetoaddress(self.nodes[0], 1, ADDRESS_MCRT1_UNSPENDABLE)[0]
            blockheight1 = self.nodes[0].getblockcount()
            self.sync_blocks()
            self.expect_wallet_notify([(bump1, blockheight1, blockhash1)])
            assert_equal(self.nodes[1].gettransaction(bump1)["confirmations"], 1)

            # Generate a second transaction to be bumped.
            # Keep the restored wallet's PQ internal derivation synchronized
            # with the source wallet for the second spend as well.
            tx2_expected_change = restored_wallet.getrawchangeaddress()

            tx2 = self.nodes[0].sendtoaddress(
                address=ADDRESS_MCRT1_UNSPENDABLE,
                amount=1,
                replaceable=True,
            )

            tx2_decoded = source_wallet.decoderawtransaction(
                source_wallet.gettransaction(tx2)["hex"]
            )
            assert tx2_expected_change in [
                output["scriptPubKey"].get("address")
                for output in tx2_decoded["vout"]
            ]

            assert_equal(tx2 in self.nodes[0].getrawmempool(), True)
            self.sync_mempools()
            self.expect_wallet_notify([(tx2, -1, UNCONFIRMED_HASH_STRING)])

            # Bump tx2 as bump2 and generate a block on node 0 while
            # disconnected, then reconnect and check for notifications on node 1
            # about newly confirmed bump2 and newly conflicted tx2.
            self.disconnect_nodes(0, 1)
            bump2 = self.nodes[0].bumpfee(tx2)["txid"]
            blockhash2 = self.generatetoaddress(self.nodes[0], 1, ADDRESS_MCRT1_UNSPENDABLE, sync_fun=self.no_op)[0]
            blockheight2 = self.nodes[0].getblockcount()
            assert_equal(self.nodes[0].gettransaction(bump2)["confirmations"], 1)
            assert_equal(tx2 in self.nodes[1].getrawmempool(), True)
            self.connect_nodes(0, 1)
            self.sync_blocks()
            self.expect_wallet_notify([(bump2, blockheight2, blockhash2), (tx2, -1, UNCONFIRMED_HASH_STRING)])
            assert_equal(self.nodes[1].gettransaction(bump2)["confirmations"], 1)

        self.log.info("test -alertnotify with large work invalid chain")
        # create a bunch of invalid blocks
        tip = self.nodes[0].getbestblockhash()
        height = self.nodes[0].getblockcount() + 1
        block_time = self.nodes[0].getblock(tip)['time'] + 1

        # Use Mercatura's live template value rather than the inherited
        # Bitcoin create_coinbase() subsidy. The template coinbase value is
        # the maximum currently permitted reward including any template fees,
        # so adding one base unit guarantees an excessive coinbase.
        invalid_coinbase_value = (
            self.nodes[0].getblocktemplate({"rules": ["segwit"]})["coinbasevalue"]
            + 1
        )

        invalid_blocks = []
        for _ in range(7):  # invalid chain must be longer than 6 blocks to trigger warning
            block = create_block(
                int(tip, 16),
                create_coinbase(height, nValue=invalid_coinbase_value),
                block_time,
            )
            block.hashMerkleRoot = block.calc_merkle_root()
            self.solve_mercatura_block(self.nodes[0], block)
            invalid_blocks.append(block)
            tip = block.hash_hex
            height += 1
            block_time += 1

        # submit headers of invalid blocks
        for invalid_block in invalid_blocks:
            self.nodes[0].submitheader(invalid_block.serialize().hex())
        # submit invalid blocks in reverse order (tip first, to set m_best_invalid)
        for invalid_block in reversed(invalid_blocks):
            self.nodes[0].submitblock(invalid_block.serialize().hex())

        self.wait_until(lambda: os.path.isfile(self.alertnotify_file), timeout=10)
        self.wait_until(self.large_work_invalid_chain_warning_in_alert_file, timeout=10)

        self.log.info("test -shutdownnotify")
        self.stop_nodes()
        self.wait_until(lambda: os.path.isfile(self.shutdownnotify_file), timeout=10)

    def large_work_invalid_chain_warning_in_alert_file(self):
        with open(self.alertnotify_file, 'r') as f:
            alert_text = f.read()
        return LARGE_WORK_INVALID_CHAIN_WARNING in alert_text

    def expect_wallet_notify(self, tx_details):
        self.wait_until(lambda: len(os.listdir(self.walletnotify_dir)) >= len(tx_details), timeout=10)
        # Should have no more and no less files than expected
        assert_equal(sorted(notify_outputname(self.wallet, tx_id) for tx_id, _, _ in tx_details), sorted(os.listdir(self.walletnotify_dir)))
        # Should now verify contents of each file
        for tx_id, blockheight, blockhash in tx_details:
            fname = os.path.join(self.walletnotify_dir, notify_outputname(self.wallet, tx_id))
            # Wait for the cached writes to hit storage
            self.wait_until(lambda: os.path.getsize(fname) > 0, timeout=10)
            with open(fname, 'rt') as f:
                text = f.read()
                # Universal newline ensures '\n' on 'nt'
                assert_equal(text[-1], '\n')
                text = text[:-1]
                if platform.system() == 'Windows':
                    # On Windows, echo as above will append a whitespace
                    assert_equal(text[-1], ' ')
                    text = text[:-1]
                expected = str(blockheight) + '_' + blockhash
                assert_equal(text, expected)

        for tx_file in os.listdir(self.walletnotify_dir):
            os.remove(os.path.join(self.walletnotify_dir, tx_file))


if __name__ == '__main__':
    NotificationsTest(__file__).main()
