"""Codex local catalog reader. No executable imports or implicit currency conversion.

Inspired by sc_datapack's source format, independently implemented against our own
archive. Actual extracted bytes and individual records are checked before use.
"""
from decimal import Decimal, ROUND_CEILING
import hashlib, json, re
from pathlib import Path

def canonical(record):
    return hashlib.sha256(json.dumps(record,sort_keys=True,default=str,separators=(',',':')).encode()).hexdigest()

def finite(text):
    v=Decimal(text)
    if not v.is_finite():raise ValueError('Non-finite price')
    return v

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
        doc=json.loads(raw)
        if doc.get('format')!='sc-local-game-catalog-1' or not isinstance(doc.get('offers'),list) or len(doc['offers'])>20000:
            raise ValueError('Unsupported catalog')
        map_path=(self.folder/doc['class_map_file']).resolve()
        if not map_path.is_relative_to(self.folder.resolve()) or map_path.stat().st_size>8*1024*1024:raise ValueError('Invalid class map')
        map_bytes=map_path.read_bytes()
        if hashlib.sha256(map_bytes).hexdigest()!=doc['class_map_sha256']:raise ValueError('Class map digest mismatch')
        class_map=json.loads(map_bytes)
        if not isinstance(class_map,dict):raise ValueError('Invalid class map')
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
            parsed=json.loads(data.decode('utf-8-sig'),parse_float=Decimal)
            sources[name]=parsed.get('Collection',parsed)['Inventory']
        lookup={}
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
