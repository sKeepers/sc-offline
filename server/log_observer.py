"""Read-only local log observation. Pattern matches are observations, not engine success.

Parser written by Codex. Mission tag names were compared with star-citizen-live;
no code copied. Current-build coverage is reported only after matching local logs.
"""
from collections import deque
from datetime import datetime, timezone
import hashlib,json,os,re,threading,time
from pathlib import Path

def parse_line(line):
    if 'MissionStartCommsNotification' in line:
        m=re.search(r'ContractId:\s*\[([\w-]+)\].*?MissionId:\s*([\w-]+)',line)
        if m:return {'kind':'mission_started','mission_id':m[2],'contract_id':m[1]}
    if 'EndMission' in line:
        m=re.search(r'MissionId\[([^\]]+)\].*?CompletionType\[([^\]]+)\]',line)
        if m:return {'kind':'mission_ended','mission_id':m[1],'outcome':m[2]}
    if 'UpdateActiveObjective' in line:
        m=re.search(r'\[Text=([^\]]{0,300})\]',line)
        if m:return {'kind':'objective','text':m[1]}
    if 'Loading level' in line:
        m=re.search(r'Loading level\s+(\w[\w.-]{0,80})',line)
        if m:return {'kind':'level_loading','level':m[1]}
    if 'Log started on' in line:return {'kind':'session_log_started'}
    if re.search(r'EXCEPTION_ACCESS_VIOLATION|Fatal Error|Game crash|\[crash\].*(?:exception|code=0x)',line,re.I):return {'kind':'crash_signal','confirmed_crash':False}
    if re.search(r'\b(?:ATC|ASOP)\b',line) and re.search(r'unconfirmed|Unspawned|rejected|unavailable',line,re.I):
        return {'kind':'delivery_problem_signal','confirmed_delivery':False}
    return None

class LogObserver:
    def __init__(self, files, checkpoint):
        self.files=[Path(p) for p in files if p];self.path=Path(checkpoint)
        try:self.saved=json.loads(self.path.read_text(encoding='utf-8'))
        except (OSError,ValueError):self.saved={}
        self.events=deque(self.saved.get('events',[])[-200:],maxlen=200);self.cursors=self.saved.get('cursors',{})
        self.lock=threading.Lock();self.stop_event=threading.Event();self.worker=None;self.status={}

    def poll(self):
        with self.lock:
            changed=False
            for path in self.files:
                key=str(path.resolve());old=self.cursors.get(key)
                try:
                    st=path.stat()
                    with path.open('rb') as f:
                        span=old.get('span',min(st.st_size,256)) if old else min(st.st_size,256)
                        prefix=hashlib.sha256(f.read(span)).hexdigest()
                        if not old:
                            # First run starts at current EOF, so historical crashes do not alert.
                            self.cursors[key]={'offset':st.st_size,'inode':st.st_ino,'span':span,'prefix':prefix}
                            changed=True;self.status[key]='наблюдение с текущего конца журнала';continue
                        rotated=st.st_size<old['offset'] or st.st_ino!=old['inode'] or prefix!=old['prefix']
                        offset=0 if rotated else old['offset']
                        if rotated or (span<256 and st.st_size>span):
                            span=min(st.st_size,256);f.seek(0);prefix=hashlib.sha256(f.read(span)).hexdigest()
                        f.seek(offset);raw=f.read(256*1024)
                        if b'\n' in raw:
                            end=raw.rfind(b'\n')+1
                            for line in raw[:end].decode('utf-8',errors='replace').splitlines():
                                event=parse_line(line)
                                if event:self.events.append({**event,'observed_at':datetime.now(timezone.utc).isoformat(),'source':path.name,
                                                            'line_sha256':hashlib.sha256(line.encode()).hexdigest(),'evidence':'log_pattern'})
                            offset+=end;changed=True
                        elif len(raw)==256*1024:
                            # Bound an oversized malformed line. It is not an engine observation.
                            offset+=len(raw);changed=True;self.status[key]='пропущена слишком длинная строка'
                        self.cursors[key]={'offset':offset,'inode':st.st_ino,'span':span,'prefix':prefix}
                        self.status[key]='чтение журнала доступно'
                except OSError:self.status[key]='журнал недоступен'
            if changed:
                self.path.parent.mkdir(parents=True,exist_ok=True)
                temp=self.path.with_suffix('.tmp')
                with temp.open('w',encoding='utf-8') as f:
                    json.dump({'cursors':self.cursors,'events':list(self.events)},f,ensure_ascii=False);f.flush();os.fsync(f.fileno())
                temp.replace(self.path)

    def snapshot(self):
        with self.lock:return {'sources':[{ 'file':Path(p).name,'status':s} for p,s in self.status.items()],
                               'events':list(reversed(self.events)), 'engine_success_confirmed':False,
                               'message':'Совпадение строки журнала не доказывает выдачу, включение двигателей или безопасное появление.'}

    def start(self):
        self.poll()
        def run():
            while not self.stop_event.wait(1):
                try:self.poll()
                except Exception:
                    with self.lock:self.status['monitor']='ошибка сохранения монитора'
        self.worker=threading.Thread(target=run,daemon=True,name='local-log-observer');self.worker.start()

    def close(self):
        self.stop_event.set()
        if self.worker:self.worker.join(3)
