"""Local acquisition ledger: offer -> purchase -> ledger ownership -> separate native-delivery state.

Provenance: new code - Claude, 2026-10-13. Revision acq-2 after the team review (reviewer Codex,
REVIEW.ru.md remarks 1 and 4). server.py is team code and is NOT modified here.
Python 3.10+ standard library + sqlite3.

What this is:
  * a pure model: no game, bridge, hook, network or file access;
  * money, stock and ownership change in ONE SQLite write transaction, or not at all;
  * every write is a compare-and-set on an expected version or expected value;
  * every purchase carries an idempotency key: identical replay returns the first receipt,
    a different body (or another principal) under the same key is refused;
  * prices and stock come only from a registered, authoritative price source. A missing price
    or stock is "unknown" and the purchase is refused. There are no built-in prices.

ONE local balance (review remark 4):
  * the model owns NO wallet table. The only local balance is server.py's profile.credits
    (server.py:64-66, CHECK(credits BETWEEN 0 AND 9000000000000); server.py:27 MAX_CREDITS).
    Money is read and debited through a WalletPort inside the caller's SQLite transaction;
    ServerProfileWallet is that port for the profile table. Top-ups stay with server.py
    /wallet/adjust (server.py:203-210); the model never credits except in migrate_acq1.
  * one limit: MAX_CREDITS = 9e12, inclusive, same as server.py. 9e12 < 2**53, so every balance
    is exact as a JSON number in a browser (IEEE double) and fits SQLite INTEGER.
  * the buyer is a Principal that the server builds from its authenticated context
    (principal_from_server_auth). owner / wallet / profile fields in a request body are refused.

Delivery (review remark 1). Allowed transitions; the DB triggers enforce the same table:
      not_requested --request_delivery------> pending
      not_sent      --request_delivery------> pending      (the ONLY retry)
      pending       --mark_not_sent(cause)--> not_sent     (refused before any native call)
      pending       --begin_dispatch--------> unconfirmed  (commit BEFORE the native call)
      unconfirmed   --confirm_delivery------> confirmed    (needs an admitted contract; none today)
  Write-ahead rule for a future dispatcher: commit begin_dispatch, and only then make the native
  call. So a row still in 'pending' means no native call was made for it, and 'not_sent' (reachable
  only from pending) means "proved not dispatched". After dispatch every outcome is uncertain,
  including a native error, so it stays 'unconfirmed'. 'unconfirmed' is terminal for retries:
  no path leads back to pending or not_sent. There is no 'failed' state and no operator
  "reconciliation" that reopens a retry: a person's note cannot prove that no item was created
  (a spawn may land later). The only way out of 'unconfirmed' is 'confirmed' through a contract.
  A refund for a lost item would be a separate future money operation, not a delivery state.

What this is NOT:
  * not a native shop transaction: LOCAL_CREDIT is never the game's aUEC. A native aUEC reading
    can only be stored as a separate observation and is never debited, credited or summed.
  * not a delivery: CONFIRMED_DELIVERY_CONTRACTS is EMPTY because no public, game-proved contract
    for putting an item into pockets / location storage / ship cargo / equipment exists.

Transactions: functions that write require an open transaction on `db` (BEGIN IMMEDIATE), like
server.py Store.mutate (server.py:139-156). Use `transaction(db)` when calling standalone.
Connections must use isolation_level=None and row_factory=sqlite3.Row, as server.py:100-104 does.
"""
from __future__ import annotations

from contextlib import contextmanager
import hashlib
import json
import re
import sqlite3
from typing import Protocol, runtime_checkable
import uuid

SCHEMA_ID = 'acq-2'
LEGACY_SCHEMA_ID = 'acq-1'
# The one balance limit, inclusive. Same value and meaning as server.py:27 MAX_CREDITS and the
# profile CHECK at server.py:66. Exact in an IEEE double (< 2**53).
MAX_CREDITS = 9_000_000_000_000
MAX_QUANTITY = 1000
MAX_STOCK = 1_000_000
MAX_VERSION = 2**53 - 1
LOCAL_CURRENCY = 'LOCAL_CREDIT'
SERVER_PROFILE_ID = 1          # server.py:65: id INTEGER PRIMARY KEY CHECK(id=1) - one local profile

SOURCE_KINDS = ('owner_declared', 'imported_file', 'native_observed')
DELIVERY_TARGETS = ('pocket', 'location_storage', 'ship_cargo', 'equipped', 'world_drop')
DELIVERY_STATES = ('not_requested', 'pending', 'not_sent', 'unconfirmed', 'confirmed')
NOT_SENT_CAUSES = ('validation_refused', 'contract_unavailable', 'cancelled_before_dispatch')
DELIVERY_TRANSITIONS = frozenset({('not_requested', 'pending'), ('not_sent', 'pending'),
                                  ('pending', 'not_sent'), ('pending', 'unconfirmed'),
                                  ('unconfirmed', 'confirmed')})
NO_RETRY_STATES = ('unconfirmed', 'confirmed')
MIGRATION_DECISIONS = ('merge_into_profile', 'discard_acq_wallet')
# Request-body fields that would name the buyer or the money. Identity comes from the session.
IDENTITY_FIELDS = frozenset({'owner', 'wallet', 'wallet_id', 'profile', 'profile_id', 'principal',
                             'account', 'user', 'user_id'})

# Contract names that may move a delivery to "confirmed". EMPTY on purpose: see module docstring.
# Add a name only after the contract is published AND proved in game; tests pin it empty.
CONFIRMED_DELIVERY_CONTRACTS: frozenset = frozenset()

_CLASS_RE = re.compile(r'[A-Za-z0-9_]{1,95}')        # same alphabet as sc_loadout.h item_class
_ID_RE = re.compile(r'[A-Za-z0-9_.:-]{1,64}')
_TABLES = ('acq_meta', 'acq_price_source', 'acq_offer', 'acq_native_wallet_observation', 'acq_holding',
           'acq_purchase', 'acq_delivery', 'acq_delivery_log', 'acq_migration')

SCHEMA_SQL = """\
-- acquisition model schema acq-2 (new code - Claude). Own acq_ prefix; does not touch PRAGMA user_version.
-- No wallet table: the only local balance is server.py profile.credits, used through a WalletPort.
CREATE TABLE IF NOT EXISTS acq_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS acq_price_source(
    id TEXT PRIMARY KEY,
    kind TEXT NOT NULL CHECK(kind IN ('owner_declared','imported_file','native_observed')),
    authoritative INTEGER NOT NULL CHECK(authoritative IN (0,1)),
    label TEXT NOT NULL,
    CHECK(kind <> 'native_observed' OR authoritative = 0));
CREATE TABLE IF NOT EXISTS acq_offer(
    shop_id TEXT NOT NULL,
    item_class TEXT NOT NULL,
    source_id TEXT NOT NULL REFERENCES acq_price_source(id),
    currency TEXT NOT NULL CHECK(currency = 'LOCAL_CREDIT'),
    unit_price INTEGER CHECK(unit_price IS NULL OR unit_price BETWEEN 1 AND 9000000000000),
    stock INTEGER CHECK(stock IS NULL OR stock BETWEEN 0 AND 1000000),
    version INTEGER NOT NULL DEFAULT 1 CHECK(version >= 1),
    PRIMARY KEY(shop_id, item_class));
CREATE TABLE IF NOT EXISTS acq_native_wallet_observation(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    observed_balance INTEGER NOT NULL CHECK(observed_balance BETWEEN 0 AND 9000000000000),
    source TEXT NOT NULL,
    observed_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));
CREATE TABLE IF NOT EXISTS acq_holding(
    profile_id INTEGER NOT NULL CHECK(profile_id >= 1),
    item_class TEXT NOT NULL,
    location_id TEXT NOT NULL,
    quantity INTEGER NOT NULL CHECK(quantity BETWEEN 0 AND 1000000),
    version INTEGER NOT NULL DEFAULT 1 CHECK(version >= 1),
    PRIMARY KEY(profile_id, item_class, location_id));
CREATE TABLE IF NOT EXISTS acq_purchase(
    id TEXT PRIMARY KEY,
    idem_key TEXT NOT NULL UNIQUE,
    digest TEXT NOT NULL,
    profile_id INTEGER NOT NULL CHECK(profile_id >= 1),
    balance_source TEXT NOT NULL,
    shop_id TEXT NOT NULL,
    item_class TEXT NOT NULL,
    quantity INTEGER NOT NULL CHECK(quantity BETWEEN 1 AND 1000),
    unit_price INTEGER NOT NULL CHECK(unit_price BETWEEN 1 AND 9000000000000),
    total INTEGER NOT NULL CHECK(total = unit_price * quantity AND total <= 9000000000000),
    source_id TEXT NOT NULL REFERENCES acq_price_source(id),
    offer_version INTEGER NOT NULL,
    credits_before INTEGER NOT NULL CHECK(credits_before BETWEEN 0 AND 9000000000000),
    credits_after INTEGER NOT NULL CHECK(credits_after = credits_before - total AND credits_after >= 0),
    stock_before INTEGER NOT NULL,
    stock_after INTEGER NOT NULL CHECK(stock_after = stock_before - quantity AND stock_after >= 0),
    location_id TEXT NOT NULL,
    receipt TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));
CREATE TABLE IF NOT EXISTS acq_delivery(
    purchase_id TEXT PRIMARY KEY REFERENCES acq_purchase(id),
    target TEXT CHECK(target IS NULL OR target IN ('pocket','location_storage','ship_cargo','equipped','world_drop')),
    state TEXT NOT NULL CHECK(state IN ('not_requested','pending','not_sent','unconfirmed','confirmed')),
    cause TEXT CHECK(cause IS NULL OR cause IN ('validation_refused','contract_unavailable','cancelled_before_dispatch')),
    contract TEXT,
    evidence TEXT,
    version INTEGER NOT NULL DEFAULT 1 CHECK(version >= 1),
    CHECK(state = 'not_requested' OR target IS NOT NULL),
    CHECK((state = 'not_sent') = (cause IS NOT NULL)),
    CHECK(state <> 'confirmed' OR (contract IS NOT NULL AND evidence IS NOT NULL)),
    CHECK(state = 'confirmed' OR (contract IS NULL AND evidence IS NULL)));
CREATE TABLE IF NOT EXISTS acq_delivery_log(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    purchase_id TEXT NOT NULL REFERENCES acq_purchase(id),
    from_state TEXT NOT NULL,
    to_state TEXT NOT NULL,
    version INTEGER NOT NULL,
    cause TEXT,
    at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));
CREATE TABLE IF NOT EXISTS acq_migration(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    from_schema TEXT NOT NULL,
    to_schema TEXT NOT NULL,
    action TEXT NOT NULL CHECK(action IN ('empty','merge_into_profile','discard_acq_wallet')),
    acq_wallet_total INTEGER NOT NULL CHECK(acq_wallet_total >= 0),
    credits_before INTEGER NOT NULL,
    credits_after INTEGER NOT NULL,
    detail TEXT NOT NULL,
    at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));
-- A delivery row is created once, as not_requested, and is never deleted (INSERT OR REPLACE included).
CREATE TRIGGER IF NOT EXISTS acq_delivery_insert BEFORE INSERT ON acq_delivery
WHEN NEW.state IS NOT 'not_requested' OR NEW.version IS NOT 1 OR NEW.target IS NOT NULL
    OR EXISTS(SELECT 1 FROM acq_delivery WHERE purchase_id = NEW.purchase_id)
BEGIN SELECT RAISE(ABORT, 'acq_delivery: a delivery starts once, as not_requested'); END;
CREATE TRIGGER IF NOT EXISTS acq_delivery_no_delete BEFORE DELETE ON acq_delivery
BEGIN SELECT RAISE(ABORT, 'acq_delivery: rows are never deleted'); END;
-- The allowed transitions. unconfirmed never goes back to pending or not_sent.
CREATE TRIGGER IF NOT EXISTS acq_delivery_transition BEFORE UPDATE ON acq_delivery
WHEN NOT COALESCE((NEW.purchase_id IS OLD.purchase_id AND NEW.version IS OLD.version + 1 AND (
       (OLD.state = 'not_requested' AND NEW.state = 'pending')
    OR (OLD.state = 'not_sent' AND NEW.state = 'pending')
    OR (OLD.state = 'pending' AND NEW.state = 'not_sent' AND NEW.target IS OLD.target)
    OR (OLD.state = 'pending' AND NEW.state = 'unconfirmed' AND NEW.target IS OLD.target)
    OR (OLD.state = 'unconfirmed' AND NEW.state = 'confirmed' AND NEW.target IS OLD.target))), 0)
BEGIN SELECT RAISE(ABORT, 'acq_delivery: transition not allowed'); END;
CREATE TRIGGER IF NOT EXISTS acq_delivery_log_update AFTER UPDATE ON acq_delivery
BEGIN INSERT INTO acq_delivery_log(purchase_id, from_state, to_state, version, cause) VALUES(NEW.purchase_id, OLD.state, NEW.state, NEW.version, NEW.cause); END;
CREATE TRIGGER IF NOT EXISTS acq_delivery_log_no_update BEFORE UPDATE ON acq_delivery_log
BEGIN SELECT RAISE(ABORT, 'acq_delivery_log is append-only'); END;
CREATE TRIGGER IF NOT EXISTS acq_delivery_log_no_delete BEFORE DELETE ON acq_delivery_log
BEGIN SELECT RAISE(ABORT, 'acq_delivery_log is append-only'); END;
INSERT OR IGNORE INTO acq_meta(key, value) VALUES('schema', 'acq-2');
"""

_HTTP = {'BAD_REQUEST': 400, 'IDENTITY_IN_BODY': 400, 'AUTH': 401, 'NOT_FOUND': 404, 'OFFER_NOT_FOUND': 404,
         'WALLET_NOT_FOUND': 404, 'NO_TRANSACTION': 500, 'SCHEMA': 500}


class AcquisitionError(Exception):
    """A refusal. Nothing was written when it is raised inside `transaction` (rolled back).

    code: BAD_REQUEST, IDENTITY_IN_BODY, AUTH, KEY_REUSED, OFFER_NOT_FOUND, WALLET_NOT_FOUND,
    NOT_FOUND, SOURCE_NOT_AUTHORITATIVE, PRICE_UNKNOWN, STOCK_UNKNOWN, VERSION_CONFLICT,
    PRICE_CHANGED, OUT_OF_STOCK, INSUFFICIENT_FUNDS, LIMIT, BAD_STATE, CONTRACT_NOT_CONFIRMED,
    MIGRATION_DECISION_NEEDED, MIGRATION_REFUSED, NO_TRANSACTION, SCHEMA.
    http_status: 400 / 401 / 404 / 409 / 500, for a server.py Problem (server.py:34-36).
    """

    def __init__(self, code, message):
        super().__init__(f'{code}: {message}')
        self.code, self.message = code, message
        self.http_status = _HTTP.get(code, 409)


def connect(path):
    """Same connection settings as server.py Store.connect (server.py:100-104)."""
    db = sqlite3.connect(path, timeout=10, isolation_level=None)
    db.row_factory = sqlite3.Row
    db.execute('PRAGMA foreign_keys=ON')
    return db


@contextmanager
def transaction(db):
    """BEGIN IMMEDIATE ... COMMIT, ROLLBACK on any exception (the write lock is taken up front)."""
    if db.in_transaction:
        raise AcquisitionError('NO_TRANSACTION', 'a transaction is already open on this connection')
    db.execute('BEGIN IMMEDIATE')
    try:
        yield db
    except BaseException:
        if db.in_transaction:
            db.execute('ROLLBACK')
        raise
    db.execute('COMMIT')


def _table_exists(db, name):
    return db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (name,)).fetchone() is not None


def _schema_value(db):
    if not _table_exists(db, 'acq_meta'):
        return None
    row = db.execute("SELECT value FROM acq_meta WHERE key='schema'").fetchone()
    return row[0] if row else None


def _create_schema(db):
    buffer = ''
    for line in SCHEMA_SQL.splitlines(keepends=True):
        if line.startswith('--'):
            continue
        buffer += line
        if sqlite3.complete_statement(buffer):
            db.execute(buffer.strip())
            buffer = ''
    if buffer.strip():
        raise AcquisitionError('SCHEMA', 'incomplete statement in SCHEMA_SQL')


def ensure_schema(db):
    """Create the acq_ tables (idempotent). Refuses a database holding another acq schema
    (acq-1 must go through migrate_acq1 first) and never creates server.py's profile table."""
    own = not db.in_transaction
    if own:
        db.execute('BEGIN IMMEDIATE')
    try:
        found = _schema_value(db)
        if found is not None and found != SCHEMA_ID:
            hint = ' Run migrate_acq1 with an explicit decision.' if found == LEGACY_SCHEMA_ID else ''
            raise AcquisitionError('SCHEMA', f'acq schema is {found!r}, expected {SCHEMA_ID!r}.{hint}')
        if _table_exists(db, 'acq_wallet'):
            raise AcquisitionError('SCHEMA', 'a legacy acq_wallet table exists; run migrate_acq1')
        _create_schema(db)
        if _schema_value(db) != SCHEMA_ID:
            raise AcquisitionError('SCHEMA', 'acq schema marker missing')
        if own:
            db.execute('COMMIT')
    except BaseException:
        if own and db.in_transaction:
            db.execute('ROLLBACK')
        raise


# ---- validation ------------------------------------------------------------------------------

def _need_tx(db):
    if not db.in_transaction:
        raise AcquisitionError('NO_TRANSACTION', 'open a write transaction first (transaction(db) or BEGIN IMMEDIATE)')


def _int(data, key, low, high):
    value = data.get(key)
    if type(value) is not int or not low <= value <= high:
        raise AcquisitionError('BAD_REQUEST', f'{key} must be an integer in [{low}, {high}]')
    return value


def _opt_int(data, key, low, high):
    if data.get(key) is None:
        return None
    return _int(data, key, low, high)


def _ident(data, key, pattern=_ID_RE):
    value = data.get(key)
    if not isinstance(value, str) or not pattern.fullmatch(value):
        raise AcquisitionError('BAD_REQUEST', f'{key} has an invalid format')
    return value


def _text(data, key, limit=200):
    value = data.get(key)
    if not isinstance(value, str) or not value.strip() or len(value) > limit or any(ord(c) < 32 for c in value):
        raise AcquisitionError('BAD_REQUEST', f'{key} must be 1..{limit} printable characters')
    return value.strip()


def _key(key):
    if not isinstance(key, str) or not 1 <= len(key) <= 100 or not all(33 <= ord(c) <= 126 for c in key):
        raise AcquisitionError('BAD_REQUEST', 'idempotency key must be 1..100 printable ASCII characters')
    return key


def _only(data, allowed):
    if not isinstance(data, dict):
        raise AcquisitionError('BAD_REQUEST', 'request must be a JSON object')
    extra = set(data) - set(allowed)
    if extra:
        raise AcquisitionError('BAD_REQUEST', f'unknown fields: {sorted(extra)}')


def _digest(kind, data):
    canonical = json.dumps(data, sort_keys=True, separators=(',', ':'), allow_nan=False)
    return hashlib.sha256((kind + '\n' + canonical).encode()).hexdigest()


# ---- who buys and which balance pays ----------------------------------------------------------

class Principal:
    """The authenticated buyer. Built by the server from its own context, never from a body.
    Immutable after construction."""
    __slots__ = ('profile_id',)

    def __init__(self, profile_id):
        if type(profile_id) is not int or not 1 <= profile_id <= MAX_VERSION:
            raise AcquisitionError('AUTH', 'principal profile_id must be a positive integer')
        object.__setattr__(self, 'profile_id', profile_id)

    def __setattr__(self, name, value):
        raise AttributeError('Principal is immutable')

    def __repr__(self):
        return f'Principal(profile_id={self.profile_id})'


def principal_from_server_auth(authenticated):
    """For server.py: call only after Handler.guard(authenticate=True) passed (server.py:322-333).
    server.py has one bearer token and one profile row id=1 (server.py:65), so an authenticated
    request acts for profile 1. Anything but True is refused."""
    if authenticated is not True:
        raise AcquisitionError('AUTH', 'request is not authenticated')
    return Principal(SERVER_PROFILE_ID)


def _principal(principal):
    if type(principal) is not Principal:
        raise AcquisitionError('AUTH', 'a Principal from the authenticated server context is required')
    return principal


@runtime_checkable
class WalletPort(Protocol):
    """The single local balance, used inside the caller's SQLite transaction on the same `db`.

    limit:  inclusive upper bound; must equal MAX_CREDITS (one limit for the whole server).
    source: a short name of the balance, shown in receipts (e.g. 'profile.credits').
    read(db, principal) -> int                         current balance, 0..limit
    debit(db, principal, amount, expected) -> int      compare-and-set: balance must equal expected
    credit(db, principal, amount, expected) -> int     compare-and-set; used only by migrate_acq1
    """
    limit: int
    source: str

    def read(self, db, principal) -> int: ...

    def debit(self, db, principal, amount, expected_balance) -> int: ...

    def credit(self, db, principal, amount, expected_balance) -> int: ...


class ServerProfileWallet:
    """WalletPort over server.py's profile table (server.py:64-67): id INTEGER PRIMARY KEY
    CHECK(id=1), name TEXT NOT NULL, credits INTEGER NOT NULL CHECK(credits BETWEEN 0 AND
    9000000000000). The table has no version column, so the compare-and-set token is the credits
    value itself (server.py /wallet/adjust, server.py:203-210, changes only that value).
    The model never creates this table; the server does."""
    limit = MAX_CREDITS
    source = 'profile.credits'
    _CHECK = 'CHECK(credits BETWEEN 0 AND 9000000000000)'

    def check_shape(self, db):
        row = db.execute("SELECT sql FROM sqlite_master WHERE type='table' AND name='profile'").fetchone()
        if row is None:
            raise AcquisitionError('SCHEMA', 'server profile table not found (server.py creates it)')
        cols = {r[1]: (str(r[2]).upper(), r[3], r[5]) for r in db.execute('PRAGMA table_info(profile)')}
        if (cols.get('id', (None,))[0] != 'INTEGER' or cols['id'][2] != 1
                or cols.get('credits') != ('INTEGER', 1, 0) or self._CHECK not in ' '.join(row[0].split())):
            raise AcquisitionError('SCHEMA', 'profile table does not match server.py (id, credits, 9e12 CHECK)')

    def read(self, db, principal):
        principal = _principal(principal)
        self.check_shape(db)
        row = db.execute('SELECT credits FROM profile WHERE id=?', (principal.profile_id,)).fetchone()
        if row is None:
            raise AcquisitionError('WALLET_NOT_FOUND', 'no local profile for this principal')
        value = row[0]
        if type(value) is not int or not 0 <= value <= self.limit:
            raise AcquisitionError('SCHEMA', 'profile.credits is outside [0, 9e12]')
        return value

    def _cas(self, db, principal, amount, expected_balance, sign):
        _need_tx(db)
        principal = _principal(principal)
        data = {'amount': amount, 'expected_balance': expected_balance}
        _int(data, 'amount', 1, self.limit)
        _int(data, 'expected_balance', 0, self.limit)
        after = expected_balance + sign * amount
        if after < 0:
            raise AcquisitionError('INSUFFICIENT_FUNDS', 'not enough local credits')
        if after > self.limit:
            raise AcquisitionError('LIMIT', 'balance would exceed 9e12')
        cur = db.execute('UPDATE profile SET credits=? WHERE id=? AND credits=?',
                         (after, principal.profile_id, expected_balance))
        if cur.rowcount != 1:
            raise AcquisitionError('VERSION_CONFLICT', 'local balance changed; re-read it')
        return after

    def debit(self, db, principal, amount, expected_balance):
        return self._cas(db, principal, amount, expected_balance, -1)

    def credit(self, db, principal, amount, expected_balance):
        return self._cas(db, principal, amount, expected_balance, +1)


def _wallet(wallet):
    if not isinstance(wallet, WalletPort):
        raise AcquisitionError('SCHEMA', 'wallet must implement WalletPort')
    if wallet.limit != MAX_CREDITS:
        raise AcquisitionError('SCHEMA', 'wallet limit differs from MAX_CREDITS; one limit only')
    return wallet


# ---- price sources and offers ----------------------------------------------------------------

def register_source(db, source_id, kind, authoritative, label):
    """A price/stock authority. native_observed can never be authoritative (no confirmed native
    shop catalog contract); owner_declared / imported_file are authoritative only when said so."""
    _need_tx(db)
    data = {'id': source_id, 'kind': kind, 'authoritative': authoritative, 'label': label}
    _ident(data, 'id')
    if kind not in SOURCE_KINDS:
        raise AcquisitionError('BAD_REQUEST', f'kind must be one of {SOURCE_KINDS}')
    if type(authoritative) is not bool:
        raise AcquisitionError('BAD_REQUEST', 'authoritative must be true or false')
    if kind == 'native_observed' and authoritative:
        raise AcquisitionError('SOURCE_NOT_AUTHORITATIVE', 'a native observation cannot be a price authority')
    db.execute('INSERT INTO acq_price_source(id, kind, authoritative, label) VALUES(?,?,?,?)',
               (source_id, kind, int(authoritative), _text(data, 'label')))
    return {'id': source_id, 'kind': kind, 'authoritative': authoritative}


def put_offer(db, source_id, shop_id, item_class, unit_price, stock, expected_version=None):
    """Create (expected_version None) or replace an offer. unit_price / stock None = unknown.
    Every change bumps the version, so a buyer holding an old quote is refused."""
    _need_tx(db)
    data = {'source_id': source_id, 'shop_id': shop_id, 'item_class': item_class,
            'unit_price': unit_price, 'stock': stock}
    _ident(data, 'source_id')
    _ident(data, 'shop_id')
    _ident(data, 'item_class', _CLASS_RE)
    price = _opt_int(data, 'unit_price', 1, MAX_CREDITS)
    count = _opt_int(data, 'stock', 0, MAX_STOCK)
    if db.execute('SELECT 1 FROM acq_price_source WHERE id=?', (source_id,)).fetchone() is None:
        raise AcquisitionError('NOT_FOUND', 'price source not registered')
    row = db.execute('SELECT version FROM acq_offer WHERE shop_id=? AND item_class=?', (shop_id, item_class)).fetchone()
    if expected_version is None:
        if row is not None:
            raise AcquisitionError('VERSION_CONFLICT', 'offer exists; pass its expected_version')
        db.execute('INSERT INTO acq_offer(shop_id, item_class, source_id, currency, unit_price, stock) VALUES(?,?,?,?,?,?)',
                   (shop_id, item_class, source_id, LOCAL_CURRENCY, price, count))
        return {'shop_id': shop_id, 'item_class': item_class, 'version': 1}
    if type(expected_version) is not int or row is None or row[0] != expected_version:
        raise AcquisitionError('VERSION_CONFLICT', 'offer changed or missing; re-read it')
    cur = db.execute('UPDATE acq_offer SET source_id=?, unit_price=?, stock=?, version=version+1 '
                     'WHERE shop_id=? AND item_class=? AND version=?',
                     (source_id, price, count, shop_id, item_class, expected_version))
    if cur.rowcount != 1:
        raise AcquisitionError('VERSION_CONFLICT', 'offer changed; re-read it')
    return {'shop_id': shop_id, 'item_class': item_class, 'version': expected_version + 1}


def import_offer_document(db, doc):
    """Import {"format":"acq-offers-1","source":{id,kind,authoritative,label},"offers":[...]}.
    Each offer: shop_id, item_class, unit_price (int or null), stock (int or null). Creates only."""
    _need_tx(db)
    if not isinstance(doc, dict) or doc.get('format') != 'acq-offers-1':
        raise AcquisitionError('BAD_REQUEST', 'format must be acq-offers-1')
    src = doc.get('source')
    offers = doc.get('offers')
    if not isinstance(src, dict) or not isinstance(offers, list) or len(offers) > 10000:
        raise AcquisitionError('BAD_REQUEST', 'source object and offers list (<= 10000) required')
    register_source(db, src.get('id'), src.get('kind'), src.get('authoritative'), src.get('label'))
    for offer in offers:
        _only(offer, ('shop_id', 'item_class', 'unit_price', 'stock'))
        put_offer(db, src['id'], offer.get('shop_id'), offer.get('item_class'),
                  offer.get('unit_price'), offer.get('stock'))
    return {'source_id': src['id'], 'offers': len(offers)}


def quote(db, shop_id, item_class):
    """Read-only availability: status is one of not_listed, not_authoritative, unknown_price,
    unknown_stock, out_of_stock, available. Price/stock are returned only when known."""
    row = db.execute('SELECT o.*, s.authoritative FROM acq_offer o JOIN acq_price_source s ON s.id=o.source_id '
                     'WHERE o.shop_id=? AND o.item_class=?', (shop_id, item_class)).fetchone()
    if row is None:
        return {'status': 'not_listed', 'shop_id': shop_id, 'item_class': item_class}
    result = {'shop_id': shop_id, 'item_class': item_class, 'source_id': row['source_id'],
              'currency': row['currency'], 'version': row['version'],
              'unit_price': row['unit_price'], 'stock': row['stock']}
    if not row['authoritative']:
        result.update(status='not_authoritative', unit_price=None, stock=None)
    elif row['unit_price'] is None:
        result['status'] = 'unknown_price'
    elif row['stock'] is None:
        result['status'] = 'unknown_stock'
    elif row['stock'] == 0:
        result['status'] = 'out_of_stock'
    else:
        result['status'] = 'available'
    return result


# ---- native observation and balance report ---------------------------------------------------

def record_native_observation(db, observed_balance, source):
    """Store a copied native aUEC reading as data only. It is never debited, credited or summed
    with the local balance. `source` must say where the number came from and how it was verified."""
    _need_tx(db)
    data = {'observed_balance': observed_balance, 'source': source}
    _int(data, 'observed_balance', 0, MAX_CREDITS)
    db.execute('INSERT INTO acq_native_wallet_observation(observed_balance, source) VALUES(?,?)',
               (observed_balance, _text(data, 'source')))


def balances(db, wallet, principal):
    """The one local balance and the last native observation side by side. Deliberately no total."""
    wallet = _wallet(wallet)
    credits = wallet.read(db, _principal(principal))
    native = db.execute('SELECT observed_balance, source, observed_at FROM acq_native_wallet_observation '
                        'ORDER BY id DESC LIMIT 1').fetchone()
    return {'local_credits': {'source': wallet.source, 'profile_id': principal.profile_id, 'credits': credits,
                              'currency': LOCAL_CURRENCY, 'limit': MAX_CREDITS},
            'native_aUEC_observation': dict(native) if native else None,
            'note': 'LOCAL_CREDIT and native aUEC are different balances; they are never added together.'}


# ---- purchase --------------------------------------------------------------------------------

_PURCHASE_FIELDS = ('shop_id', 'item_class', 'quantity', 'quoted_unit_price', 'expected_offer_version',
                    'expected_credits', 'location_id')


def purchase(db, wallet, principal, data, key):
    """One atomic purchase: local balance debit (through `wallet`) + stock decrement + ledger
    holding + receipt + delivery record (state not_requested), all in the caller's transaction.

    wallet:    a WalletPort (ServerProfileWallet for server.py's profile.credits).
    principal: a Principal from the authenticated server context (principal_from_server_auth).
    data:      shop_id, item_class, quantity (1..1000), quoted_unit_price, expected_offer_version,
               expected_credits (the balance the buyer saw), location_id (a logical ledger place).
               owner / wallet / profile fields are refused with IDENTITY_IN_BODY.
    Returns the receipt; a replay of the same key + body + principal returns the first receipt
    with replayed=True and changes nothing.
    """
    _need_tx(db)
    wallet = _wallet(wallet)
    principal = _principal(principal)
    key = _key(key)
    if not isinstance(data, dict):
        raise AcquisitionError('BAD_REQUEST', 'request must be a JSON object')
    named = IDENTITY_FIELDS & set(data)
    if named:
        raise AcquisitionError('IDENTITY_IN_BODY',
                               f'{sorted(named)}: the buyer comes from the authenticated session, not the body')
    _only(data, _PURCHASE_FIELDS)
    shop_id = _ident(data, 'shop_id')
    item_class = _ident(data, 'item_class', _CLASS_RE)
    location_id = _ident(data, 'location_id')
    quantity = _int(data, 'quantity', 1, MAX_QUANTITY)
    quoted = _int(data, 'quoted_unit_price', 1, MAX_CREDITS)
    offer_version = _int(data, 'expected_offer_version', 1, MAX_VERSION)
    expected_credits = _int(data, 'expected_credits', 0, MAX_CREDITS)
    digest = _digest('purchase', {'profile_id': principal.profile_id, 'body': data})

    previous = db.execute('SELECT digest, receipt FROM acq_purchase WHERE idem_key=?', (key,)).fetchone()
    if previous is not None:
        if previous[0] != digest:
            raise AcquisitionError('KEY_REUSED', 'this idempotency key was used for a different purchase')
        receipt = json.loads(previous[1])
        receipt['replayed'] = True
        return receipt

    offer = db.execute('SELECT o.*, s.authoritative FROM acq_offer o JOIN acq_price_source s ON s.id=o.source_id '
                       'WHERE o.shop_id=? AND o.item_class=?', (shop_id, item_class)).fetchone()
    if offer is None:
        raise AcquisitionError('OFFER_NOT_FOUND', 'item is not listed in this shop')
    if not offer['authoritative']:
        raise AcquisitionError('SOURCE_NOT_AUTHORITATIVE', 'offer comes from a non-authoritative source')
    if offer['unit_price'] is None:
        raise AcquisitionError('PRICE_UNKNOWN', 'no authoritative price; purchase refused')
    if offer['stock'] is None:
        raise AcquisitionError('STOCK_UNKNOWN', 'no authoritative stock; purchase refused')
    if offer['version'] != offer_version:
        raise AcquisitionError('VERSION_CONFLICT', 'offer changed since the quote; re-quote')
    if offer['unit_price'] != quoted:
        raise AcquisitionError('PRICE_CHANGED', 'price differs from the quote; re-quote')
    if offer['stock'] < quantity:
        raise AcquisitionError('OUT_OF_STOCK', 'not enough stock')

    balance = wallet.read(db, principal)
    if balance != expected_credits:
        raise AcquisitionError('VERSION_CONFLICT', 'local balance changed since the quote; re-read it')
    total = offer['unit_price'] * quantity
    if total > MAX_CREDITS:
        raise AcquisitionError('LIMIT', 'total exceeds the balance limit 9e12')
    if balance < total:
        raise AcquisitionError('INSUFFICIENT_FUNDS', 'not enough local credits')

    after = wallet.debit(db, principal, total, expected_credits)
    cur = db.execute('UPDATE acq_offer SET stock=stock-?, version=version+1 '
                     'WHERE shop_id=? AND item_class=? AND version=? AND stock>=?',
                     (quantity, shop_id, item_class, offer_version, quantity))
    if cur.rowcount != 1:
        raise AcquisitionError('VERSION_CONFLICT', 'offer changed during the purchase')
    db.execute('INSERT INTO acq_holding(profile_id, item_class, location_id, quantity) VALUES(?,?,?,?) '
               'ON CONFLICT(profile_id, item_class, location_id) DO UPDATE SET quantity=quantity+excluded.quantity, '
               'version=version+1', (principal.profile_id, item_class, location_id, quantity))
    held = db.execute('SELECT quantity, version FROM acq_holding WHERE profile_id=? AND item_class=? AND location_id=?',
                      (principal.profile_id, item_class, location_id)).fetchone()

    purchase_id = uuid.uuid4().hex
    receipt = {
        'purchase_id': purchase_id, 'idempotency_key': key, 'replayed': False,
        'item_class': item_class, 'quantity': quantity, 'unit_price': offer['unit_price'], 'total': total,
        'currency': LOCAL_CURRENCY,
        'balance': {'source': wallet.source, 'profile_id': principal.profile_id,
                    'credits_before': balance, 'credits_after': after},
        'offer': {'shop_id': shop_id, 'source_id': offer['source_id'], 'stock_before': offer['stock'],
                  'stock_after': offer['stock'] - quantity, 'version_after': offer_version + 1},
        'holding': {'profile_id': principal.profile_id, 'location_id': location_id,
                    'quantity_after': held['quantity'], 'version_after': held['version'], 'kind': 'ledger'},
        'native': {'aUEC_changed': False, 'delivery_state': 'not_requested',
                   'note': 'Ledger purchase only. No game item was created or moved.'},
    }
    db.execute('INSERT INTO acq_purchase(id, idem_key, digest, profile_id, balance_source, shop_id, item_class, '
               'quantity, unit_price, total, source_id, offer_version, credits_before, credits_after, stock_before, '
               'stock_after, location_id, receipt) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',
               (purchase_id, key, digest, principal.profile_id, wallet.source, shop_id, item_class, quantity,
                offer['unit_price'], total, offer['source_id'], offer_version, balance, after,
                offer['stock'], offer['stock'] - quantity, location_id, json.dumps(receipt, sort_keys=True)))
    db.execute("INSERT INTO acq_delivery(purchase_id, target, state) VALUES(?, NULL, 'not_requested')", (purchase_id,))
    return receipt


# ---- native delivery state (separate from money and ownership) ------------------------------

def _delivery(db, purchase_id, expected_version):
    row = db.execute('SELECT * FROM acq_delivery WHERE purchase_id=?', (purchase_id,)).fetchone()
    if row is None:
        raise AcquisitionError('NOT_FOUND', 'delivery record not found')
    if type(expected_version) is not int or row['version'] != expected_version:
        raise AcquisitionError('VERSION_CONFLICT', 'delivery changed; re-read it')
    return row


def _move(db, purchase_id, expected_version, allowed_from, **fields):
    row = _delivery(db, purchase_id, expected_version)
    if row['state'] not in allowed_from:
        raise AcquisitionError('BAD_STATE', f"delivery is {row['state']}, expected one of {allowed_from}")
    names = sorted(fields)
    sets = ', '.join(f'{n}=?' for n in names)
    cur = db.execute(f'UPDATE acq_delivery SET {sets}, version=version+1 WHERE purchase_id=? AND version=?',
                     [fields[n] for n in names] + [purchase_id, expected_version])
    if cur.rowcount != 1:
        raise AcquisitionError('VERSION_CONFLICT', 'delivery changed; re-read it')
    return dict(db.execute('SELECT * FROM acq_delivery WHERE purchase_id=?', (purchase_id,)).fetchone())


def get_delivery(db, purchase_id):
    row = db.execute('SELECT * FROM acq_delivery WHERE purchase_id=?', (purchase_id,)).fetchone()
    return dict(row) if row else None


def request_delivery(db, purchase_id, target, expected_version):
    """not_requested / not_sent -> pending. Records intent only; nothing is sent to the game here.
    unconfirmed and confirmed can never be requested again."""
    _need_tx(db)
    if target not in DELIVERY_TARGETS:
        raise AcquisitionError('BAD_REQUEST', f'target must be one of {DELIVERY_TARGETS}')
    return _move(db, purchase_id, expected_version, ('not_requested', 'not_sent'),
                 state='pending', target=target, cause=None)


def mark_not_sent(db, purchase_id, expected_version, cause):
    """pending -> not_sent: refused BEFORE any native call (cause is one of NOT_SENT_CAUSES).
    Only from pending: after begin_dispatch the outcome is uncertain and this is refused."""
    _need_tx(db)
    if cause not in NOT_SENT_CAUSES:
        raise AcquisitionError('BAD_REQUEST', f'cause must be one of {NOT_SENT_CAUSES}')
    return _move(db, purchase_id, expected_version, ('pending',), state='not_sent', cause=cause)


def begin_dispatch(db, purchase_id, expected_version):
    """pending -> unconfirmed. Commit this BEFORE the native call (write-ahead). From here every
    outcome, a native error included, is uncertain: never retried, never not_sent."""
    _need_tx(db)
    return _move(db, purchase_id, expected_version, ('pending',), state='unconfirmed')


def confirm_delivery(db, purchase_id, expected_version, contract, evidence):
    """unconfirmed -> confirmed, ONLY through a contract in CONFIRMED_DELIVERY_CONTRACTS.
    That set is empty today, so this always refuses with CONTRACT_NOT_CONFIRMED."""
    _need_tx(db)
    if contract not in CONFIRMED_DELIVERY_CONTRACTS:
        raise AcquisitionError('CONTRACT_NOT_CONFIRMED',
                               'no published, game-proved delivery contract; delivery stays unconfirmed')
    return _move(db, purchase_id, expected_version, ('unconfirmed',),
                 state='confirmed', contract=contract, evidence=_text({'evidence': evidence}, 'evidence', 1000))


# ---- migration from acq-1 (own acq_wallet) ---------------------------------------------------

def migrate_acq1(db, wallet, principal, decision=None, expected_credits=None):
    """Upgrade an acq-1 database (own acq_wallet, limit 1e12) to acq-2 (no own wallet) in the
    caller's transaction. Offers, price sources and native observations are copied.

    Refused (MIGRATION_REFUSED) when acq-1 purchases, holdings or deliveries exist: their owner was
    taken from the request body and cannot be mapped to an authenticated profile safely.
    Refused (MIGRATION_DECISION_NEEDED) when acq_wallet holds money and no decision is given,
    whatever profile.credits holds; moving money between balances must be an explicit act.
    decision='merge_into_profile': profile.credits += sum(acq_wallet), compare-and-set on
      expected_credits, refused with LIMIT above 9e12.
    decision='discard_acq_wallet': the owner states the acq_wallet money is not real; dropped.
    Every run is recorded in acq_migration (action, totals, the old wallet rows)."""
    _need_tx(db)
    wallet = _wallet(wallet)
    principal = _principal(principal)
    found = _schema_value(db)
    if found != LEGACY_SCHEMA_ID or not _table_exists(db, 'acq_wallet'):
        raise AcquisitionError('SCHEMA', f'not an acq-1 database (schema {found!r})')
    if decision is not None and decision not in MIGRATION_DECISIONS:
        raise AcquisitionError('BAD_REQUEST', f'decision must be one of {MIGRATION_DECISIONS}')
    for table in ('acq_purchase', 'acq_holding', 'acq_delivery'):
        if db.execute(f'SELECT count(*) FROM {table}').fetchone()[0]:
            raise AcquisitionError('MIGRATION_REFUSED',
                                   f'{table} has acq-1 rows with a client-supplied owner; migrate them by hand')
    wallets = [(r[0], r[1]) for r in db.execute('SELECT id, balance FROM acq_wallet ORDER BY id')]
    acq_total = sum(balance for _, balance in wallets)
    credits_before = wallet.read(db, principal)
    action = 'empty'
    credits_after = credits_before
    if acq_total > 0:
        if decision is None:
            raise AcquisitionError('MIGRATION_DECISION_NEEDED',
                                   f'acq_wallet holds {acq_total} and {wallet.source} holds {credits_before} '
                                   f'LOCAL_CREDIT; choose one of {MIGRATION_DECISIONS}')
        action = decision
        if decision == 'merge_into_profile':
            if expected_credits != credits_before:
                raise AcquisitionError('VERSION_CONFLICT', 'local balance changed; re-read it before merging')
            if credits_before + acq_total > MAX_CREDITS:
                raise AcquisitionError('LIMIT', 'merged balance would exceed 9e12')
            credits_after = wallet.credit(db, principal, acq_total, credits_before)
    sources = [tuple(r) for r in db.execute('SELECT id, kind, authoritative, label FROM acq_price_source')]
    offers = [tuple(r) for r in db.execute('SELECT shop_id, item_class, source_id, currency, unit_price, stock, '
                                           'version FROM acq_offer')]
    observations = [tuple(r) for r in db.execute('SELECT id, observed_balance, source, observed_at '
                                                 'FROM acq_native_wallet_observation')]
    for table in ('acq_delivery', 'acq_purchase', 'acq_holding', 'acq_wallet', 'acq_offer',
                  'acq_native_wallet_observation', 'acq_price_source', 'acq_meta'):
        db.execute(f'DROP TABLE IF EXISTS {table}')
    _create_schema(db)
    db.executemany('INSERT INTO acq_price_source(id, kind, authoritative, label) VALUES(?,?,?,?)', sources)
    db.executemany('INSERT INTO acq_offer(shop_id, item_class, source_id, currency, unit_price, stock, version) '
                   'VALUES(?,?,?,?,?,?,?)', offers)
    db.executemany('INSERT INTO acq_native_wallet_observation(id, observed_balance, source, observed_at) '
                   'VALUES(?,?,?,?)', observations)
    detail = json.dumps({'acq_wallet_rows': wallets, 'balance_source': wallet.source,
                         'profile_id': principal.profile_id}, sort_keys=True)
    db.execute('INSERT INTO acq_migration(from_schema, to_schema, action, acq_wallet_total, credits_before, '
               'credits_after, detail) VALUES(?,?,?,?,?,?,?)',
               (LEGACY_SCHEMA_ID, SCHEMA_ID, action, acq_total, credits_before, credits_after, detail))
    return {'from': LEGACY_SCHEMA_ID, 'to': SCHEMA_ID, 'action': action, 'acq_wallet_total': acq_total,
            'credits_before': credits_before, 'credits_after': credits_after, 'offers': len(offers)}
