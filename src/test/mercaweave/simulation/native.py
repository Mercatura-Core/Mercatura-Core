#!/usr/bin/env python3
"""WO07B real native-v2 network/exchange campaign. Never constructs v3 scripts."""
import dataclasses
import hashlib
import json
import pathlib
import random
import sys
import time
from decimal import Decimal
ROOT=pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'test/functional'))
sys.path.insert(0,str(ROOT/'src/test/mercaweave'))
from simulation.core import Ledger,Manifest,Metrics,mca,units,write_json
from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import CTransaction,CTxIn,COutPoint,CTxOut,from_hex
from test_framework.util import assert_equal

class NativeSimulation(BitcoinTestFramework):
    def add_options(self,parser):
        parser.add_argument('--manifest',type=pathlib.Path,required=True)
        parser.add_argument('--output',type=pathlib.Path,required=True)
    def set_test_params(self):
        self.manifest=Manifest(**json.loads(self.options.manifest.read_text())).validate()
        self.num_nodes=self.manifest.nodes;self.setup_clean_chain=True;self.uses_wallet=True;self.rpc_timeout=240
        self.extra_args=[['-unsafesqlitesync=0','-keypool=5','-acceptnonstdtxn=0'] for _ in range(self.num_nodes)]
    def skip_test_if_missing_module(self):self.skip_if_no_wallet()
    def address(self,owner):
        address=self.wallets[owner].getnewaddress();info=self.wallets[owner].getaddressinfo(address)
        assert_equal(info['witness_version'],2)
        self.ledger.own(info['scriptPubKey'],owner);return dict(address=address,script=info['scriptPubKey'])
    def mine(self,count=1):
        results=[]
        for _ in range(count):
            start=time.monotonic();blockhash=self.generatetoaddress(self.nodes[0],1,self.reward['address'])[0]
            block=self.nodes[0].getblock(blockhash,2);fees,claim=self.ledger.block(block)
            stats=self.nodes[0].getblockstats(blockhash,['subsidy','totalfee'])
            assert_equal(fees,stats['totalfee']);assert_equal(claim,fees+stats['subsidy'])
            for node in self.nodes:assert_equal(node.getbestblockhash(),blockhash)
            event=dict(height=block['height'],bytes=block['size'],weight=block['weight'],transactions=len(block['tx'])-1,fees=fees,generate_validate_propagate_ms=(time.monotonic()-start)*1000)
            self.metrics.event('block',**event);results.append(event)
            included={t['txid'] for t in block['tx'][1:]}
            for row in self.metrics.rows:
                if row['txid'] in included:row.update(confirmed=True,height=block['height'],confirmation_ms=(time.monotonic()-row['submitted_at'])*1000)
        return results
    def settle(self):
        blocks=0
        while self.nodes[0].getrawmempool():
            self.mine();blocks+=1
            if blocks>30:raise AssertionError('bounded confirmation stalled')
        for owner,wallet in self.wallets.items():
            self.nodes[0].syncwithvalidationinterfacequeue()
            assert_equal(units(wallet.getbalance()),self.ledger.balance(owner))
        self.metrics.sample(self.nodes,self.options.tmpdir)
        self.metrics.checkpoint(height=self.ledger.height,pending=len(self.ledger.pending),credits=self.ledger.credits)
        return blocks
    def transaction(self,owner,coins,outputs,phase):
        start=time.monotonic();tx=CTransaction();tx.version=2
        tx.vin=[CTxIn(COutPoint(int(c['txid'],16),c['vout']),nSequence=0xfffffffd) for c in coins]
        # Conservative user fee above the unchanged full-byte relay minimum.
        fee=10*len(coins)+(43*len(outputs)+999)//1000
        available=sum(c['value'] for c in coins);requested=sum(o['value'] for o in outputs)
        change=available-requested-fee
        if change<0:raise ValueError('insufficient independently confirmed funds')
        outputs=list(outputs)
        if change:
            if change<2:raise ValueError('dust change')
            outputs.append(dict(**self.change[owner],value=change))
        tx.vout=[CTxOut(o['value'],bytes.fromhex(o['script'])) for o in outputs]
        construction_ms=(time.monotonic()-start)*1000;start=time.monotonic()
        signed=self.wallets[owner].signrawtransactionwithwallet(tx.serialize().hex());assert_equal(signed['complete'],True)
        signing_ms=(time.monotonic()-start)*1000;decoded=self.nodes[0].decoderawtransaction(signed['hex'])
        actual=from_hex(CTransaction(),signed['hex']);assert_equal(actual.txid_hex,decoded['txid'])
        assert_equal(actual.wtxid_hex,decoded['hash']);assert decoded['weight']<=400000
        for witness in actual.wit.vtxinwit:
            assert_equal([len(item) for item in witness.scriptWitness.stack],[3309,1952])
        assert fee>=(decoded['size']+999)//1000
        return dict(raw=signed['hex'],decoded=decoded,coins=coins,outputs=outputs,fee=fee,phase=phase,construction_ms=construction_ms,signing_ms=signing_ms)
    def submit(self,prepared):
        tx=prepared['decoded'];start=time.monotonic();txid=self.nodes[0].sendrawtransaction(prepared['raw']);assert_equal(txid,tx['txid'])
        self.ledger.register(txid,prepared['coins'],prepared['outputs'],prepared['fee'],prepared['phase'])
        row=dict(txid=txid,wtxid=tx['hash'],bytes=tx['size'],weight=tx['weight'],inputs=len(tx['vin']),outputs=len(tx['vout']),native_signatures=len(tx['vin']),groups=0,fee_units=prepared['fee'],phase=prepared['phase'],construction_ms=prepared['construction_ms'],signing_ms=prepared['signing_ms'],admission_ms=(time.monotonic()-start)*1000,submitted_at=start,accepted=True,confirmed=False)
        self.metrics.rows.append(row);return row
    def propagate(self,rows):
        remaining={r['txid']:r for r in rows};deadline=time.monotonic()+90
        while remaining:
            seen=set.intersection(*(set(n.getrawmempool()) for n in self.nodes))
            for txid in list(remaining):
                if txid in seen:
                    row=remaining.pop(txid);row['propagation_ms']=(time.monotonic()-row['submitted_at'])*1000
            if not remaining:break
            if time.monotonic()>deadline:raise AssertionError('mempool convergence deadline')
            time.sleep(.05)
        # Record the aggregate last-node convergence observation, not submission success.
        now=time.monotonic()
        for row in rows:row['propagation_upper_ms']=(now-row['submitted_at'])*1000
    def fund(self,count):
        coins=self.ledger.inventory('miner');needed=count*500
        while sum(c['value'] for c in coins)<needed+100:
            if self.ledger.height>400:raise ValueError('bounded funding maturity budget exhausted')
            self.mine(10);coins=self.ledger.inventory('miner')
        selected=[]
        for coin in coins:
            selected.append(coin)
            if sum(c['value'] for c in selected)>=needed+10*len(selected)+100:break
        # Unique output indices permit repeated keys without duplicate-address RPC shortcuts.
        outputs=[dict(**self.change['customers'],value=500) for _ in range(count)]
        self.submit(self.transaction('miner',selected,outputs,'funding'));self.settle()
        inventory=self.ledger.inventory('customers')
        return [c for c in inventory if c['value']==500][-count:]
    def epoch(self,count):
        funding=self.fund(count);self.rng.shuffle(funding);rows=[]
        capital=20*count+500
        treasury=self.ledger.inventory('miner');chosen=[]
        for coin in treasury:
            chosen.append(coin)
            if sum(c['value'] for c in chosen)>capital+10*len(chosen)+2:break
        self.submit(self.transaction('miner',chosen,[dict(**self.change['exchange'],value=capital)],'hot_wallet_funding'));self.settle()
        for index,coin in enumerate(funding):
            recipient=self.deposit_shared if self.manifest.custody=='reused' else self.address('exchange')
            out=dict(**recipient,value=200,customer=str(len(self.metrics.rows)))
            rows.append(self.submit(self.transaction('customers',[coin],[out],'deposit')))
            if self.manifest.mining_interval and len(rows)%self.manifest.mining_interval==0:self.propagate(rows);self.settle();rows=[]
        if rows:self.propagate(rows)
        self.settle()
        expected_credit=sum(self.ledger.credits.values());assert_equal(expected_credit,200*sum(r['phase']=='deposit' for r in self.metrics.rows))
        if self.manifest.custody=='centralized':
            deposits=[c for c in self.ledger.inventory('exchange') if c['value']==200]
            for offset in range(0,len(deposits),64):
                chosen=deposits[offset:offset+64]
                total=sum(c['value'] for c in chosen)-10*len(chosen)-1
                self.submit(self.transaction('exchange',chosen,[dict(**self.change['exchange'],value=total)],'custody_sweep'))
            self.propagate([r for r in self.metrics.rows if not r['confirmed']]);self.settle()
        inventory=self.ledger.inventory('exchange')
        if self.manifest.custody!='centralized':inventory=[c for c in inventory if c['value']==200]
        if self.manifest.custody=='centralized':inventory.sort(key=lambda c:c['script']!=self.change['exchange']['script'])
        # Separate versus batched withdrawals are genuine transactions; account debits are off-chain.
        withdrawals=count//2;done=0
        while done<withdrawals:
            size=min(self.manifest.batch_size,withdrawals-done)
            outputs=[dict(**self.address('customers'),value=100) for _ in range(size)]
            available_accounts=[k for k,v in self.ledger.credits.items() if v-self.ledger.debits.get(k,0)>=100]
            assert len(available_accounts)>=size
            for account in available_accounts[:size]:self.ledger.debits[account]=self.ledger.debits.get(account,0)+100
            # One deposit UTXO can fund one withdrawal; batching combines independent keys.
            selected=[]
            while inventory and sum(c['value'] for c in selected)<100*size+10*len(selected)+(43*size+999)//1000:
                selected.append(inventory.pop(0))
            self.submit(self.transaction('exchange',selected,outputs,'withdrawal'));done+=size
        pending=[r for r in self.metrics.rows if not r['confirmed']];self.propagate(pending);self.settle()
        assert self.ledger.balance('exchange')>=expected_credit-sum(self.ledger.debits.values())
        self.metrics.event('accounting',credited_units=expected_credit,withdrawn_units=sum(self.ledger.debits.values()),offchain_liabilities=expected_credit-sum(self.ledger.debits.values()),offchain_transfers=0)
    def network(self):
        self.disconnect_nodes(0,1);self.metrics.event('partition',edge=[0,1])
        # No transactions during split: this isolates block catch-up from mempool relay.
        hashes=self.generatetoaddress(self.nodes[0],2,self.reward['address'],sync_fun=self.no_op)
        for h in hashes:
            block=self.nodes[0].getblock(h,2);fees,claim=self.ledger.block(block);assert_equal(fees,0)
        assert self.nodes[-1].getblockcount()<self.nodes[0].getblockcount()
        start=time.monotonic();self.connect_nodes(0,1);self.sync_blocks();self.metrics.event('reconnect',catchup_ms=(time.monotonic()-start)*1000)
        self.restart_node(self.num_nodes-1);self.connect_nodes(self.num_nodes-2,self.num_nodes-1);self.sync_all()
        self.metrics.event('restart',tip=self.nodes[0].getbestblockhash());self.settle()
    def run_test(self):
        self.metrics=Metrics(self.options.output);self.ledger=Ledger();self.wallets={};self.rng=random.Random(self.manifest.seed)
        write_json(self.options.output/'manifest.json',dataclasses.asdict(self.manifest))
        write_json(self.options.output/'source-hashes.json',{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in pathlib.Path(__file__).parent.glob('*.py')})
        for owner in ('miner','customers','exchange'):
            self.nodes[0].createwallet(owner);self.wallets[owner]=self.nodes[0].get_wallet_rpc(owner)
        self.change={owner:self.address(owner) for owner in self.wallets};self.reward=self.change['miner'];self.deposit_shared=self.address('exchange')
        self.metrics.event('isolation',chain=self.nodes[0].getblockchaininfo()['chain'],nodes=self.num_nodes,transport='loopback-native-v2',standard_policy=True,synchronous_sqlite=True)
        assert_equal(self.nodes[0].getblockchaininfo()['chain'],'regtest')
        self.mine(101);completed=0
        while completed<self.manifest.transactions:
            count=min(self.manifest.epoch_size,self.manifest.transactions-completed);self.epoch(count);completed+=count
            self.metrics.checkpoint(completed_deposits=completed,height=self.ledger.height,credits=self.ledger.credits)
        if self.manifest.disruptions:self.network()
        stop=time.monotonic()+self.manifest.soak_seconds;cycles=0;start=time.monotonic()
        while time.monotonic()<stop and cycles<self.manifest.max_soak_cycles:
            self.epoch(min(10,self.manifest.epoch_size));self.network();cycles+=1
        self.metrics.event('soak',requested_seconds=self.manifest.soak_seconds,actual_seconds=time.monotonic()-start,cycles=cycles)
        self.metrics.checkpoint(completed_deposits=completed,height=self.ledger.height,credits=self.ledger.credits)
        self.log.info('WO07B_NATIVE_SIMULATION_PASS')

if __name__=='__main__':NativeSimulation(__file__).main()
