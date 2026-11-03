"""Explicit native wallet, hostile-wire and credited-deposit reorg scenarios."""
import copy
import time
from test_framework.messages import CTransaction,CTxOut,from_hex
from test_framework.psbt import PSBT,PSBT_IN_WITNESS_UTXO
from test_framework.util import assert_equal,assert_raises_rpc_error
from simulation.core import units,mca

class Lifecycle:
    def hostile(self):
        coin=next(c for c in self.ledger.inventory('customers') if c['value']>=200)
        original=self.transaction('customers',[coin],[dict(**self.change['customers'],value=100)],'hostile-control')
        assert self.nodes[0].testmempoolaccept([original['raw']])[0]['allowed']
        before=self.nodes[0].gettxoutsetinfo()['hash_serialized_3']
        for kind in ('signature','public_key','output_value','sequence','oversized_signature','duplicate_input'):
            tx=from_hex(CTransaction(),original['raw'])
            if kind in ('signature','public_key','oversized_signature'):
                index=1 if kind=='public_key' else 0
                data=bytearray(tx.wit.vtxinwit[0].scriptWitness.stack[index])
                if kind=='oversized_signature':data.append(0)
                else:data[100]^=1
                tx.wit.vtxinwit[0].scriptWitness.stack[index]=bytes(data)
            elif kind=='output_value':tx.vout[0].nValue-=1
            elif kind=='sequence':tx.vin[0].nSequence-=1
            else:tx.vin.append(copy.deepcopy(tx.vin[0]));tx.wit.vtxinwit.append(copy.deepcopy(tx.wit.vtxinwit[0]))
            start=time.monotonic();results=[]
            for node in self.nodes:
                first=node.testmempoolaccept([tx.serialize().hex()])[0];second=node.testmempoolaccept([tx.serialize().hex()])[0]
                assert_equal(first['allowed'],False);assert_equal(first['reject-reason'],second['reject-reason']);results.append(first['reject-reason'])
            assert_equal(len(set(results)),1)
            self.metrics.event('native_rejection',mutation=kind,reason=results[0],nodes=len(results),validation_ms=(time.monotonic()-start)*1000)
        assert_equal(self.nodes[0].gettxoutsetinfo()['hash_serialized_3'],before)
    def psbt_roundtrip(self):
        customer=next(c for c in self.ledger.inventory('customers') if c['value']>=200)
        exchange=next(c for c in self.ledger.inventory('exchange') if c['value']>=200)
        coins=[customer,exchange]
        outputs=[dict(**self.change['customers'],value=customer['value']-11),dict(**self.change['exchange'],value=exchange['value']-10)]
        inputs=[dict(txid=c['txid'],vout=c['vout'],sequence=0xfffffffd) for c in coins]
        from simulation.core import mca
        unsigned=self.nodes[0].createpsbt(inputs,[{o['address']:mca(o['value'])} for o in outputs])
        public=PSBT.from_base64(unsigned)
        for index,coin in enumerate(coins):
            actual=self.nodes[0].gettxout(coin['txid'],coin['vout'])
            assert actual is not None
            assert_equal(units(actual['value']),coin['value']);assert_equal(actual['scriptPubKey']['hex'],coin['script'])
            public.i[index].map[PSBT_IN_WITNESS_UTXO]=CTxOut(coin['value'],bytes.fromhex(coin['script'])).serialize()
        unsigned=public.to_base64()
        left=self.wallets['customers'].walletprocesspsbt(unsigned)
        right=self.wallets['exchange'].walletprocesspsbt(unsigned)
        assert not left['complete'] and not right['complete']
        combined=self.nodes[0].combinepsbt([left['psbt'],right['psbt']]);final=self.nodes[0].finalizepsbt(combined)
        assert final['complete'];decoded=self.nodes[0].decoderawtransaction(final['hex'])
        prepared=dict(raw=final['hex'],decoded=decoded,coins=coins,outputs=outputs,fee=21,phase='multisigner_psbt',construction_ms=0,signing_ms=0)
        self.submit(prepared);self.propagate([self.metrics.rows[-1]]);self.settle();self.metrics.event('native_multisigner_psbt',complete=True,inputs=2,independent_wallets=2)
    def credited_reorg(self):
        # Reversible expected ledger includes a pending independently funded deposit.
        coin=next(c for c in self.ledger.inventory('customers') if c['value']>=200)
        out=dict(**self.address('exchange'),value=100,customer='reorg-customer')
        self.submit(self.transaction('customers',[coin],[out],'deposit'));self.propagate([self.metrics.rows[-1]])
        before=copy.deepcopy(self.ledger);self.mine(2);tip_a=self.nodes[0].getbestblockhash()
        credited=copy.deepcopy(self.ledger);state_a=self.nodes[0].gettxoutsetinfo()['hash_serialized_3']
        first=self.nodes[0].getblockhash(before.height+1)
        for node in self.nodes:node.invalidateblock(first)
        self.sync_all();self.ledger=before
        assert 'reorg-customer' not in self.ledger.credits
        self.nodes[0].syncwithvalidationinterfacequeue()
        assert_equal(units(self.wallets['exchange'].getbalance()),self.ledger.balance('exchange'))
        assert_equal(self.nodes[0].getblockcount(),before.height)
        for row in self.metrics.rows:
            if row['txid'] in self.ledger.pending:row['confirmed']=False
        # Reconsider restores the exact original two-block branch and all credits.
        for node in self.nodes:node.reconsiderblock(first)
        self.sync_all();assert_equal(self.nodes[0].getbestblockhash(),tip_a)
        self.ledger=credited;assert_equal(self.nodes[0].gettxoutsetinfo()['hash_serialized_3'],state_a)
        assert_equal(self.wallets['exchange'].gettransaction(self.metrics.rows[-1]['txid'])['confirmations'],2)
        self.metrics.rows[-1]['confirmed']=True;self.settle()
        self.metrics.event('credited_deposit_reorg',depth=2,credit_removed_and_restored=True,utxo_hash_restored=True,tip=tip_a)
    def reward_consolidation(self):
        rewards=[c for c in self.ledger.inventory('miner') if c['coinbase']]
        if len(rewards)<50:
            self.metrics.event('reward_consolidation',executed=False,mature=len(rewards),reason='fewer than 50 mature coinbases in this manifest')
            return
        coins=rewards[:50];value=sum(c['value'] for c in coins)-501
        self.submit(self.transaction('miner',coins,[dict(**self.reward,value=value)],'native_reward_consolidation'))
        self.propagate([self.metrics.rows[-1]]);self.settle()
        self.metrics.event('reward_consolidation',executed=True,mature_inputs=50,witness_signatures=50,automatic=False)
    def encrypted_backup(self):
        wallet=self.wallets['exchange'];passphrase='public WO07B isolated test passphrase'
        expected=self.ledger.balance('exchange');wallet.encryptwallet(passphrase)
        coin=next(c for c in self.ledger.inventory('exchange') if c['value']>=200)
        unsigned=self.nodes[0].createrawtransaction([dict(txid=coin['txid'],vout=coin['vout'])],{self.change['exchange']['address']:mca(coin['value']-11)})
        assert_raises_rpc_error(-13,'walletpassphrase',wallet.signrawtransactionwithwallet,unsigned)
        wallet.walletpassphrase(passphrase,60);assert wallet.signrawtransactionwithwallet(unsigned)['complete']
        path=self.options.output/'encrypted-native-wallet.backup';wallet.backupwallet(str(path));wallet.walletlock()
        self.nodes[0].restorewallet('exchange-restored',str(path),False)
        restored=self.nodes[0].get_wallet_rpc('exchange-restored');self.nodes[0].syncwithvalidationinterfacequeue()
        assert_equal(units(restored.getbalance()),expected)
        restored.walletpassphrase(passphrase,30);assert restored.signrawtransactionwithwallet(unsigned)['complete'];restored.walletlock()
        self.nodes[0].unloadwallet('exchange-restored');path.unlink()
        self.restart_node(0)
        for owner in self.wallets:
            self.nodes[0].loadwallet(owner);self.wallets[owner]=self.nodes[0].get_wallet_rpc(owner)
        self.connect_nodes(0,1);self.sync_all();self.settle()
        assert_equal(self.wallets['exchange'].getwalletinfo()['unlocked_until'],0)
        self.wallets['exchange'].walletpassphrase(passphrase,30)
        assert self.wallets['exchange'].signrawtransactionwithwallet(unsigned)['complete']
        self.wallets['exchange'].walletlock()
        self.metrics.event('encrypted_backup_restore',locked_rejection=-13,wallet_node_restart=True,balance_units=expected,plaintext_private_material_exported=False)
