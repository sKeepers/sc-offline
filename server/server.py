"""Local single-player state service. No game networking or engine integration."""
from __future__ import annotations

import argparse
from contextlib import closing
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
import math
from logging.handlers import RotatingFileHandler
from pathlib import Path
import secrets
import sqlite3
import sys
import subprocess
import tempfile
import uuid
import webbrowser
from catalog import Catalog
from game_catalog import GameCatalog
import operations
from log_observer import LogObserver

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')

VERSION = '0.3.3'
LIMIT = 64 * 1024
MAX_CREDITS = 9_000_000_000_000
# Catalog files: a version younger than CATALOG_SETTLE seconds is shown as "updating" (the last
# complete one stays in use); a file that vanished is bridged for CATALOG_GRACE seconds (rename/copy).
CATALOG_SETTLE = 1.0
CATALOG_GRACE = 15.0


import acquisition as acq

class Problem(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


def text(data, key, limit=120):
    value = data.get(key)
    if not isinstance(value, str) or not value.strip() or len(value) > limit or any(ord(c) < 32 for c in value):
        raise Problem(400, f'{key} должно содержать от 1 до {limit} символов.')
    return value.strip()


def integer(data, key, low, high):
    value = data.get(key)
    if type(value) is not int or not low <= value <= high:
        raise Problem(400, f'{key} должно быть целым числом от {low} до {high}.')
    return value


class Store:
    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with closing(self.connect()) as db:
            version = db.execute('PRAGMA user_version').fetchone()[0]
            if version not in (0, 1, 2):
                raise RuntimeError('Database schema is newer than this server. Keep the database and use the matching server.')
            db.execute('PRAGMA journal_mode=WAL')
            db.executescript('''
                BEGIN IMMEDIATE;
                CREATE TABLE IF NOT EXISTS profile (
                    id INTEGER PRIMARY KEY CHECK(id=1), name TEXT NOT NULL,
                    credits INTEGER NOT NULL CHECK(credits BETWEEN 0 AND 9000000000000));
                INSERT OR IGNORE INTO profile VALUES(1, 'Локальный пилот', 0);
                CREATE TABLE IF NOT EXISTS locations(id TEXT PRIMARY KEY, name TEXT NOT NULL);
                INSERT OR IGNORE INTO locations VALUES('offline-home', 'Домашнее хранилище (без привязки к миру)');
                CREATE TABLE IF NOT EXISTS hangars(
                    id TEXT PRIMARY KEY, name TEXT NOT NULL,
                    location_id TEXT NOT NULL REFERENCES locations(id));
                CREATE TABLE IF NOT EXISTS ships(
                    id TEXT PRIMARY KEY, class_name TEXT NOT NULL, name TEXT NOT NULL,
                    state TEXT NOT NULL CHECK(state IN ('stored','deployed')),
                    location_id TEXT REFERENCES locations(id),
                    hangar_id TEXT UNIQUE REFERENCES hangars(id), version INTEGER NOT NULL DEFAULT 1,
                    CHECK((state='stored' AND location_id IS NOT NULL) OR
                          (state='deployed' AND location_id IS NULL AND hangar_id IS NULL)));
                CREATE TABLE IF NOT EXISTS inventory(
                    item_class TEXT NOT NULL, location_id TEXT NOT NULL REFERENCES locations(id),
                    quantity INTEGER NOT NULL CHECK(quantity BETWEEN 0 AND 1000000),
                    PRIMARY KEY(item_class, location_id));
                CREATE TABLE IF NOT EXISTS events(
                    sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                    created_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')),
                    operation TEXT NOT NULL, details TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS requests(
                    key TEXT PRIMARY KEY, digest TEXT NOT NULL, response TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS bridge_ops(
                    id TEXT PRIMARY KEY, ship_id TEXT NOT NULL REFERENCES ships(id),
                    status TEXT NOT NULL CHECK(status IN ('reserved','confirmed','cancelled')),
                    entity_id TEXT, reason TEXT);
                CREATE UNIQUE INDEX IF NOT EXISTS bridge_one_reservation
                    ON bridge_ops(ship_id) WHERE status='reserved';
                PRAGMA user_version=2;
                COMMIT;
            ''')
            acq.ensure_schema(db)
            operations.ensure_schema(db)
        self.game_catalog = GameCatalog(self.path.parent / 'game-catalog')

    def connect(self):
        db = sqlite3.connect(self.path, timeout=10, isolation_level=None)
        db.row_factory = sqlite3.Row
        db.execute('PRAGMA foreign_keys=ON')
        return db

    @staticmethod
    def need(db, table, identifier):
        # Only caller-owned constant table names, never request-supplied SQL.
        row = db.execute(f'SELECT * FROM {table} WHERE id=?', (identifier,)).fetchone()
        if row is None:
            raise Problem(404, f'{table}: запись не найдена.')
        return dict(row)

    def snapshot(self):
        with closing(self.connect()) as db:
            db.execute('BEGIN')
            result = {'server_version': VERSION, 'schema_version': 2, 'game_connected': False}
            result['profile'] = dict(db.execute('SELECT name, credits FROM profile WHERE id=1').fetchone())
            for table in ('locations', 'hangars', 'ships', 'inventory'):
                result[table] = [dict(r) for r in db.execute(f'SELECT * FROM {table} ORDER BY rowid')]
            result['bridge_operations'] = [dict(r) for r in db.execute('SELECT * FROM bridge_ops')]
            result['events'] = [dict(r) for r in db.execute('SELECT * FROM events ORDER BY sequence DESC LIMIT 100')]
            db.execute('COMMIT')
            return result

    @staticmethod
    def fields(data, allowed):
        if set(data)-allowed:raise Problem(400, 'Unexpected request fields.')

    def shops(self):
        with closing(self.connect()) as db:
            db.execute('BEGIN')
            result = {'currency':'LOCAL_CREDIT','physical_delivery':False,
                    'offers':[dict(r) for r in db.execute("SELECT o.shop_id,o.item_class,o.source_id,o.currency,o.version,CASE WHEN s.authoritative=1 THEN o.unit_price END AS unit_price,CASE WHEN s.authoritative=1 THEN o.stock END AS stock,s.label AS source_label,s.kind AS source_kind,s.authoritative FROM acq_offer o JOIN acq_price_source s ON s.id=o.source_id ORDER BY shop_id,item_class")],
                    'holdings':[dict(r) for r in db.execute('SELECT * FROM acq_holding WHERE profile_id=1')],
                    'purchases':[json.loads(r[0]) for r in db.execute('SELECT receipt FROM acq_purchase WHERE profile_id=1 ORDER BY rowid DESC LIMIT 100')]}
            for offer in result['offers']:
                offer['status'] = ('catalog_changed' if self.stale_catalog_source(offer['source_id']) else
                                   'not_authoritative' if not offer['authoritative'] else
                                   'unknown_price' if offer['unit_price'] is None else
                                   'unknown_stock' if offer['stock'] is None else
                                   'out_of_stock' if offer['stock'] == 0 else 'available')
            db.execute('COMMIT')
            return result

    def backup(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'offline.sqlite3'
            with closing(self.connect()) as src, closing(sqlite3.connect(path)) as dst:
                src.backup(dst)
            return path.read_bytes()

    def stale_catalog_source(self, source):
        current = getattr(self.game_catalog, 'digest', None) or '-'
        return source.startswith('catalog-local-') and source != 'catalog-local-' + current[:12]

    def mutate(self, operation, data, key):
        if not isinstance(data, dict):
            raise Problem(400, 'Тело JSON должно быть объектом.')
        if not isinstance(key, str) or not 1 <= len(key) <= 100 or not key.isascii() or any(ord(c) < 33 for c in key):
            raise Problem(400, 'Provide an Idempotency-Key header (1-100 printable ASCII characters).')
        if operation == '/shops/quote':
            with closing(self.connect()) as db:
                return self.apply(db, operation, data, key)
        digest = hashlib.sha256((operation + '\n' + json.dumps(data, sort_keys=True, separators=(',', ':'), allow_nan=False)).encode()).hexdigest()
        with closing(self.connect()) as db:
            db.execute('BEGIN IMMEDIATE')
            try:
                previous = db.execute('SELECT digest,response FROM requests WHERE key=?', (key,)).fetchone()
                if previous:
                    if previous['digest'] != digest:
                        raise Problem(409, 'Ключ повтора уже использован для другого запроса.')
                    db.execute('ROLLBACK')
                    return json.loads(previous['response'])
                result = self.apply(db, operation, data, key)
                db.execute('INSERT INTO events(operation,details) VALUES(?,?)', (operation, json.dumps(result)))
                db.execute('INSERT INTO requests VALUES(?,?,?)', (key, digest, json.dumps(result)))
                db.execute('COMMIT')
                return result
            except Exception:
                if db.in_transaction:
                    db.execute('ROLLBACK')
                raise

    def apply(self, db, op, data, key=None):
        if op.startswith('/tools/'):
            try:return operations.apply(db,op,data)
            except ValueError as error:raise Problem(400,str(error)) from error
        if op.startswith('/shops/'):
            try:
                principal=acq.principal_from_server_auth(True)  # HTTP guard has passed; Store is a trusted local interface.
                if op == '/shops/catalog-offer':
                    self.fields(data,{'shop_id','item_class','stock','use_base_price','expected_version','rounding'})
                    if data.get('use_base_price')is not True:
                        raise Problem(400,'Подтвердите перенос числовой базовой цены в LOCAL_CREDIT для локального учёта.')
                    try:offer,price=self.game_catalog.local_offer(data.get('shop_id'),data.get('item_class'),data.get('rounding','exact'))
                    except ValueError as error:raise Problem(400,str(error)) from error
                    stock=integer(data,'stock',0,1000000)
                    source='catalog-local-'+self.game_catalog.digest[:12]
                    if not db.execute('SELECT 1 FROM acq_price_source WHERE id=?',(source,)).fetchone():
                        acq.register_source(db,source,'owner_declared',True,'Локальный учёт по базовым ценам игры; остаток задан владельцем; '+self.game_catalog.digest[:12])
                    local_shop='local-'+hashlib.sha256(offer['shop_id'].encode()).hexdigest()[:20]
                    return acq.put_offer(db,source,local_shop,offer['item_class'],price,stock,data.get('expected_version'))
                if op == '/shops/source':
                    self.fields(data, {'id','kind','authoritative','label'})
                    return acq.register_source(db, data.get('id'), data.get('kind'), data.get('authoritative'), data.get('label'))
                if op == '/shops/offer':
                    self.fields(data, {'source_id','shop_id','item_class','unit_price','stock','expected_version'})
                    return acq.put_offer(db, data.get('source_id'), data.get('shop_id'), data.get('item_class'), data.get('unit_price'), data.get('stock'), data.get('expected_version'))
                if op == '/shops/quote':
                    self.fields(data, {'shop_id','item_class'})
                    shop, item = text(data,'shop_id'), text(data,'item_class')
                    result = acq.quote(db, shop, item)
                    row = db.execute('SELECT source_id FROM acq_offer WHERE shop_id=? AND item_class=?', (shop,item)).fetchone()
                    if row and self.stale_catalog_source(row[0]):result['status']='catalog_changed'
                    return result
                if op == '/shops/purchase':
                    self.need(db, 'locations', text(data,'location_id'))
                    receipt=acq.purchase(db, acq.ServerProfileWallet(), principal, data, key)
                    # Claude 2026-10-14: an offer made from another (or no longer verified) catalog is refused;
                    # its ship/item decision below would use a catalog it was not made from. mutate() rolls back.
                    source=receipt['offer']['source_id']
                    if self.stale_catalog_source(source):
                        raise Problem(409,'Каталог игры изменился или недоступен. Подтвердите локальное предложение по текущему каталогу.')
                    candidates=[o for o in self.game_catalog.lookup.values() if o and o['item_class']==data.get('item_class') and o['kind']=='ship']
                    if candidates:
                        for _ in range(data['quantity']):
                            if db.execute('SELECT COUNT(*) FROM ships').fetchone()[0]>=256:
                                raise Problem(409,'Флот достиг лимита 256 кораблей. Покупка отменена.')
                            ship_id=uuid.uuid4().hex
                            db.execute("INSERT INTO ships VALUES(?,?,?,'stored',?,NULL,1)",(ship_id,data['item_class'],data['item_class'],data['location_id']))
                        receipt['owned_fleet_added']=data['quantity']
                        db.execute('UPDATE acq_purchase SET receipt=? WHERE idem_key=?',(json.dumps(receipt),key))
                    return receipt
            except acq.AcquisitionError as error:
                raise Problem(error.http_status, str(error)) from error
            raise Problem(404, 'Unknown shop operation.')
        if op == '/bridge/recover':
            if sys.platform != 'win32':
                raise Problem(409, 'Восстановление требует Windows-сервера на компьютере с игрой.')
            running = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq StarCitizen.exe', '/FO', 'CSV', '/NH'], capture_output=True, text=True, check=True)
            if 'starcitizen.exe' in running.stdout.lower():
                raise Problem(409, 'Перед восстановлением закройте Star Citizen.')
            if data.get('confirm_session_ended') is not True:
                raise Problem(400, 'Подтвердите завершение игровой сессии.')
            db.execute("UPDATE ships SET state='stored',location_id='offline-home',hangar_id=NULL,version=version+1 WHERE id IN (SELECT ship_id FROM bridge_ops WHERE status IN ('reserved','confirmed')) AND (state='deployed' OR id IN (SELECT ship_id FROM bridge_ops WHERE status='reserved'))")
            db.execute("UPDATE bridge_ops SET status='cancelled',reason='User recovered after game exit' WHERE status='reserved'")
            return {'recovered': True, 'note': 'Учёт кораблей возвращён в домашнее хранилище. Физические корабли не восстановлены.'}
        if op == '/bridge/reserve':
            ship = self.need(db, 'ships', text(data, 'ship_id'))
            version = integer(data, 'expected_version', 1, 2**53-1)
            identifier = text(data, 'operation_id', 64)
            if len(identifier) != 32 or any(c not in '0123456789abcdef' for c in identifier):
                raise Problem(400, 'operation_id must be 32 lowercase hex characters.')
            if ship['state'] != 'stored' or ship['version'] != version:
                raise Problem(409, 'Корабль не на хранении или версия записи изменилась. Обновите флот.')
            if db.execute("SELECT 1 FROM bridge_ops WHERE ship_id=? AND status='reserved'", (ship['id'],)).fetchone():
                raise Problem(409, 'Для корабля уже есть незавершённое резервирование.')
            db.execute("INSERT INTO bridge_ops(id,ship_id,status) VALUES(?,?,'reserved')", (identifier, ship['id']))
            return {'operation_id': identifier, 'status': 'reserved'}
        if op in ('/bridge/confirm', '/bridge/cancel'):
            identifier = text(data, 'operation_id', 64)
            row = db.execute('SELECT * FROM bridge_ops WHERE id=?', (identifier,)).fetchone()
            if not row:
                raise Problem(404, 'Резервирование корабля не найдено.')
            if row['status'] != 'reserved':
                raise Problem(409, 'Резервирование уже завершено. Повторяйте с исходным ключом запроса.')
            if op == '/bridge/confirm':
                entity = text(data, 'entity_id', 20)
                if not entity.isascii() or not entity.isdecimal() or not 0 < int(entity) < 2**64:
                    raise Problem(400, 'A nonzero 64-bit entity ID is required.')
                db.execute("UPDATE ships SET state='deployed',location_id=NULL,hangar_id=NULL,version=version+1 WHERE id=?", (row['ship_id'],))
                db.execute("UPDATE bridge_ops SET status='confirmed',entity_id=? WHERE id=?", (entity, identifier))
                return {'operation_id': identifier, 'status': 'confirmed'}
            reason = text(data, 'reason', 200)
            db.execute("UPDATE bridge_ops SET status='cancelled',reason=? WHERE id=?", (reason, identifier))
            return {'operation_id': identifier, 'status': 'cancelled'}
        if op == '/profile':
            name = text(data, 'name', 80)
            db.execute('UPDATE profile SET name=? WHERE id=1', (name,))
            return {'name': name}
        if op == '/wallet/adjust':
            delta = integer(data, 'delta', -MAX_CREDITS, MAX_CREDITS)
            reason = text(data, 'reason', 200)
            balance = db.execute('SELECT credits FROM profile WHERE id=1').fetchone()[0] + delta
            if not 0 <= balance <= MAX_CREDITS:
                raise Problem(409, 'Недостаточно кредитов или превышен предел баланса.')
            db.execute('UPDATE profile SET credits=? WHERE id=1', (balance,))
            return {'credits': balance, 'delta': delta, 'reason': reason}
        if op == '/locations':
            identifier, name = text(data, 'id'), text(data, 'name')
            db.execute('INSERT INTO locations VALUES(?,?)', (identifier, name))
            return {'id': identifier, 'name': name}
        if op == '/hangars':
            loc = text(data, 'location_id')
            self.need(db, 'locations', loc)
            identifier, name = str(uuid.uuid4()), text(data, 'name')
            db.execute('INSERT INTO hangars VALUES(?,?,?)', (identifier, name, loc))
            return {'id': identifier, 'name': name, 'location_id': loc}
        if op == '/ships':
            loc = text(data, 'location_id')
            self.need(db, 'locations', loc)
            if db.execute('SELECT COUNT(*) FROM ships').fetchone()[0] >= 256:  # same limit as purchase and bridge
                raise Problem(409, 'Флот достиг лимита 256 кораблей.')
            identifier = str(uuid.uuid4())
            db.execute('INSERT INTO ships(id,class_name,name,state,location_id) VALUES(?,?,?,\'stored\',?)',
                       (identifier, text(data, 'class_name', 200), text(data, 'name'), loc))
            return self.need(db, 'ships', identifier)
        parts = op.strip('/').split('/')
        if len(parts) == 3 and parts[0] == 'ships' and parts[2] == 'delete':
            ship = self.need(db, 'ships', parts[1])
            version = integer(data, 'expected_version', 1, 2**53-1)
            if version != ship['version']:
                raise Problem(409, 'Запись корабля изменилась. Обновите страницу и повторите.')
            if data.get('confirm_delete') is not True:
                raise Problem(400, 'Подтвердите удаление записи корабля.')
            if ship['state'] != 'stored' or db.execute("SELECT 1 FROM bridge_ops WHERE ship_id=? AND status='reserved'", (ship['id'],)).fetchone():
                raise Problem(409, 'Удаление доступно только для корабля на хранении без незавершённого создания. После выхода используйте Recover-Bridge.cmd.')
            history = [dict(r) for r in db.execute('SELECT * FROM bridge_ops WHERE ship_id=?', (ship['id'],))]
            db.execute('DELETE FROM bridge_ops WHERE ship_id=?', (ship['id'],))
            db.execute('DELETE FROM ships WHERE id=?', (ship['id'],))
            # mutate writes this archive into events in the same transaction.
            return {'deleted': ship, 'bridge_history': history}
        if len(parts) == 3 and parts[0] == 'ships' and parts[2] == 'transition':
            ship = self.need(db, 'ships', parts[1])
            if ship['state'] == 'deployed' and db.execute("SELECT 1 FROM bridge_ops WHERE ship_id=? AND status='confirmed'", (ship['id'],)).fetchone():
                raise Problem(409, 'Корабль создан в игре. Восстановление учёта доступно после выхода; хранение в игровом мире ещё не реализовано.')
            if db.execute("SELECT 1 FROM bridge_ops WHERE ship_id=? AND status='reserved'", (ship['id'],)).fetchone():
                raise Problem(409, 'Создание корабля не подтверждено. Не меняйте запись до восстановления.')
            version = integer(data, 'expected_version', 1, 2**53-1)
            if version != ship['version']:
                raise Problem(409, 'Запись корабля изменилась. Обновите страницу и повторите.')
            action = text(data, 'action')
            if action == 'deploy' and ship['state'] == 'stored':
                db.execute("UPDATE ships SET state='deployed',location_id=NULL,hangar_id=NULL,version=version+1 WHERE id=?", (ship['id'],))
            elif action == 'store' and ship['state'] == 'deployed':
                hangar = self.need(db, 'hangars', text(data, 'hangar_id'))
                if db.execute('SELECT 1 FROM ships WHERE hangar_id=?', (hangar['id'],)).fetchone():
                    raise Problem(409, 'В записи ангара уже есть корабль.')
                db.execute("UPDATE ships SET state='stored',location_id=?,hangar_id=?,version=version+1 WHERE id=?",
                           (hangar['location_id'], hangar['id'], ship['id']))
            else:
                raise Problem(409, 'Allowed transitions: stored -> deploy, deployed -> store.')
            return self.need(db, 'ships', ship['id'])
        if op == '/inventory/adjust':
            item, loc = text(data, 'item_class', 200), text(data, 'location_id')
            self.need(db, 'locations', loc)
            delta = integer(data, 'delta', -1_000_000, 1_000_000)
            old = db.execute('SELECT quantity FROM inventory WHERE item_class=? AND location_id=?', (item, loc)).fetchone()
            quantity = (old[0] if old else 0) + delta
            if not 0 <= quantity <= 1_000_000:
                raise Problem(409, 'Недостаточно предметов или превышен предел количества.')
            db.execute('INSERT INTO inventory VALUES(?,?,?) ON CONFLICT(item_class,location_id) DO UPDATE SET quantity=excluded.quantity', (item, loc, quantity))
            return {'item_class': item, 'location_id': loc, 'quantity': quantity}
        if op == '/demo':
            if db.execute('SELECT count(*) FROM events').fetchone()[0]:
                raise Problem(409, 'Пример доступен только до первого изменения. Ваши данные сохранены.')
            db.execute("UPDATE profile SET credits=100000 WHERE id=1")
            db.execute("INSERT INTO hangars VALUES('demo-hangar','Пример ангара (учёт)','offline-home')")
            db.execute("INSERT INTO ships VALUES('demo-gladius','AEGS_Gladius','Мой Gladius','stored','offline-home','demo-hangar',1)")
            db.execute("INSERT INTO inventory VALUES('demo_supply_crate','offline-home',5)")
            return {'demo': True, 'note': 'Добавлены только примерные записи сервера. Игровые объекты не созданы.'}
        raise Problem(404, 'Неизвестная операция API.')


class Server(ThreadingHTTPServer):
    daemon_threads = False  # server_close waits for active requests before process exit
    allow_reuse_address = False

    def __init__(self, address, store, token, page, catalog=None):
        self.store, self.token, self.page = store, token, page
        self.catalog = catalog or Catalog(Path(__file__).resolve().parent.parent / 'data',
                                          settle=CATALOG_SETTLE, grace=CATALOG_GRACE)
        super().__init__(address, Handler)


class Handler(BaseHTTPRequestHandler):
    server_version = 'SC-Local/' + VERSION

    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, fmt, *args):
        # Do not log request URLs, credentials, or state payloads.
        pass

    def send(self, status, data, kind='application/json; charset=utf-8', extra=None):
        payload = json.dumps(data, allow_nan=False).encode() if kind.startswith('application/json') else data
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(payload)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('X-Frame-Options', 'DENY')
        self.send_header('Referrer-Policy', 'no-referrer')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(payload)

    def guard(self, authenticate=True):
        port = self.server.server_address[1]
        hosts = {f'127.0.0.1:{port}', f'localhost:{port}'}
        if self.headers.get('Host') not in hosts:
            raise Problem(403, 'Используйте локальный адрес из окна сервера.')
        origin = self.headers.get('Origin')
        if origin is not None and origin not in {f'http://{host}' for host in hosts}:
            raise Problem(403, 'Запросы с другого сайта запрещены.')
        if authenticate:
            supplied = self.headers.get('Authorization', '')
            if not secrets.compare_digest(supplied.encode(), ('Bearer ' + self.server.token).encode()):
                raise Problem(401, 'Нужен токен локального API.')

    def send_catalog(self):
        """GET /api/v1/catalog with ETag = catalog version; If-None-Match -> 304 without a body.
        File problems arrive as status.state in a 200 body; only an unexpected error is a 503."""
        try:
            snapshot = self.server.catalog.snapshot()
        except Exception:
            logging.exception('Catalog read failed')
            return self.send(503, {'error': 'Каталог временно недоступен. Повторите позже.'})
        tag = '"' + snapshot.get('version', '') + '"'
        extra = {'ETag': tag, 'X-Catalog-Status': snapshot.get('status', {}).get('state', 'ok')}
        wanted = [t.strip() for t in self.headers.get('If-None-Match', '').split(',')]
        if snapshot.get('version') and (tag in wanted or 'W/' + tag in wanted):
            self.send_response(304)
            self.send_header('Cache-Control', 'no-store')
            for k, v in extra.items():
                self.send_header(k, v)
            self.end_headers()
            return None
        return self.send(200, snapshot, extra=extra)

    def do_GET(self):
        self.handle_request(False)

    def do_POST(self):
        self.handle_request(True)

    def handle_request(self, write):
        try:
            public = not write and self.path in ('/', '/health', '/tools', '/testing')
            self.guard(not public)
            if not write:
                if self.path == '/':
                    page = self.server.page.replace('__LOCAL_TOKEN__', self.server.token)
                    return self.send(200, page.encode(), 'text/html; charset=utf-8')
                if self.path == '/tools':
                    page=(Path(__file__).resolve().parent/'tools.html').read_text(encoding='utf-8').replace('__LOCAL_TOKEN__',self.server.token)
                    return self.send(200,page.encode(),'text/html; charset=utf-8')
                if self.path == '/testing':
                    page=(Path(__file__).resolve().parent/'testing.html').read_bytes()
                    return self.send(200,page,'text/html; charset=utf-8')
                if self.path == '/api/v1/game-catalog':
                    return self.send(200,self.server.store.game_catalog.snapshot())
                if self.path == '/api/v1/tools':
                    with closing(self.server.store.connect()) as db:
                        result=operations.snapshot(db)
                    observer=getattr(self.server,'observer',None)
                    result['logs']=observer.snapshot() if observer else {'events':[],'sources':[],'message':'Монитор журналов не подключён.'}
                    return self.send(200,result)
                if self.path == '/health':
                    with closing(self.server.store.connect()) as db:
                        db.execute('SELECT 1 FROM profile').fetchone()
                    return self.send(200, {'status': 'ok', 'version': VERSION, 'api_version': 1, 'game_connected': False})
                if self.path == '/api/v1/state':
                    return self.send(200, self.server.store.snapshot())
                if self.path == '/api/v1/catalog':
                    return self.send_catalog()
                if self.path == '/api/v1/bridge/fleet':
                    state = self.server.store.snapshot()
                    reserved = {r['ship_id'] for r in state['bridge_operations'] if r['status'] == 'reserved'}
                    if len(state['ships']) > 256:
                        raise Problem(409, 'This bridge supports at most 256 owned ships.')
                    rows = ['SCBRIDGE1']
                    for ship in state['ships']:
                        fields = [ship['id'], ship['class_name'], ship['name'],
                                  'reserved' if ship['id'] in reserved else ship['state']]
                        rows.append('\t'.join(x.encode('utf-8').hex() for x in fields) + '\t' + str(ship['version']))
                    return self.send(200, ('\n'.join(rows)+'\n').encode(), 'text/plain; charset=utf-8')
                if self.path == '/api/v1/shops':
                    return self.send(200, self.server.store.shops())
                if self.path == '/api/v1/backup':
                    return self.send(200, self.server.store.backup(), 'application/octet-stream',
                                     {'Content-Disposition': 'attachment; filename="offline-backup.sqlite3"'})
                raise Problem(404, 'Not found.')
            if not self.path.startswith('/api/v1/'):
                raise Problem(404, 'Not found.')
            if self.headers.get('Transfer-Encoding'):
                raise Problem(400, 'Chunked requests are not supported.')
            if self.headers.get('Content-Type', '').split(';')[0].strip().lower() != 'application/json':
                raise Problem(415, 'Use Content-Type: application/json.')
            try:
                size = int(self.headers.get('Content-Length', ''))
            except ValueError:
                raise Problem(411, 'Content-Length required.')
            if size < 0 or size > LIMIT:
                raise Problem(413, 'Request body exceeds 64 KiB.')
            try:
                raw = self.rfile.read(size)
                if len(raw) != size:
                    raise ValueError('Incomplete body')
                def reject_constant(value):
                    raise ValueError('Non-finite JSON numbers are not allowed')
                def finite_float(value):
                    number = float(value)
                    if not math.isfinite(number):
                        raise ValueError('Non-finite JSON number')
                    return number
                data = json.loads(raw, parse_constant=reject_constant, parse_float=finite_float)
            except (ValueError, UnicodeError, RecursionError):
                raise Problem(400, 'Некорректный JSON.')
            if self.path == '/api/v1/control/shutdown':
                if data != {'confirm': True}:
                    raise Problem(400, 'Для остановки требуется подтверждение.')
                self.send(200, {'status': 'stopping'})
                import threading
                threading.Thread(target=self.server.shutdown, daemon=True).start()
                return
            result = self.server.store.mutate(self.path[len('/api/v1'):], data, self.headers.get('Idempotency-Key'))
            self.send(200, result)
        except Problem as exc:
            self.close_connection = True
            self.send(exc.status, {'error': exc.message})
        except sqlite3.IntegrityError:
            self.send(409, {'error': 'A record already exists or conflicts with the requested state.'})
        except sqlite3.OperationalError:
            logging.exception('Database operation failed')
            self.send(503, {'error': 'Database unavailable or busy. Retry the same request key.'})
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            self.close_connection = True
        except Exception:
            logging.exception('Request failed')
            self.send(500, {'error': 'Ошибка сервера. Подробности в server.log.'})


def main():
    root = Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description='Local offline state service; no game connection yet.')
    parser.add_argument('--port', type=int, default=18870)
    parser.add_argument('--data-dir', type=Path, default=root / 'server-state')
    parser.add_argument('--pair-mod', type=Path, help='Pair with an explicitly supplied mod launcher folder')
    parser.add_argument('--open', action='store_true', help='Open control page in your browser')
    parser.add_argument('--game-log', type=Path, default=Path(r'D:\StarCitizen\LIVE\Game.log'))
    parser.add_argument('--mod-log', type=Path, default=root.parent/'data'/'mod.log')
    parser.add_argument('--link-mod', action='store_true', help='Choose your mod launcher folder and pair it with this server')
    parser.add_argument('--recover-bridge', action='store_true', help='Recover bridge ships after closing the game')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error('port must be between 1 and 65535')
    args.data_dir.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, handlers=[RotatingFileHandler(args.data_dir / 'server.log', maxBytes=1_000_000, backupCount=2)])
    token_path = args.data_dir / 'api-token.txt'
    try:
        with token_path.open('x', encoding='utf-8') as f:
            token = secrets.token_hex(32)
            f.write(token)
        token_path.chmod(0o600)
    except FileExistsError:
        token = token_path.read_text(encoding='utf-8').strip()
    if len(token) != 64 or any(c not in '0123456789abcdef' for c in token):
        raise RuntimeError('api-token.txt is invalid. Close the server and remove only that token file to regenerate it.')
    if args.link_mod or args.recover_bridge or args.pair_mod:
        if args.pair_mod and args.recover_bridge:
            parser.error('--pair-mod is pairing only; use the recovery dialog separately')
        if args.pair_mod:
            chosen = str(args.pair_mod.resolve())
        else:
            import tkinter as tk
            from tkinter import filedialog
            window = tk.Tk()
            window.withdraw()
            chosen = filedialog.askdirectory(title='Выберите папку русского лаунчера sc-offline.exe')
            window.destroy()
        if not chosen:
            print('Привязка отменена. Файлы мода не изменены.')
            return
        folder = Path(chosen)
        if not (folder / 'sc-offline.exe').is_file():
            raise RuntimeError('Выберите папку лаунчера мода, а не LIVE/Bin64.')
        target = folder / 'data' / 'bridge.ini'
        if args.recover_bridge:
            # Require the selected mod to be paired to this database's token.
            if not target.exists() or target.read_text(encoding='ascii').splitlines() != [str(args.port), token]:
                raise RuntimeError('Мод не связан с этим сервером. Восстановление не выполнено.')
            from tkinter import messagebox
            if not messagebox.askyesno('Восстановить учёт после выхода', 'Игра закрыта? Вернуть записи кораблей в домашнее хранилище и завершить журнал? Физические корабли и груз не восстанавливаются.'):
                return
            Store(args.data_dir / 'offline.sqlite3').mutate('/bridge/recover', {'confirm_session_ended': True}, secrets.token_hex(16))
            pending = folder / 'data' / 'bridge-pending.txt'
            if pending.exists():
                pending.rename(pending.with_name('bridge-recovered-'+secrets.token_hex(4)+'.txt'))
            print('Учёт восстановлен. Журнал архивирован; записи кораблей снова доступны.')
            return
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_suffix('.tmp')
        temporary.write_text(str(args.port)+'\n'+token+'\n', encoding='ascii')
        temporary.replace(target)
        print('Привязка готова. Запустите сервер, затем откройте M → Корабли → Мой флот.')
        return
    page = (Path(__file__).resolve().parent / 'dashboard.html').read_text(encoding='utf-8')
    # Bind before opening SQLite: a second launch must not modify the active database.
    server = Server(('127.0.0.1', args.port), None, token, page)
    try:
        server.store = Store(args.data_dir / 'offline.sqlite3')
        server.observer=LogObserver([args.game_log,args.mod_log],args.data_dir/'log-observer.json')
        server.observer.start()
        url = f'http://127.0.0.1:{args.port}'
        print(f'SC Local Server {VERSION}\nСтраница управления: {url}\nСохранённые данные: {args.data_dir.resolve()}\nСвязь с игрой проверяется в меню мода\nОставьте окно открытым. Ctrl+C останавливает сервер.', flush=True)
        if args.open:
            webbrowser.open(url)
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nСервер остановлен. Данные сохранены.')
    finally:
        if getattr(server,'observer',None):server.observer.close()
        server.server_close()


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(f'Не удалось запустить сервер: {exc}', file=sys.stderr)
        sys.exit(1)
