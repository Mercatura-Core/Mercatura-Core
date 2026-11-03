"""Independent accounting and manifest bounds, without nodes or network."""
import unittest
from simulation.core import Ledger,Manifest,units

class Accounting(unittest.TestCase):
    def test_money_and_bounds(self):
        self.assertEqual(units('1.23'),123)
        for value in ['1.001','0.009']:
            with self.assertRaises(ValueError):units(value)
        for changed in [dict(nodes=1),dict(transactions=100001),dict(transactions=50000),dict(batch_size=65),dict(soak_seconds=-1),dict(seed=-1),dict(nodes=2.5),dict(bootstrap_blocks=100)]:
            with self.assertRaises(ValueError):Manifest(**changed).validate()
        self.assertEqual(Manifest(transactions=50000,extended=True).validate().transactions,50000)
    def test_confirmation_credit_maturity_and_duplicate(self):
        ledger=Ledger();ledger.own('52','miner');ledger.own('53','exchange')
        ledger.block(dict(height=1,tx=[dict(txid='coin',vout=[dict(n=0,value='5.00',scriptPubKey=dict(hex='52'))])]))
        self.assertEqual(ledger.inventory('miner'),[]);ledger.height=101
        self.assertEqual(ledger.balance('miner'),500)
        ledger.register('deposit',[dict(txid='coin',vout=0)],[dict(value=490,script='53',customer='alice')],10,'deposit')
        self.assertEqual(ledger.credits,{})
        with self.assertRaises(AssertionError):ledger.register('duplicate',[dict(txid='coin',vout=0)],[dict(value=490)],10,'deposit')
        ledger.block(dict(height=102,tx=[dict(txid='cb',vout=[]),dict(txid='deposit',vin=[dict(txid='coin',vout=0)],vout=[dict(n=0,value='4.90',scriptPubKey=dict(hex='53'))])]))
        self.assertEqual(ledger.credits,dict(alice=490));self.assertEqual(ledger.balance('exchange'),490);self.assertEqual(ledger.total_fees,10)
        self.assertNotIn(('coin',0),ledger.coins)

if __name__=='__main__':unittest.main()
