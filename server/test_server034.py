"""Independent synthetic regressions. No game, real log or live ledger mutation."""
from pathlib import Path
import json,tempfile,unittest
from unittest.mock import patch
from log_observer import LogObserver,parse_line,_valid_event

STALL='<Actor stall> Actor stall detected, Player: Synthetic, Type: downstream, Length: 3.746040'
DEST="<Vehicle Destruction> CVehicle::OnAdvanceDestroyLevel: Vehicle 'SyntheticShip' [42] in zone 'test' advanced from destroy level {} to {} caused by 'Synthetic'"

class ClassificationTests(unittest.TestCase):
    def test_stall_is_not_crash_and_duration_is_bounded(self):
        event=parse_line(STALL)
        self.assertEqual(event['kind'],'actor_stall_signal')
        self.assertEqual(event['duration_seconds'],'3.746040')
        self.assertIsNone(parse_line(STALL.replace('3.746040','999999')))
    def test_disable_destroy_and_reverse_transitions(self):
        self.assertEqual(parse_line(DEST.format(0,1))['kind'],'vehicle_disabled_signal')
        for a,b in [(0,2),(1,2)]:
            self.assertEqual(parse_line(DEST.format(a,b))['kind'],'vehicle_destroyed_signal')
        for a,b in [(2,1),(1,0),(2,2)]:self.assertIsNone(parse_line(DEST.format(a,b)))
    def test_bounded_and_unrelated_input(self):
        for line in [None,{},'x'*17000,'Actor stall mentioned in help','Vehicle was destroyed','non-fatal error']:
            self.assertIsNone(parse_line(line))
        self.assertEqual(parse_line('EXCEPTION_ACCESS_VIOLATION')['kind'],'crash_signal')

class DedupTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.log=self.root/'Game.log';self.log.write_text('historical EXCEPTION_ACCESS_VIOLATION\n')
        self.cp=self.root/'observer.json';self.observer=LogObserver([self.log],self.cp);self.observer.poll()
    def tearDown(self):self.tmp.cleanup()
    def append(self,line):
        with self.log.open('a',encoding='utf-8') as f:f.write(line+'\n')
    def test_no_historical_alert_and_repeat_cooldown(self):
        self.assertFalse(self.observer.snapshot()['events'])
        with patch('log_observer.time.monotonic',return_value=100):
            self.append(STALL);self.append(STALL);self.observer.poll()
        self.assertEqual(len(self.observer.snapshot()['events']),1)
        with patch('log_observer.time.monotonic',return_value=106):
            self.append(STALL);self.observer.poll()
        self.assertEqual(len(self.observer.snapshot()['events']),2)
    def test_timestamped_exact_repeat_survives_restart_and_rotation(self):
        line='<2026-10-15T10:00:00.000Z> '+STALL
        self.append(line);self.observer.poll()
        self.observer=LogObserver([self.log],self.cp)
        self.log.write_text(line+'\n',encoding='utf-8');self.observer.poll()
        self.assertEqual(len(self.observer.snapshot()['events']),1)
        event=self.observer.snapshot()['events'][0]
        self.assertTrue(_valid_event(event))
        self.assertEqual(event['log_timestamp'],'2026-10-15T10:00:00.000Z')
    def test_distinct_vehicles_and_destruction_levels_not_collapsed(self):
        self.append(DEST.format(0,1));self.append(DEST.format(1,2));self.append(DEST.format(0,1).replace('[42]','[43]'))
        self.observer.poll();self.assertEqual(len(self.observer.snapshot()['events']),3)
        self.assertNotIn('crash_signal',[e['kind'] for e in self.observer.snapshot()['events']])
    def test_recent_duplicates_still_suppressed_after_restart(self):
        self.append(STALL);self.observer.poll()
        self.observer=LogObserver([self.log],self.cp)
        self.append(STALL);self.observer.poll();self.assertEqual(len(self.observer.snapshot()['events']),1)
    def test_invalid_saved_new_payload_dropped(self):
        self.append(STALL);self.observer.poll();event=self.observer.snapshot()['events'][0]
        event['duration_seconds']='nan';self.assertFalse(_valid_event(event))
    def test_distinct_actor_stalls_are_separate(self):
        self.append(STALL);self.append(STALL.replace('Player: Synthetic','Player: Another'))
        self.observer.poll();self.assertEqual(len(self.observer.events),2)
    def test_recent_map_is_bounded_during_burst(self):
        with patch('log_observer.time.monotonic',return_value=100):
            for i in range(800):self.observer._observe({'kind':'objective','text':str(i)},str(i),'Game.log')
        self.assertLessEqual(len(self.observer.recent),512)
        self.assertEqual(len(self.observer.events),200)
    def test_event_payload_cannot_claim_confirmed_crash(self):
        self.append('EXCEPTION_ACCESS_VIOLATION');self.observer.poll()
        event=self.observer.snapshot()['events'][0];event['confirmed_crash']=True
        self.assertFalse(_valid_event(event))

import test_server as base_http
class GuideHttpTests(unittest.TestCase):
    setUp=base_http.HttpTests.setUp
    tearDown=base_http.HttpTests.tearDown
    request=base_http.HttpTests.request
    def test_data_images_allowed_only_for_local_testing_page(self):
        status,body,headers=self.request('/testing',headers={'Authorization':''})
        self.assertEqual(status,200)
        self.assertIn("img-src 'self' data:",headers['Content-Security-Policy'])
        self.assertNotIn('img-src',self.request('/')[2]['Content-Security-Policy'])
        self.assertIn(b'accept="image/png,image/jpeg,image/webp"',body)
        self.assertNotIn(b'a'*64,body)

if __name__=='__main__':unittest.main()
