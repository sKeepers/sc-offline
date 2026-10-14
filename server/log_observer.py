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
    if re.search(r'EXCEPTION_ACCESS_VIOLATION|(?<!non-)(?<!non )\bFatal Error|Game crash|\[crash\].*(?:exception|code=0x)',line,re.I):return {'kind':'crash_signal','confirmed_crash':False}
    if re.search(r'\b(?:ATC|ASOP)\b',line) and re.search(r'unconfirmed|Unspawned|rejected|unavailable',line,re.I):
        return {'kind':'delivery_problem_signal','confirmed_delivery':False}
    return None

# Claude 2026-10-14: per-poll bounds. A checkpoint value never raises them.
PREFIX_SPAN=256          # bytes hashed to recognise a replaced file
READ_LIMIT=256*1024      # bytes read after the cursor in one poll
CHECKPOINT_LIMIT=4*1024*1024
_K32=None

def _kernel32():
    """kernel32 with declared signatures: a 64-bit HANDLE is never truncated to a C int."""
    global _K32
    if _K32 is None:
        import ctypes
        from ctypes import wintypes
        k=ctypes.WinDLL('kernel32',use_last_error=True)
        k.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
        k.CreateFileW.restype=wintypes.HANDLE
        k.CloseHandle.argtypes=[wintypes.HANDLE]
        k.CloseHandle.restype=wintypes.BOOL
        _K32=k
    return _K32

def _open_shared(path):
    """Read-only handle that never blocks the game from renaming, deleting or writing its log.

    Ownership passes once: raw handle -> CRT fd -> FileIO -> BufferedReader.
    Each failure closes only what owns the handle at that moment, exactly once."""
    if os.name!='nt':return open(path,'rb')
    import ctypes,io,msvcrt
    from ctypes import wintypes
    k=_kernel32()
    # GENERIC_READ; FILE_SHARE_READ|WRITE|DELETE; OPEN_EXISTING; FILE_ATTRIBUTE_NORMAL
    handle=k.CreateFileW(str(path),0x80000000,7,None,3,0x80,None)
    if handle is None or handle==wintypes.HANDLE(-1).value:raise ctypes.WinError(ctypes.get_last_error())
    try:fd=msvcrt.open_osfhandle(handle,os.O_RDONLY)
    except BaseException:
        k.CloseHandle(handle);raise   # no fd yet: the raw handle is still ours
    try:raw=io.FileIO(fd,'rb',closefd=True)
    except BaseException:
        os.close(fd);raise            # the fd owns the handle; never CloseHandle it as well
    try:return io.BufferedReader(raw)
    except BaseException:
        raw.close();raise             # FileIO owns the fd and closes it once

_HEX64=re.compile(r'[0-9a-f]{64}')

def _valid_cursor(key,c):
    """A damaged checkpoint loses that cursor; it never widens a read or crashes the poll."""
    if not isinstance(key,str) or not 0<len(key)<=4096 or not isinstance(c,dict):return False
    if not {'offset','inode','span','prefix'}<=set(c)<={'offset','inode','span','prefix','discard'}:return False
    if not all(type(c[k]) is int for k in ('offset','inode','span')):return False
    if not (0<=c['offset']<2**63 and 0<=c['inode']<2**128 and 0<=c['span']<=PREFIX_SPAN):return False
    return isinstance(c['prefix'],str) and bool(_HEX64.fullmatch(c['prefix'])) and c.get('discard',True) is True

_EVENT_FIELDS={'mission_started':{'mission_id','contract_id'},'mission_ended':{'mission_id','outcome'},
               'objective':{'text'},'level_loading':{'level'},'session_log_started':set(),
               'crash_signal':{'confirmed_crash'},'delivery_problem_signal':{'confirmed_delivery'}}
_EVENT_COMMON={'kind','observed_at','source','line_sha256','evidence'}

def _valid_event(e):
    """Only the shape parse_line/poll write. Anything else is dropped, not shown as evidence."""
    # Claude 2026-10-14 (Codex review 1644): kind may be [] or {} in valid JSON; an unhashable
    # value in a dict lookup raises TypeError, so the type is checked first. Every other lookup
    # here is a set of JSON object keys (always str) or an equality check, which cannot raise.
    if not isinstance(e,dict) or not isinstance(e.get('kind'),str) or e['kind'] not in _EVENT_FIELDS:return False
    extra=_EVENT_FIELDS[e['kind']]
    if set(e)!=_EVENT_COMMON|extra or e['evidence']!='log_pattern':return False
    if not isinstance(e['line_sha256'],str) or not _HEX64.fullmatch(e['line_sha256']):return False
    if not isinstance(e['source'],str) or not 0<len(e['source'])<=255:return False
    if not isinstance(e['observed_at'],str) or len(e['observed_at'])>64:return False
    try:datetime.fromisoformat(e['observed_at'])
    except ValueError:return False
    for name in extra:
        if name.startswith('confirmed_'):
            if e[name] is not False:return False
        elif not isinstance(e[name],str) or len(e[name])>1024:return False
    return True

def _no_constant(name):raise ValueError('Non-finite JSON constant')

class LogObserver:
    def __init__(self, files, checkpoint):
        self.files=[Path(p) for p in files if p];self.path=Path(checkpoint)
        try:
            with self.path.open('rb') as f:raw=f.read(CHECKPOINT_LIMIT+1)
            if len(raw)>CHECKPOINT_LIMIT:raise ValueError('Checkpoint too large')
            self.saved=json.loads(raw.decode('utf-8'),parse_constant=_no_constant)
        except (OSError,ValueError,RecursionError):self.saved={}
        if not isinstance(self.saved,dict):self.saved={}
        events=self.saved.get('events');cursors=self.saved.get('cursors')
        self.events=deque([e for e in events[-200:] if _valid_event(e)] if isinstance(events,list) else [],maxlen=200)
        self.cursors=dict([(k,v) for k,v in cursors.items() if _valid_cursor(k,v)][:64]) if isinstance(cursors,dict) else {}
        self.lock=threading.Lock();self.stop_event=threading.Event();self.worker=None;self.status={};self.absent=set()
        self.unsaved=False  # Claude 0.3.1 review: a failed save is retried on the next poll

    def poll(self):
        with self.lock:
            changed=False
            for path in self.files:
                key=str(path.resolve());old=self.cursors.get(key)
                try:
                    with _open_shared(path) as f:
                        st=os.fstat(f.fileno())  # the opened file, not a path that may have rotated since
                        span=old['span'] if old else min(st.st_size,PREFIX_SPAN)
                        prefix=hashlib.sha256(f.read(span)).hexdigest()
                        if not old and key in self.absent:
                            # Created after this monitor saw it missing: the whole file is new.
                            old={'offset':0,'inode':st.st_ino,'span':span,'prefix':prefix};changed=True
                        if not old:
                            # First run starts at current EOF, so historical crashes do not alert.
                            self.cursors[key]={'offset':st.st_size,'inode':st.st_ino,'span':span,'prefix':prefix}
                            changed=True;self.status[key]='наблюдение с текущего конца журнала';continue
                        rotated=st.st_size<old['offset'] or st.st_ino!=old['inode'] or prefix!=old['prefix']
                        offset=0 if rotated else old['offset']
                        if rotated or (span<PREFIX_SPAN and st.st_size>span):
                            span=min(st.st_size,PREFIX_SPAN);f.seek(0);prefix=hashlib.sha256(f.read(span)).hexdigest()
                        f.seek(offset);raw=f.read(READ_LIMIT);status='чтение журнала доступно'
                        discard=bool(old.get('discard')) and not rotated
                        if discard:
                            # The rest of an oversized line is not a separate line.
                            cut=raw.find(b'\n');status='пропущена слишком длинная строка'
                            if cut<0:offset+=len(raw);changed=changed or bool(raw);raw=b''
                            else:offset+=cut+1;raw=raw[cut+1:];discard=False;changed=True
                        if b'\n' in raw:
                            end=raw.rfind(b'\n')+1
                            for line in raw[:end].decode('utf-8',errors='replace').splitlines():
                                event=parse_line(line)
                                if event:self.events.append({**event,'observed_at':datetime.now(timezone.utc).isoformat(),'source':path.name,
                                                            'line_sha256':hashlib.sha256(line.encode()).hexdigest(),'evidence':'log_pattern'})
                            offset+=end;changed=True
                        elif len(raw)==READ_LIMIT:
                            # Bound an oversized malformed line. It is not an engine observation.
                            offset+=len(raw);changed=True;discard=True;status='пропущена слишком длинная строка'
                        self.cursors[key]={'offset':offset,'inode':st.st_ino,'span':span,'prefix':prefix,**({'discard':True} if discard else {})}
                        self.status[key]=status
                except FileNotFoundError:
                    if key not in self.cursors:self.absent.add(key)
                    self.status[key]='журнал недоступен'
                except OSError:self.status[key]='журнал недоступен'
            if changed or self.unsaved:
                self.unsaved=True
                self.path.parent.mkdir(parents=True,exist_ok=True)
                temp=self.path.with_suffix('.tmp')
                with temp.open('w',encoding='utf-8') as f:
                    json.dump({'cursors':self.cursors,'events':list(self.events)},f,ensure_ascii=False);f.flush();os.fsync(f.fileno())
                temp.replace(self.path)
                self.unsaved=False;self.status.pop('monitor',None)

    def snapshot(self):
        with self.lock:return {'sources':[{ 'file':Path(p).name,'status':s} for p,s in self.status.items()],
                               'events':list(reversed(self.events)), 'engine_success_confirmed':False,
                               'message':'Совпадение строки журнала не доказывает выдачу, включение двигателей или безопасное появление.'}

    def _guarded_poll(self):
        # Claude 0.3.1 review: the monitor is optional. A failed save in the first poll
        # (log-observer.json held open by another process) must not stop the server start.
        try:self.poll()
        except Exception:
            with self.lock:self.status['monitor']='ошибка сохранения монитора'

    def start(self):
        self._guarded_poll()
        def run():
            while not self.stop_event.wait(1):self._guarded_poll()
        self.worker=threading.Thread(target=run,daemon=True,name='local-log-observer');self.worker.start()

    def close(self):
        self.stop_event.set()
        if self.worker:self.worker.join(3)
