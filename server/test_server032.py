"""Independent Codex regressions, synthetic logs/catalog/SQLite only."""
from pathlib import Path
from unittest.mock import patch
import hashlib,json,tempfile,unittest
import game_catalog,log_observer
from server import Store
import test_server031
from test_local_tools import fixture

class ObserverRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.log=self.root/'synthetic.log';self.cp=self.root/'observer.json'
        self.log.write_bytes(b'previous session\n')
    def tearDown(self):self.tmp.cleanup()
    def test_failed_initial_save_does_not_prevent_start(self):
        observer=log_observer.LogObserver([self.log],self.cp)
        try:
            with patch.object(Path,'replace',side_effect=PermissionError('synthetic lock')):
                observer.start()
                self.assertTrue(observer.worker.is_alive())
                self.assertTrue(observer.unsaved)
                self.assertTrue(any(s['file']=='monitor' for s in observer.snapshot()['sources']))
        finally:observer.close()
    def test_failed_event_save_retries_without_new_input_and_survives_restart(self):
        observer=log_observer.LogObserver([self.log],self.cp);observer.poll()
        with self.log.open('ab')as f:f.write(b'Log started on synthetic\n')
        with patch.object(Path,'replace',side_effect=PermissionError('synthetic lock')):
            observer._guarded_poll()
        self.assertTrue(observer.unsaved);self.assertEqual(len(observer.snapshot()['events']),1)
        observer._guarded_poll()
        self.assertFalse(observer.unsaved)
        self.assertFalse(any(s['file']=='monitor' for s in observer.snapshot()['sources']))
        restarted=log_observer.LogObserver([self.log],self.cp);restarted.poll()
        self.assertEqual(len(restarted.snapshot()['events']),1)
        self.assertEqual(json.loads(self.cp.read_text())['cursors'],observer.cursors)
    def test_nonfatal_spellings_do_not_emit_crash_but_fatal_does(self):
        for line in ['nonfatal error','NonFatal Error','non_fatal error','non-fatal error','non fatal error']:
            with self.subTest(line=line):self.assertIsNone(log_observer.parse_line(line))
        for line in ['Fatal Error','EXCEPTION_ACCESS_VIOLATION']:
            self.assertEqual(log_observer.parse_line(line)['kind'],'crash_signal')

class OfferStatusTests(unittest.TestCase):
    setUp=test_server031.CatalogPurchaseTests.setUp
    tearDown=test_server031.CatalogPurchaseTests.tearDown
    write=test_server031.CatalogPurchaseTests.write
    def test_stale_quote_and_list_are_disabled_without_database_mutation(self):
        self.doc['captured_at']='changed';self.write();store=Store(self.s.path)
        before=store.snapshot()
        self.assertEqual(store.shops()['offers'][0]['status'],'catalog_changed')
        self.assertEqual(store.mutate('/shops/quote',{'shop_id':self.buy['shop_id'],'item_class':'Test_Item'},'quote')['status'],'catalog_changed')
        self.assertEqual(store.snapshot(),before)
        store.mutate('/shops/catalog-offer',dict(self.offer,expected_version=1),'confirm')
        self.assertEqual(store.shops()['offers'][0]['status'],'available')
    def test_missing_catalog_marks_old_offer_unavailable(self):
        (self.cat/'catalog.json').unlink();store=Store(self.s.path)
        self.assertEqual(store.shops()['offers'][0]['status'],'catalog_changed')

class CatalogRecordTests(unittest.TestCase):
    def test_resolved_path_required_but_unresolved_null_is_valid(self):
        with tempfile.TemporaryDirectory()as td:
            folder=Path(td)/'catalog';doc=fixture(folder)
            doc['files'][0]['archive_entry']='Data/fixture.json'
            mapping={'test-guid':{'item_class':'Test_Item','kind':'item','record_path':'Data/item.xml'}}
            raw=json.dumps(mapping).encode();(folder/'class-map.json').write_bytes(raw)
            doc['class_map_sha256']=hashlib.sha256(raw).hexdigest()
            doc['offers'][0]['record_path']='Data/item.xml'
            game_catalog.check_document(doc)
            doc['offers'][0]['record_path']=None
            with self.assertRaisesRegex(ValueError,'Missing record path'):game_catalog.check_document(doc)
            doc['offers'][0]['item_class']=None;doc['offers'][0]['kind']='unresolved'
            game_catalog.check_document(doc)

if __name__=='__main__':unittest.main()
