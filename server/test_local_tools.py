"""Codex independent regressions for imported catalog, planners and observations."""
from contextlib import closing
from pathlib import Path
import json,tempfile,unittest,time,hashlib,threading,http.client
from decimal import Decimal
from server import Store,Server,Problem
from game_catalog import GameCatalog,canonical
from log_observer import LogObserver,parse_line
import operations

def fixture(folder):
    folder.mkdir();(folder/'sources').mkdir()
    record={'ID':{'ID':['test-guid']},'BuyPrice':10.5,'SellPrice':5}
    raw=json.dumps({'Inventory':[record]}).encode();(folder/'sources/fixture-shop.json').write_bytes(raw)
    offer={'shop_id':'fixture-shop','guid':'test-guid','item_class':'Test_Item','kind':'item','buy_auec':'10.5','sell_auec':'5',
           'stock':None,'stock_basis':'unknown','source_file':'sources/fixture-shop.json','record_index':0,
           'record_sha256':canonical(json.loads(raw,parse_float=Decimal)['Inventory'][0])}
    doc={'format':'sc-local-game-catalog-1','game_manifest':{'Data':{'Branch':'fixture'}},'captured_at':'fixture','unresolved':0,
         'files':[{'path':'sources/fixture-shop.json','size':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}],'offers':[offer]}
    mapping=json.dumps({'test-guid':{'item_class':'Test_Item','kind':'item'}}).encode()
    (folder/'class-map.json').write_bytes(mapping);doc['class_map_file']='class-map.json';doc['class_map_sha256']=hashlib.sha256(mapping).hexdigest()
    (folder/'catalog.json').write_text(json.dumps(doc));return doc

class CatalogTests(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.folder=Path(self.tmp.name)/'game-catalog';self.doc=fixture(self.folder)
    def tearDown(self):self.tmp.cleanup()
    def rewrite(self): (self.folder/'catalog.json').write_text(json.dumps(self.doc))
    def test_own_source_and_fraction_preserved(self):
        c=GameCatalog(self.folder);self.assertEqual(c.snapshot()['offers'][0]['buy_auec'],'10.5')
        with self.assertRaises(ValueError):c.local_offer('fixture-shop','Test_Item')
        self.assertEqual(c.local_offer('fixture-shop','Test_Item','ceil')[1],11)
    def test_source_tampering_fail_closed(self):
        (self.folder/'sources/fixture-shop.json').write_text('{}');self.assertEqual(GameCatalog(self.folder).snapshot()['status'],'unavailable')
    def test_offer_tampering_fail_closed(self):
        self.doc['offers'][0]['buy_auec']='11';self.rewrite();self.assertEqual(GameCatalog(self.folder).snapshot()['status'],'unavailable')
    def test_static_max_not_live_stock(self):
        self.doc['offers'][0]['stock']=10;self.rewrite();self.assertEqual(GameCatalog(self.folder).snapshot()['status'],'unavailable')
    def test_path_escape_rejected(self):
        self.doc['files'][0]['path']='../outside';self.rewrite();self.assertEqual(GameCatalog(self.folder).snapshot()['status'],'unavailable')
    def test_malformed_class_fail_closed(self):
        self.doc['offers'][0]['item_class']=[];self.rewrite();self.assertEqual(GameCatalog(self.folder).snapshot()['status'],'unavailable')
    def test_valid_wrong_class_binding_refused(self):
        self.doc['offers'][0]['item_class']='Other_Valid_Class';self.rewrite();self.assertEqual(GameCatalog(self.folder).snapshot()['status'],'unavailable')

class PlannerTests(unittest.TestCase):
    def point(self,name,x,frame='local'):return {'name':name,'frame':frame,'x':x,'y':0,'z':0}
    def test_route_uses_first_start_and_nearest_next(self):
        r=operations.route([self.point('start',0),self.point('far',30),self.point('near',10)],32,20)
        self.assertEqual([p['name'] for p in r['points']],['start','near','far']);self.assertEqual(r['distance_m'],30)
    def test_mixed_coordinate_frames_refused(self):
        with self.assertRaises(ValueError):operations.route([self.point('a',0),self.point('b',1,'other')])
    def test_over_capacity_refused(self):
        with self.assertRaises(ValueError):operations.route([self.point('a',0),self.point('b',1)],10,20)
    def test_nonfinite_and_bool_refused(self):
        for v in [True,float('nan'),float('inf'),10**400]:
            with self.assertRaises(ValueError):operations.route([self.point('a',0),self.point('b',v)])
    def test_job_survives_restart_without_native_claim(self):
        with tempfile.TemporaryDirectory() as d:
            s=Store(Path(d)/'state.db');body={'name':'My lot','resource':'Gold','quantity':4,'duration_minutes':1}
            a=s.mutate('/tools/refining/create',body,'job');self.assertEqual(a,s.mutate('/tools/refining/create',body,'job'))
            s=Store(s.path)
            with closing(s.connect())as db:r=operations.snapshot(db)['entries'][0]
            self.assertFalse(r['game_confirmed']);self.assertEqual(r['quantity'],4)
            with self.assertRaises(Problem):s.mutate('/tools/refining/complete',{'id':r['id'],'expected_version':2},'bad')
            done=s.mutate('/tools/refining/complete',{'id':r['id'],'expected_version':1},'complete')
            self.assertEqual(done['status'],'user_confirmed')

class ObserverTests(unittest.TestCase):
    def test_old_crash_ignored_and_restart_deduplicates(self):
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/'Game.log';cp=Path(d)/'cursor.json';f.write_text('EXCEPTION_ACCESS_VIOLATION\n')
            o=LogObserver([f],cp);o.poll();self.assertEqual(o.snapshot()['events'],[])
            with f.open('a')as out:out.write('Loading level megamap\nEXCEPTION_ACCESS_VIOLATION\n')
            o.poll();self.assertEqual(len(o.snapshot()['events']),2)
            o=LogObserver([f],cp);o.poll();self.assertEqual(len(o.snapshot()['events']),2)
            self.assertFalse(o.snapshot()['events'][0]['confirmed_crash'])
    def test_rotation_and_partial_line(self):
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/'Game.log';f.write_text('old\n');o=LogObserver([f],Path(d)/'cursor.json');o.poll()
            f.write_text('Log started on fixture\nLoading level x')
            o.poll();self.assertEqual(len(o.snapshot()['events']),1)
            with f.open('a')as out:out.write('\n')
            o.poll();self.assertEqual(len(o.snapshot()['events']),2)
    def test_patterns_not_delivery_success(self):
        self.assertIsNone(parse_line('command engines_on dispatched successfully'))
        self.assertFalse(parse_line('ASOP delivery unconfirmed')['confirmed_delivery'])

class ShopFixTests(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.store=Store(Path(self.tmp.name)/'db')
    def tearDown(self):self.tmp.cleanup()
    def test_quote_is_read_only_and_fresh(self):
        s=self.store;s.mutate('/shops/source',{'id':'local','kind':'owner_declared','authoritative':True,'label':'local'},'src')
        body={'source_id':'local','shop_id':'shop','item_class':'Test_Item','unit_price':10,'stock':2}
        s.mutate('/shops/offer',body,'off');count=len(s.snapshot()['events'])
        self.assertEqual(s.mutate('/shops/quote',{'shop_id':'shop','item_class':'Test_Item'},'quote')['unit_price'],10)
        s.mutate('/shops/offer',dict(body,unit_price=20,expected_version=1),'upd')
        self.assertEqual(s.mutate('/shops/quote',{'shop_id':'shop','item_class':'Test_Item'},'quote')['unit_price'],20)
        self.assertEqual(len(s.snapshot()['events']),count+1)
    def test_community_prices_not_shown(self):
        s=self.store;s.mutate('/shops/source',{'id':'other','kind':'imported_file','authoritative':False,'label':'external'},'src')
        s.mutate('/shops/offer',{'source_id':'other','shop_id':'shop','item_class':'Test_Item','unit_price':10,'stock':2},'off')
        o=s.shops()['offers'][0];self.assertIsNone(o['unit_price']);self.assertIsNone(o['stock']);self.assertEqual(o['status'],'not_authoritative')
    def test_ship_purchase_adds_once_to_owned_fleet(self):
        class FixtureCatalog:
            lookup={('fixture','Test_Ship'):{'item_class':'Test_Ship','kind':'ship'}}
        s=self.store;s.game_catalog=FixtureCatalog()
        s.mutate('/wallet/adjust',{'delta':100,'reason':'fixture'},'fund')
        s.mutate('/shops/source',{'id':'local','kind':'owner_declared','authoritative':True,'label':'local'},'src')
        s.mutate('/shops/offer',{'source_id':'local','shop_id':'shop','item_class':'Test_Ship','unit_price':10,'stock':2},'off')
        body={'shop_id':'shop','item_class':'Test_Ship','quantity':1,'quoted_unit_price':10,'expected_offer_version':1,'expected_credits':100,'location_id':'offline-home'}
        a=s.mutate('/shops/purchase',body,'buy');b=s.mutate('/shops/purchase',body,'buy')
        self.assertEqual(a,b);self.assertEqual(len(s.snapshot()['ships']),1);self.assertEqual(a['owned_fleet_added'],1)
        self.assertEqual(s.shops()['purchases'][0],a)
    def test_invalid_json_gets_400(self):
        server=Server(('127.0.0.1',0),self.store,'a'*64,'test');t=threading.Thread(target=server.serve_forever);t.start()
        try:
            for raw in ['{"x":1e400}','{"x":'+('['*2000)+'0'+(']'*2000)+'}']:
                c=http.client.HTTPConnection(*server.server_address);c.request('POST','/api/v1/shops/purchase',raw,{'Authorization':'Bearer '+'a'*64,'Content-Type':'application/json','Idempotency-Key':'bad'})
                r=c.getresponse();r.read();self.assertEqual(r.status,400);c.close()
        finally:server.shutdown();t.join();server.server_close()

if __name__=='__main__':unittest.main()
