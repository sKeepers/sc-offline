"""Codex independent server regressions; no native game calls or incoming tests."""
from contextlib import closing
from pathlib import Path
import tempfile, unittest, concurrent.futures, json
from server import Store, Problem
import test_server
import acquisition as acq

class PurchaseTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.store=Store(Path(self.temp.name)/'test.sqlite3')
        self.store.mutate('/wallet/adjust',{'delta':1000,'reason':'test fixture'},'fund')
        self.store.mutate('/shops/source',{'id':'fixture','kind':'owner_declared','authoritative':True,'label':'Test local prices, not native'},'source')
        self.offer(10,2)
        self.body={'shop_id':'local-shop','item_class':'Test_Item','quantity':1,'quoted_unit_price':10,'expected_offer_version':1,'expected_credits':1000,'location_id':'offline-home'}
    def tearDown(self):self.temp.cleanup()
    def offer(self,price,stock):
        self.store.mutate('/shops/offer',{'source_id':'fixture','shop_id':'local-shop','item_class':'Test_Item','unit_price':price,'stock':stock},'offer')
    def test_retry_debits_once(self):
        a=self.store.mutate('/shops/purchase',self.body,'buy');b=self.store.mutate('/shops/purchase',self.body,'buy')
        self.assertEqual(a,b);self.assertEqual(self.store.snapshot()['profile']['credits'],990)
        self.assertEqual(self.store.shops()['holdings'][0]['quantity'],1)
        self.assertEqual(a['native']['delivery_state'],'not_requested')
    def test_stale_wallet_rolls_back(self):
        self.store.mutate('/wallet/adjust',{'delta':1,'reason':'test conflict'},'change')
        with self.assertRaises(Problem):self.store.mutate('/shops/purchase',self.body,'buy')
        self.assertEqual(self.store.shops()['offers'][0]['stock'],2);self.assertFalse(self.store.shops()['holdings'])
    def test_no_client_identity(self):
        with self.assertRaises(Problem):self.store.mutate('/shops/purchase',dict(self.body,owner='another'),'buy')
        self.assertEqual(self.store.snapshot()['profile']['credits'],1000)
    def test_unknown_price(self):
        self.store.mutate('/shops/offer',{'source_id':'fixture','shop_id':'local-shop','item_class':'Test_Item','unit_price':None,'stock':2,'expected_version':1},'unknown')
        with self.assertRaises(Problem):self.store.mutate('/shops/purchase',dict(self.body,expected_offer_version=2),'buy')
    def test_wrong_location(self):
        with self.assertRaises(Problem):self.store.mutate('/shops/purchase',dict(self.body,location_id='missing'),'buy')
        self.assertFalse(self.store.shops()['purchases'])
    def test_reused_key_changes_body_refused(self):
        self.store.mutate('/shops/purchase',self.body,'buy')
        with self.assertRaises(Problem):self.store.mutate('/shops/purchase',dict(self.body,quantity=2),'buy')
    def test_concurrent_last_stock(self):
        self.store.mutate('/shops/offer',{'source_id':'fixture','shop_id':'local-shop','item_class':'Test_Item','unit_price':10,'stock':1,'expected_version':1},'last')
        body=dict(self.body,expected_offer_version=2)
        def buy(key):
            try:self.store.mutate('/shops/purchase',body,key);return True
            except Problem:return False
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:out=list(pool.map(buy,['a','b']))
        self.assertEqual(sum(out),1);self.assertEqual(self.store.snapshot()['profile']['credits'],990)
    def test_no_retry_after_dispatch(self):
        receipt=self.store.mutate('/shops/purchase',self.body,'buy')
        with closing(self.store.connect()) as db:
            db.execute('BEGIN IMMEDIATE');p=receipt['purchase_id']
            acq.request_delivery(db,p,'pocket',1);acq.begin_dispatch(db,p,2);db.execute('COMMIT')
            db.execute('BEGIN IMMEDIATE')
            with self.assertRaises(acq.AcquisitionError):acq.mark_not_sent(db,p,3,'contract_unavailable')
            with self.assertRaises(acq.AcquisitionError):acq.request_delivery(db,p,'pocket',3)
            db.execute('ROLLBACK')
    def test_failure_late_in_transaction_rolls_back_debit(self):
        with closing(self.store.connect()) as db:
            db.execute("CREATE TRIGGER refuse_receipt BEFORE INSERT ON acq_purchase BEGIN SELECT RAISE(ABORT,'test failure'); END")
        with self.assertRaises(Exception):self.store.mutate('/shops/purchase',self.body,'buy')
        self.assertEqual(self.store.snapshot()['profile']['credits'],1000)
        self.assertEqual(self.store.shops()['offers'][0]['stock'],2)

class ShopHttpTests(test_server.HttpTests):
    def test_shop_endpoint_auth(self):
        self.assertEqual(self.request('/api/v1/shops',headers={'Authorization':''})[0],401)
        status,raw,_=self.request('/api/v1/shops');self.assertEqual(status,200)
        self.assertFalse(json.loads(raw)['physical_delivery'])
    def test_native_delivery_endpoint_absent(self):
        self.assertEqual(self.request('/api/v1/shops/delivery','POST','{}')[0],404)

if __name__=='__main__':unittest.main()
