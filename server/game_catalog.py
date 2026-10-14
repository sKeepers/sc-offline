"""Codex local catalog reader. No executable imports or implicit currency conversion.

Inspired by sc_datapack's source format, independently implemented against our own
archive. Actual extracted bytes and individual records are checked before use.
"""
from datetime import datetime
from decimal import Decimal, ROUND_CEILING
import hashlib, json, math, re, unicodedata
from pathlib import Path

def canonical(record):
    return hashlib.sha256(json.dumps(record,sort_keys=True,default=str,separators=(',',':')).encode()).hexdigest()

def finite(text):
    v=Decimal(text)
    if not v.is_finite():raise ValueError('Non-finite price')
    return v

def no_constant(name):raise ValueError('Non-finite JSON constant')

def unique_keys(pairs):
    # Claude 2026-10-14: json keeps the last of duplicate keys silently; a GUID bound twice is refused.
    keys=[k for k,_ in pairs]
    if len(set(keys))!=len(keys):raise ValueError('Duplicate key')
    return dict(pairs)

def finite_float(text):
    # Claude 2026-10-14: 1e400 parses as float inf without any NaN/Infinity literal.
    v=float(text)
    if not math.isfinite(v):raise ValueError('Non-finite JSON number')
    return v

def strict_json(data,parse_float=finite_float):
    """Every catalog file: no duplicate keys at any depth, no NaN/Infinity, no overflowing numbers."""
    return json.loads(data,object_pairs_hook=unique_keys,parse_constant=no_constant,parse_float=parse_float)

HEX64=re.compile(r'[0-9a-f]{64}')
GUID=re.compile(r'[0-9a-z_.:-]{1,64}')
KIND=re.compile(r'[a-z_]{1,32}')
CLASS=re.compile(r'[A-Za-z0-9_]{1,95}')
# Claude 2026-10-14 (Codex review 1644): the real extractor keeps provenance - archive_entry in
# files[], record_path in offers[] and in class-map values. These are the exact real key sets.
# The shape without provenance is accepted only as a whole-document legacy form (Codex's own
# test_local_tools fixture uses it); a document mixing the two forms is refused.
FILE_KEYS={'path','size','sha256','archive_entry'}
OFFER_KEYS={'shop_id','guid','item_class','kind','buy_auec','sell_auec','stock','stock_basis','source_file','record_index','record_sha256','record_path'}
CLASS_MAP_KEYS={'item_class','kind','record_path'}
LEGACY_FILE_KEYS=FILE_KEYS-{'archive_entry'}
LEGACY_OFFER_KEYS=OFFER_KEYS-{'record_path'}
LEGACY_CLASS_MAP_KEYS=CLASS_MAP_KEYS-{'record_path'}
# A path inside the game archive / DataCore, shown as provenance and never opened. 1024 chars is
# about 4x Windows MAX_PATH and far above real P4K entry paths; the 16 MiB catalog limit still
# bounds the total.
PROVENANCE_LIMIT=1024

def provenance_path_ok(v):
    r"""Relative archive path: 'Data\Libs\...' or 'Data/Libs/...' (both separators accepted).
    Refused: empty or longer than PROVENANCE_LIMIT, control/format/line-separator characters,
    leading slash or backslash (absolute, UNC, \\?\), drive prefix 'C:', empty segments and
    segments made only of dots and spaces ('.', '..', '...', '.. ')."""
    if not isinstance(v,str) or not 0<len(v)<=PROVENANCE_LIMIT:return False
    if any(unicodedata.category(ch)[0]=='C' or unicodedata.category(ch) in ('Zl','Zp') for ch in v):return False
    if v[0] in '/\\' or re.match(r'[A-Za-z]:',v):return False
    return all(part.strip(' .') for part in re.split(r'[\\/]',v))

def shape_of(keys,real,legacy):
    if keys==real:return 'provenance'
    if keys==legacy:return 'legacy'
    return None

def captured_at_ok(v):
    # ISO-8601 time, or a short plain label (fixtures use 'fixture'/'synthetic').
    if not isinstance(v,str) or not 0<len(v)<=64:return False
    if re.fullmatch(r'[A-Za-z][A-Za-z0-9_.-]{0,63}',v):return True
    try:datetime.fromisoformat(v);return True
    except ValueError:return False

def check_document(doc):
    """Types and values of every field load() and snapshot() use, before anything is marked available."""
    if not isinstance(doc,dict) or doc.get('format')!='sc-local-game-catalog-1':raise ValueError('Unsupported catalog')
    offers,files=doc.get('offers'),doc.get('files')
    if not isinstance(offers,list) or len(offers)>20000 or not isinstance(files,list) or len(files)>20000:raise ValueError('Unsupported catalog')
    if 'game_manifest' not in doc or 'captured_at' not in doc or 'unresolved' not in doc:raise ValueError('Incomplete catalog')
    manifest=doc['game_manifest']
    if not isinstance(manifest,dict) or len(json.dumps(manifest,allow_nan=False))>64*1024:raise ValueError('Invalid game manifest')
    if not captured_at_ok(doc['captured_at']):raise ValueError('Invalid captured_at')
    if type(doc['unresolved']) is not int or not 0<=doc['unresolved']<=len(offers):raise ValueError('Invalid unresolved count')
    if not isinstance(doc.get('class_map_file'),str) or not isinstance(doc.get('class_map_sha256'),str) or not HEX64.fullmatch(doc['class_map_sha256']):
        raise ValueError('Invalid class map reference')
    shapes=set()
    for f in files:
        shape=shape_of(set(f),FILE_KEYS,LEGACY_FILE_KEYS) if isinstance(f,dict) else None
        if not shape or not isinstance(f['path'],str) or not 0<len(f['path'])<=260:raise ValueError('Invalid file entry')
        if type(f['size']) is not int or f['size']<0 or not isinstance(f['sha256'],str) or not HEX64.fullmatch(f['sha256']):raise ValueError('Invalid file entry')
        if shape=='provenance' and not provenance_path_ok(f['archive_entry']):raise ValueError('Invalid archive entry')
        shapes.add(shape)
    for o in offers:
        shape=shape_of(set(o),OFFER_KEYS,LEGACY_OFFER_KEYS) if isinstance(o,dict) else None
        if not shape:raise ValueError('Invalid offer')
        # An unresolved GUID may have no record path; a present one must be a safe archive path.
        if shape=='provenance' and o['record_path'] is not None and not provenance_path_ok(o['record_path']):raise ValueError('Invalid record path')
        shapes.add(shape)
        if not isinstance(o['guid'],str) or not GUID.fullmatch(o['guid']) or not isinstance(o['kind'],str) or not KIND.fullmatch(o['kind']):raise ValueError('Invalid offer')
        if not isinstance(o['source_file'],str) or not isinstance(o['record_sha256'],str) or not HEX64.fullmatch(o['record_sha256']):raise ValueError('Invalid offer')
        if any(o[k] is not None and not isinstance(o[k],str) for k in ('buy_auec','sell_auec','item_class')):raise ValueError('Invalid offer')
    if len(shapes)>1:raise ValueError('Provenance present only in part of the catalog')
    return shapes.pop() if shapes else None

def check_class_map(class_map,shape=None):
    """shape: what check_document found in catalog.json ('provenance', 'legacy' or None if empty).
    Class-map values must all have the same form, and the same as the catalog."""
    if not isinstance(class_map,dict):raise ValueError('Invalid class map')
    shapes={shape} if shape else set()
    for guid,b in class_map.items():
        value_shape=shape_of(set(b),CLASS_MAP_KEYS,LEGACY_CLASS_MAP_KEYS) if isinstance(b,dict) else None
        if not GUID.fullmatch(guid) or not value_shape:raise ValueError('Invalid class map')
        if not isinstance(b['item_class'],str) or not CLASS.fullmatch(b['item_class']) or not isinstance(b['kind'],str) or not KIND.fullmatch(b['kind']):
            raise ValueError('Invalid class map')
        if value_shape=='provenance' and not provenance_path_ok(b['record_path']):raise ValueError('Invalid class map record path')
        shapes.add(value_shape)
        if len(shapes)>1:raise ValueError('Provenance present only in part of the catalog')

class GameCatalog:
    def __init__(self, folder):
        self.folder=Path(folder);self.data=None;self.error=None;self.lookup={}
        if not (self.folder/'catalog.json').exists():return
        try:self.load()
        except (ValueError,KeyError,TypeError,OSError,RecursionError,IndexError,ArithmeticError,AttributeError) as e:
            self.error='Каталог не прошёл проверку: '+type(e).__name__

    def load(self):
        raw=(self.folder/'catalog.json').read_bytes()
        if len(raw)>16*1024*1024:raise ValueError('Catalog too large')
        doc=strict_json(raw)
        shape=check_document(doc)
        map_path=(self.folder/doc['class_map_file']).resolve()
        if not map_path.is_relative_to(self.folder.resolve()) or map_path.stat().st_size>8*1024*1024:raise ValueError('Invalid class map')
        map_bytes=map_path.read_bytes()
        if hashlib.sha256(map_bytes).hexdigest()!=doc['class_map_sha256']:raise ValueError('Class map digest mismatch')
        class_map=strict_json(map_bytes)
        check_class_map(class_map,shape)
        sources={};budget=0
        for f in doc['files']:
            name=f['path'];path=(self.folder/name).resolve()
            if not path.is_relative_to(self.folder.resolve()) or path==self.folder.resolve():raise ValueError('Invalid path')
            if name in sources:raise ValueError('Duplicate source')
            if path.stat().st_size>8*1024*1024:raise ValueError('Source too large')
            data=path.read_bytes();budget+=len(data)
            if budget>32*1024*1024 or len(data)!=f['size'] or hashlib.sha256(data).hexdigest()!=f['sha256']:
                raise ValueError('Source digest mismatch')
            if not data:sources[name]=[];continue
            parsed=strict_json(data.decode('utf-8-sig'),parse_float=Decimal)
            sources[name]=parsed.get('Collection',parsed)['Inventory']
        lookup={};kinds={}
        for offer in doc['offers']:
            index=offer['record_index']
            if type(index)is not int or index<0:raise ValueError('Invalid record index')
            record=sources[offer['source_file']][index]
            if Path(offer['source_file']).stem!=offer['shop_id']:raise ValueError('Shop source mismatch')
            if canonical(record)!=offer['record_sha256']:raise ValueError('Record digest mismatch')
            ids=record.get('ID',{}).get('ID',[])
            if (str(ids[0]).lower() if ids else '')!=offer['guid']:raise ValueError('GUID mismatch')
            for src,key in [('BuyPrice','buy_auec'),('SellPrice','sell_auec')]:
                if (str(record[src]) if src in record else None)!=offer[key]:raise ValueError('Price mismatch')
                if offer[key] is not None and finite(offer[key])<0:raise ValueError('Negative price')
            if offer['stock']is not None or offer['stock_basis']!='unknown':raise ValueError('Static data is not live stock')
            if not isinstance(offer['shop_id'],str) or not re.fullmatch(r'[A-Za-z0-9_.:-]{1,120}',offer['shop_id']):raise ValueError('Invalid shop')
            item=offer['item_class']
            binding=class_map.get(offer['guid'])
            if binding:
                if item!=binding['item_class'] or offer['kind']!=binding['kind']:raise ValueError('Class/GUID binding mismatch')
            elif item is not None or offer['kind']!='unresolved':raise ValueError('Unknown GUID must stay unresolved')
            if item is not None and (not isinstance(item,str) or not re.fullmatch(r'[A-Za-z0-9_]{1,95}',item)):
                raise ValueError('Invalid class')
            if item:
                # One class, one kind: otherwise the fleet decision in server.py is ambiguous.
                if kinds.setdefault(item,offer['kind'])!=offer['kind']:raise ValueError('Class bound to different kinds')
                key=(offer['shop_id'],item)
                # Duplicate variants are displayed, but ambiguous conversions are refused.
                lookup[key]=None if key in lookup else offer
        self.data=doc;self.lookup=lookup;self.digest=hashlib.sha256(raw).hexdigest()

    def snapshot(self):
        if not self.data:return {'status':'unavailable','message':self.error or 'Каталог ещё не извлечён из вашей игры.','offers':[]}
        return {'status':'verified_sources','version':self.digest,'game_manifest':self.data['game_manifest'],
                'captured_at':self.data['captured_at'],'offers':self.data['offers'],
                'unresolved':self.data['unresolved'],'shop_files':len(self.data['files']),
                'live_stock_known':False,'physical_delivery':False,
                'message':'Базовые цены из игровых файлов. Текущий остаток и физическая выдача неизвестны.'}

    def local_offer(self, shop, item, rounding='exact'):
        if not isinstance(shop,str) or not isinstance(item,str):raise ValueError('Expected shop and class strings')
        o=self.lookup.get((shop,item))
        if not o or o['buy_auec']is None:raise ValueError('Нет однозначного предложения с базовой ценой.')
        if re.search(r'anniversary|rental|test|featuretest',o['shop_id'],re.I):
            raise ValueError('Архивный, тестовый или арендный каталог не является предложением покупки.')
        price=finite(o['buy_auec'])
        if rounding not in ('exact','ceil'):raise ValueError('Неизвестное правило округления.')
        if rounding=='ceil':price=price.to_integral_value(rounding=ROUND_CEILING)
        if price!=price.to_integral_value() or not 1<=price<=9000000000000:
            raise ValueError('Цена не переносится в целочисленный учёт без отдельного правила.')
        return o,int(price)
