"""Independent local planning: same-frame routes and user-entered refinery jobs.

No automatic trade, missions or control of the game. No external prices/network.
"""
import json, math, time, uuid

def ensure_schema(db):
    db.execute("CREATE TABLE IF NOT EXISTS tool_entries(id TEXT PRIMARY KEY, kind TEXT NOT NULL, payload TEXT NOT NULL, version INTEGER NOT NULL DEFAULT 1)")

def label(value,limit=120):
    if not isinstance(value,str) or not value.strip() or len(value)>limit or any(ord(c)<32 for c in value):raise ValueError('Нужно непустое название.')
    return value.strip()

def number(value,lo,hi):
    if type(value)not in (int,float) or not lo<=value<=hi or not math.isfinite(value):raise ValueError('Число вне допустимого диапазона.')
    return value

def route(points, capacity=0, cargo=0):
    if not isinstance(points,list) or not 2<=len(points)<=50:raise ValueError('Нужно от 2 до 50 остановок.')
    cleaned=[]
    for p in points:
        if not isinstance(p,dict) or set(p)!={'name','frame','x','y','z'}:raise ValueError('Остановка: name, frame, x, y, z.')
        cleaned.append({'name':label(p['name']),'frame':label(p['frame']),**{k:number(p[k],-1e13,1e13) for k in ('x','y','z')}})
    if len({p['frame'] for p in cleaned})!=1:raise ValueError('Маршрут требует одну систему координат. Межзонное расстояние неизвестно.')
    number(capacity,0,1e9);number(cargo,0,1e9)
    if cargo>capacity:raise ValueError('Груз превышает указанную вместимость корабля.')
    def distance(a,b):return math.dist([a[k] for k in ('x','y','z')],[b[k] for k in ('x','y','z')])
    ordered=[cleaned[0]];remaining=cleaned[1:]
    while remaining:
        next_point=min(remaining,key=lambda p:distance(ordered[-1],p));remaining.remove(next_point);ordered.append(next_point)
    legs=[{'from':a['name'],'to':b['name'],'meters':distance(a,b)} for a,b in zip(ordered,ordered[1:])]
    return {'points':ordered,'legs':legs,'distance_m':sum(l['meters'] for l in legs),'capacity_scu':capacity,'cargo_scu':cargo,
            'method':'nearest_next','planning_only':True,'obstacles_checked':False}

def apply(db,op,data):
    if op=='/tools/routes/save':
        if set(data)-{'name','points','capacity_scu','cargo_scu'}:raise ValueError('Неизвестные поля маршрута.')
        result=route(data.get('points'),data.get('capacity_scu',0),data.get('cargo_scu',0));result['name']=label(data.get('name'));kind='route'
    elif op=='/tools/refining/create':
        if set(data)-{'name','resource','quantity','duration_minutes'}:raise ValueError('Неизвестные поля переработки.')
        duration=number(data.get('duration_minutes'),1,30*24*60)
        result={'name':label(data.get('name')),'resource':label(data.get('resource')),'quantity':number(data.get('quantity'),0.000001,1e9),
                'started_at':time.time(),'due_at':time.time()+duration*60,'status':'tracking','source':'manual','game_confirmed':False};kind='refining'
    elif op in ('/tools/entries/delete','/tools/refining/complete'):
        if set(data)!={'id','expected_version'}:raise ValueError('Нужны id и expected_version.')
        entry=db.execute('SELECT * FROM tool_entries WHERE id=?',(label(data['id']),)).fetchone()
        if not entry:raise ValueError('Запись не найдена.')
        if type(data['expected_version'])is not int or data['expected_version']!=entry['version']:raise ValueError('Запись изменена. Обновите страницу.')
        if op.endswith('/delete'):
            db.execute('DELETE FROM tool_entries WHERE id=?',(entry['id'],));return {'deleted':entry['id']}
        if entry['kind']!='refining':raise ValueError('Это не запись переработки.')
        result=json.loads(entry['payload']);result['status']='user_confirmed';result['confirmed_at']=time.time()
        db.execute('UPDATE tool_entries SET payload=?,version=version+1 WHERE id=?',(json.dumps(result),entry['id']))
        return {'id':entry['id'],'version':entry['version']+1,**result}
    else:raise ValueError('Неизвестная операция планировщика.')
    identifier=uuid.uuid4().hex
    db.execute('INSERT INTO tool_entries(id,kind,payload) VALUES(?,?,?)',(identifier,kind,json.dumps(result,allow_nan=False)))
    return {'id':identifier,'kind':kind,'version':1,**result}

def snapshot(db):
    rows=[]
    for r in db.execute('SELECT * FROM tool_entries ORDER BY rowid DESC LIMIT 500'):
        item={'id':r['id'],'kind':r['kind'],'version':r['version'],**json.loads(r['payload'])}
        if item['kind']=='refining':
            item['remaining_seconds']=max(0,math.ceil(item['due_at']-time.time()))
            item['timer_ready']=item['remaining_seconds']==0
        rows.append(item)
    return {'entries':rows,'planning_only':True,'physical_delivery':False}
