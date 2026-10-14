"""Codex independent 0.3.1 acceptance: synthetic files/SQLite only, no incoming tests."""
from pathlib import Path
from contextlib import closing
from unittest.mock import patch
import copy,hashlib,json,os,sqlite3,tempfile,unittest
import game_catalog,log_observer,operations
from server import Store,Problem
from test_local_tools import fixture

class CatalogPurchaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.folder=Path(self.tmp.name);self.cat=self.folder/'game-catalog'
        self.doc=fixture(self.cat);self.doc['offers'][0]['kind']='ship'
        mapping={'test-guid':{'item_class':'Test_Item','kind':'ship'}}
        raw=json.dumps(mapping).encode();(self.cat/'class-map.json').write_bytes(raw)
        self.doc['class_map_sha256']=hashlib.sha256(raw).hexdigest();self.write()
        self.s=Store(self.folder/'db');self.s.mutate('/wallet/adjust',{'delta':100,'reason':'synthetic'},'fund')
        self.offer={'shop_id':'fixture-shop','item_class':'Test_Item','stock':5,'use_base_price':True,'rounding':'ceil'}
        self.s.mutate('/shops/catalog-offer',self.offer,'offer')
        row=self.s.shops()['offers'][0]
        self.buy={'shop_id':row['shop_id'],'item_class':'Test_Item','quantity':1,'quoted_unit_price':11,
                  'expected_offer_version':1,'expected_credits':100,'location_id':'offline-home'}
    def tearDown(self):self.tmp.cleanup()
    def write(self): (self.cat/'catalog.json').write_text(json.dumps(self.doc),encoding='utf-8')
    def test_missing_catalog_purchase_refuses_without_debit(self):
        (self.cat/'catalog.json').rename(self.cat/'catalog.previous');s=Store(self.s.path)
        before=s.snapshot()
        with self.assertRaises(Problem)as e:s.mutate('/shops/purchase',self.buy,'buy')
        self.assertEqual(e.exception.status,409);self.assertEqual(s.snapshot(),before)
        self.assertFalse(s.shops()['purchases']);self.assertFalse(s.shops()['holdings'])
    def test_changed_catalog_requires_reconfirmation_then_adds_ship(self):
        self.doc['captured_at']='fixture-new';self.write();s=Store(self.s.path)
        with self.assertRaises(Problem):s.mutate('/shops/purchase',self.buy,'buy')
        self.assertEqual(s.snapshot()['profile']['credits'],100)
        s.mutate('/shops/catalog-offer',dict(self.offer,expected_version=1),'reconfirm')
        receipt=s.mutate('/shops/purchase',dict(self.buy,expected_offer_version=2),'buy')
        self.assertEqual(receipt['owned_fleet_added'],1);self.assertEqual(len(s.snapshot()['ships']),1)
        self.assertEqual(s.snapshot()['profile']['credits'],89)
    def test_successful_retry_after_catalog_changed_is_not_charged_again(self):
        receipt=self.s.mutate('/shops/purchase',self.buy,'buy')
        self.doc['captured_at']='fixture-new';self.write();s=Store(self.s.path)
        self.assertEqual(s.mutate('/shops/purchase',self.buy,'buy'),receipt)
        self.assertEqual(len(s.snapshot()['ships']),1);self.assertEqual(s.snapshot()['profile']['credits'],89)
    def fill_fleet(self,count):
        with closing(self.s.connect()) as db:
            db.executemany("INSERT INTO ships VALUES(?,?,?,'stored','offline-home',NULL,1)",[(f'synthetic-{i}','Test_Item','fixture') for i in range(count)])
    def test_manual_add_cannot_break_bridge_limit(self):
        self.fill_fleet(256)
        with self.assertRaises(Problem)as e:self.s.mutate('/ships',{'class_name':'Test_Item','name':'extra','location_id':'offline-home'},'extra')
        self.assertEqual(e.exception.status,409);self.assertEqual(len(self.s.snapshot()['ships']),256)
    def test_two_ship_purchase_rolls_back_at_limit(self):
        self.fill_fleet(255);before=self.s.snapshot()
        with self.assertRaises(Problem):self.s.mutate('/shops/purchase',dict(self.buy,quantity=2),'buy')
        self.assertEqual(self.s.snapshot(),before);self.assertFalse(self.s.shops()['purchases'])
        self.assertEqual(self.s.shops()['offers'][0]['stock'],5)
    def test_two_ship_purchase_reaches_limit_exactly(self):
        self.fill_fleet(254);r=self.s.mutate('/shops/purchase',dict(self.buy,quantity=2),'buy')
        self.assertEqual(r['owned_fleet_added'],2);self.assertEqual(len(self.s.snapshot()['ships']),256)
    def test_partial_or_nonfinite_catalog_is_unavailable(self):
        original=copy.deepcopy(self.doc)
        for field in ['game_manifest','captured_at','unresolved']:
            self.doc=copy.deepcopy(original);del self.doc[field];self.write()
            self.assertEqual(game_catalog.GameCatalog(self.cat).snapshot()['status'],'unavailable')
        self.doc=original;self.doc['game_manifest']={'bad':float('inf')};self.write()
        raw=(self.cat/'catalog.json').read_text().replace('Infinity','1e400');(self.cat/'catalog.json').write_text(raw)
        self.assertEqual(game_catalog.GameCatalog(self.cat).snapshot()['status'],'unavailable')
    def test_duplicate_map_key_not_silently_replaced(self):
        raw=b'{"test-guid":{"item_class":"Test_Item","kind":"ship"},"test-guid":{"item_class":"Test_Item","kind":"ship"}}'
        (self.cat/'class-map.json').write_bytes(raw);self.doc['class_map_sha256']=hashlib.sha256(raw).hexdigest();self.write()
        self.assertEqual(game_catalog.GameCatalog(self.cat).snapshot()['status'],'unavailable')

class ObserverSafetyTests(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.log=self.root/'synthetic.log';self.cp=self.root/'cursor.json'
    def tearDown(self):self.tmp.cleanup()
    @unittest.skipUnless(os.name=='nt','Windows sharing contract')
    def test_open_reader_allows_rotation_and_removal(self):
        self.log.write_bytes(b'fixture\n')
        with log_observer._open_shared(self.log)as f:
            rotated=self.log.with_suffix('.rotated');self.log.rename(rotated);rotated.unlink()
            self.assertEqual(f.read(),b'fixture\n')
    def test_new_log_after_monitor_start_is_observed(self):
        o=log_observer.LogObserver([self.log],self.cp);o.poll();self.log.write_text('Log started on synthetic\n')
        o.poll();self.assertEqual([e['kind'] for e in o.snapshot()['events']],['session_log_started'])
    def test_bad_checkpoint_and_unhashable_kind_never_break_start(self):
        for value in [[],{'cursors':[]},{'events':[{'kind':[]},{'kind':{}}]}, {'cursors':{str(self.log):{'offset':0,'inode':0,'span':10**9,'prefix':'0'*64}}}]:
            self.cp.write_text(json.dumps(value));o=log_observer.LogObserver([self.log],self.cp);o.poll();self.assertEqual(o.snapshot()['events'],[])
    def test_long_line_tail_is_not_crash_evidence(self):
        self.log.write_bytes(b'old\n');o=log_observer.LogObserver([self.log],self.cp);o.poll()
        with self.log.open('ab')as f:f.write(b'x'*(log_observer.READ_LIMIT+20)+b'EXCEPTION_ACCESS_VIOLATION\nLog started on fixture\n')
        o.poll();o.poll();self.assertEqual([e['kind'] for e in o.snapshot()['events']],['session_log_started'])
    def test_non_fatal_error_is_not_crash(self):
        for text in ['non-fatal error','Non fatal error']:
            self.assertIsNone(log_observer.parse_line(text))
        self.assertEqual(log_observer.parse_line('Fatal Error')['kind'],'crash_signal')
    def test_rotation_during_open_does_not_duplicate_new_file(self):
        self.log.write_text('old\n');o=log_observer.LogObserver([self.log],self.cp);o.poll()
        real_open=log_observer._open_shared
        def replace_then_open(path):
            self.log.rename(self.log.with_suffix('.previous'));self.log.write_text('Log started on new\n');return real_open(path)
        with patch.object(log_observer,'_open_shared',side_effect=replace_then_open):o.poll()
        o.poll();self.assertEqual(len(o.snapshot()['events']),1)

class RefiningObservationTests(unittest.TestCase):
    def test_repeat_confirmation_keeps_first_timestamp(self):
        with tempfile.TemporaryDirectory()as t:
            s=Store(Path(t)/'db');r=s.mutate('/tools/refining/create',{'name':'lot','resource':'synthetic','quantity':1,'duration_minutes':1},'new')
            done=s.mutate('/tools/refining/complete',{'id':r['id'],'expected_version':1},'confirm')
            with self.assertRaises(Problem):s.mutate('/tools/refining/complete',{'id':r['id'],'expected_version':2},'confirm-again')
            with closing(s.connect())as db:row=operations.snapshot(db)['entries'][0]
            self.assertEqual(row['confirmed_at'],done['confirmed_at']);self.assertEqual(row['version'],2);self.assertFalse(row['game_confirmed'])

if __name__=='__main__':unittest.main()
