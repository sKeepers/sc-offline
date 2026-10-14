"""Read-only UI catalogs from the mod lists and installed localization.

item_class / class_name is always the exact line from items.txt / ships.txt. Names, categories,
aliases and search terms are for display only and never replace the identifier.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import stat
import sys
import threading
import time

MAKERS = dict(AEGS='Aegis', ANVL='Anvil', ARGO='Argo', BANU='Banu',
              CNOU='Consolidated Outland', CRUS='Crusader', DRAK='Drake',
              ESPR='Esperia', GAMA='Gatac', GRIN='Greycat', KRIG='Kruger',
              MISC='MISC', MRAI='Mirai', ORIG='Origin', RSI='RSI',
              TMBL='Tumbril', VNCL='Vanduul', XIAN='Aopoa')
# Section names and order as gear_attachments.h (kGearSections, then kExtraSections).
GEAR_SECTIONS = ('undersuit', 'helmet', 'torso', 'arms', 'legs', 'backpack',
                 'primary', 'sidearm', 'ammo', 'grenade')
EXTRA_SECTIONS = ('optic', 'barrel', 'underbarrel', 'toolmodule', 'toolcanister', 'toolbattery')
CATEGORIES = dict(undersuit='Поддоспешники', helmet='Шлемы', torso='Броня корпуса',
                  arms='Броня рук', legs='Броня ног', backpack='Рюкзаки',
                  primary='Основное оружие', sidearm='Пистолеты', ammo='Боеприпасы',
                  grenade='Гранаты', optic='Прицелы', barrel='Дульные насадки',
                  underbarrel='Подствольные модули', toolmodule='Модули мультитула',
                  toolcanister='SRT-картриджи', toolbattery='Батареи мультитула')
# A section the mod does not know yet: its classes stay in the catalog under this label.
# (The game menu skips such lines; the web catalog keeps the class so records stay readable.)
UNKNOWN_CATEGORY = 'Новый раздел: {}'

# The same dictionary as ListDisplayLabel in src/list_label.h (test_catalog checks parity).
WORDS = {
    'outpost parts': 'Модули аванпостов', 'sq42': 'Эскадрилья 42', 'outlaw': 'Преступники',
    'otlw': 'Преступники', 'depot': 'Склад', 'layout': 'Планировка', 'cluster': 'Группа модулей',
    'shelter': 'Убежище', 'hab': 'Жилой модуль', 'ruin': 'Руины', 'building': 'Здание',
    'cargo': 'Груз', 'counter': 'Прилавок', 'bench': 'Скамья', 'rack': 'Стеллаж',
    'mannequin': 'Манекен', 'fruit': 'Фрукты', 'plant': 'Растение', 'weapon': 'Оружие',
    'watertank': 'Бак воды', 'shop': 'Магазин', 'containers': 'Контейнеры',
    'stall': 'Торговая стойка', 'box': 'Ящик', 'food': 'Еда', 'stool': 'Табурет',
    'booth': 'Кабина', 'display': 'Дисплей', 'components': 'Компоненты', 'stand': 'Подставка',
    'full': 'Полный', 'single': 'Одиночный', 'static': 'Статичный', 'lootable': 'С содержимым',
    'int': 'Интерьер', 'ext': 'Внешний', 'xs': 'Очень малый', 'sml': 'Малый', 'med': 'Средний',
    'lge': 'Большой', 'entry': 'Вход', 'sand': 'Песок', 'red': 'Красный', 'frost': 'Лёд',
    'bar': 'Бар', 'hub': 'Центральный модуль', 'human': 'Человек', 'armor': 'Броня',
    'combat': 'Боевой', 'light': 'Лёгкая', 'medium': 'Средняя', 'heavy': 'Тяжёлая',
    'superheavy': 'Сверхтяжёлая', 'core': 'Корпус', 'helmet': 'Шлем', 'arms': 'Руки',
    'legs': 'Ноги', 'undersuit': 'Поддоспешник', 'flightsuit': 'Лётный костюм', 'suit': 'Костюм',
    'backpack': 'Рюкзак', 'pistol': 'Пистолет', 'rifle': 'Винтовка', 'sniper': 'Снайперская',
    'shotgun': 'Дробовик', 'smg': 'Пистолет-пулемёт', 'lmg': 'Пулемёт', 'glauncher': 'Гранатомёт',
    'crossbow': 'Арбалет', 'energy': 'Энергетический', 'ballistic': 'Баллистический',
    'mag': 'Магазин', 'gren': 'Граната', 'grenade': 'Граната', 'frag': 'Осколочная',
    'multitool': 'Мультитул', 'medgun': 'Медицинский инструмент', 'vial': 'Картридж',
    'repair': 'Ремонт', 'salvage': 'Утилизация', 'filled': 'Заполненный', 'resource': 'Ресурс',
    'env': 'Для опасной среды', 'xenothreat': 'XenoThreat', 'seat': 'Кресло', 'pilot': 'Пилот',
    'copilot': 'Второй пилот', 'gunner': 'Стрелок', 'turret': 'Турель', 'captain': 'Капитан',
    'engineer': 'Инженер', 'operator': 'Оператор', 'bridge': 'Мостик', 'navy': 'Флот',
    'marine': 'Морпех', 'fatigues': 'Полевая форма', 'jumpsuit': 'Комбинезон', 'medic': 'Медик',
    'commander': 'Командир', 'lieutenant': 'Лейтенант', 'officer': 'Офицер',
    'commodore': 'Коммодор', 'deck': 'Палубный', 'crew': 'Экипаж', 'training': 'Учебный',
    'shorts': 'Шорты', 'become': 'Образ', 'experimental': 'Экспериментальный',
    'outposts': 'Аванпосты', 'outpost': 'Аванпост', 'parts': 'Части', 'guards': 'Охрана',
    'bunkers': 'Бункеры', 'habs': 'Жилые модули', 'ruins': 'Руины', 'fob': 'Передовая база',
    'cover': 'Укрытия', 'market': 'Рынок', 'base': 'База', 'furniture': 'Мебель',
    'storage': 'Хранение', 'lights': 'Освещение', 'turrets': 'Турели', 'deco': 'Декорации',
    'props': 'Предметы', 'displays': 'Дисплеи', 'crate': 'Ящик', 'container': 'Контейнер',
    'chair': 'Стул', 'table': 'Стол', 'bed': 'Кровать', 'door': 'Дверь', 'wall': 'Стена',
    'optics': 'Прицел', 'ubarrel': 'Подствольный модуль', 'holo': 'Голографический',
    'rdot': 'Коллиматорный', 'supp': 'Глушитель', 'comp': 'Компенсатор', 'stab': 'Стабилизатор',
    'flsh': 'Фонарь', 'lasr': 'Лазерный целеуказатель', 'mining': 'Добыча', 'cutter': 'Резак',
    'tractorbeam': 'Тяговый луч', 'healing': 'Лечение',
}
# gear::EnglishAlias in src/gear_attachments.h: PR #127 product names, for search and hints only.
ALIASES = {
    'grin_multitool_01_mining': 'OreBit Mining Attachment',
    'grin_multitool_01_cutter': 'OxyTorch Cutter Attachment',
    'grin_multitool_01_tractorbeam': 'TruHold Tractor Beam Attachment',
    'grin_multitool_01_healing': 'LifeGuard Medical Attachment',
    'grin_multitool_01_salvage_repair': 'Cambio-Lite SRT Attachment',
    'grin_multitool_resource_salvage_repair_01': 'Cambio-Lite SRT Canister (empty)',
    'grin_multitool_resource_salvage_repair_01_filled': 'Cambio-Lite SRT Canister (filled)',
}
NOTE = ('Каталог доступных классов, не список принадлежащих вам вещей. '
        'Некоторые варианты тестовые или предназначены для NPC.')
NAME_PREFIXES = ('item_Name_', 'item_Name', 'vehicle_Name_', 'vehicle_Name')
CLASS_NAME = re.compile(r'[A-Za-z0-9_\-.]+')
SAFE_CODE = re.compile(r'[A-Za-z0-9_\-()]+')
ITEM_CLASS_LIMIT = 200            # server.py accepts item_class up to 200 characters
LIST_LIMIT = 4 * 1024 * 1024
LOCALE_LIMIT = 64 * 1024 * 1024
CONFIG_LIMIT = 2 * 1024 * 1024   # LocalI18n::Read default
BOM = b'\xef\xbb\xbf'
FILES = dict(ships='ships.txt', items='items.txt', locale='global.ini')
REASONS = dict(missing='файл не найден', unavailable='файл занят или недоступен',
               changing='файл менялся во время чтения', too_big='файл слишком большой',
               incomplete='файл записан не полностью (нулевые байты)',
               broken_utf8='файл не в кодировке UTF-8', young='файл только что изменён')
RANK = dict(ok=0, fallback=0, missing=1, updating=2, stale=3, unavailable=4)


def ascii_lower(text):
    return ''.join(c.lower() if c < '\x80' else c for c in text)


def parse_names(data):
    """global.ini bytes -> ({lower-case class: name}, invalid lines). As ReadLocalizedListNames;
    a line that is not valid UTF-8 is skipped and counted instead of becoming U+FFFD text."""
    # Codex: match native Local6 strict whole-file UTF-8; _load keeps last-good on failure.
    data.decode("utf-8-sig")
    result, invalid = {}, 0
    for raw in data.split(b'\n'):
        if raw.endswith(b'\r'):
            raw = raw[:-1]
        if raw.startswith(BOM):
            raw = raw[3:]
        try:
            line = raw.decode('utf-8')
        except UnicodeDecodeError:
            invalid += 1
            continue
        key, sep, value = line.partition('=')
        if not sep or not value:
            continue
        for prefix in NAME_PREFIXES:
            if key.startswith(prefix):
                key = ascii_lower(key[len(prefix):])
                if key and '\\n' not in value and not value.startswith('@'):
                    result[key] = value
                break
    return result, invalid


def localized_names(path):
    """Backward-compatible helper: names from one global.ini, {} when it is missing."""
    path = Path(path) if path else None
    if not path or not path.is_file():
        return {}
    return parse_names(path.read_bytes())[0]


def fallback(raw, ship=False):
    """Readable label without global.ini. Items follow ListDisplayLabel (src/list_label.h)."""
    if ship:
        tokens = raw.split('_')
        tokens[0] = MAKERS.get(tokens[0], tokens[0])
        return ' '.join(tokens)
    whole = WORDS.get(ascii_lower(raw))
    if whole:
        return whole
    return ' · '.join(WORDS.get(ascii_lower(t), t) for t in re.split(r'[_\- /\\]', raw) if t)


def parse_ships(text):
    """ships.txt as before: exact class lines, first occurrence wins."""
    rows, seen, category = [], set(), ''
    for raw in text.splitlines():
        raw = raw.strip()
        if not raw or raw.startswith(('#', ';')):
            continue
        if raw.startswith('[') and raw.endswith(']'):
            category = raw[1:-1]
            continue
        if raw in seen or not CLASS_NAME.fullmatch(raw):
            continue
        seen.add(raw)
        rows.append((raw, [category]))
    return dict(rows=rows, duplicates=0, rejected=0, unknown_sections=[])


def parse_items(text):
    """items.txt: exact class lines; a class listed in several sections keeps every category.
    Section headers match gear_attachments.h SectionOf (case-insensitive, '#' starts a comment)."""
    rows, order = {}, []
    duplicates = rejected = 0
    unknown, category = [], ''
    for raw in text.splitlines():
        line = raw.split('#', 1)[0].strip()
        if not line or line.startswith(';'):
            continue
        if line.startswith('['):
            inner = line[1:].split(']', 1)[0] if ']' in line else line[1:]
            known = ascii_lower(inner)
            if known in CATEGORIES:
                category = known
            else:
                category = inner if inner.strip() else '[]'   # kept as written: never merged into a known one
                if category not in unknown:
                    unknown.append(category)
            continue
        if not CLASS_NAME.fullmatch(line) or len(line) > ITEM_CLASS_LIMIT:
            rejected += 1
            continue
        if line in rows:
            duplicates += 1
            if category not in rows[line]:
                rows[line].append(category)
            continue
        rows[line] = [category]
        order.append(line)
    return dict(rows=[(c, rows[c]) for c in order], duplicates=duplicates,
                rejected=rejected, unknown_sections=unknown)


# ---- locale source: the same rules as LocalI18n::Discover (local/localization.h) -------------
def safe_code(text):
    return bool(text) and len(text) < 80 and text not in ('.', '..') and bool(SAFE_CODE.fullmatch(text))


def read_text(path, limit=CONFIG_LIMIT, require_utf8=True):
    """LocalI18n::Read: bounded, BOM removed; None when missing, too big or not UTF-8."""
    try:
        with open(path, 'rb') as f:
            data = f.read(limit + 1)
    except OSError:
        return None
    if len(data) > limit:
        return None
    if data.startswith(BOM):
        data = data[3:]
    if not require_utf8:
        return data.decode('latin-1')
    if b'\0' in data:
        return None
    try:
        return data.decode('utf-8')
    except UnicodeDecodeError:
        return None


def parse_settings(text):
    """LocalI18n::Parse without escapes: trimmed key=value, '#'/';' comments, first key wins."""
    out = {}
    for line in (text or '').split('\n'):
        if line.endswith('\r'):
            line = line[:-1]
        stripped, at = line.strip(' \t\r\n'), line.find('=')
        if not stripped or stripped[0] in '#;' or at < 0:
            continue
        key, value = line[:at].strip(' \t\r\n'), line[at + 1:].strip(' \t\r\n')
        if not key or len(key) > 160 or len(value) > 32768 or key in out:
            continue
        out[key] = value
    return out


def language_code(slot, label):
    if 'Рус' in label or 'рус' in label or slot == 'russian' or slot.startswith('russian_'):
        return 'ru'
    if slot == 'english' or slot.startswith('english_'):
        return 'en'
    for prefix, code in (('german', 'de'), ('french', 'fr'), ('spanish', 'es'), ('italian', 'it'),
                         ('polish', 'pl'), ('portuguese', 'pt'), ('korean', 'ko'),
                         ('japanese', 'ja'), ('chinese', 'zh')):
        if slot.startswith(prefix):
            return code
    return slot


def launcher_settings(path):
    """language / game_language from sc-offline.ini, as the launcher's ReadConfig reads them."""
    language = game = 'auto'
    try:
        with open(path, 'rb') as f:
            data = f.read(CONFIG_LIMIT + 1)
    except OSError:
        return language, game
    if len(data) > CONFIG_LIMIT:
        return language, game
    trim = ' \t\r\n"'
    for line in data.removeprefix(BOM).decode('utf-8', 'replace').split('\n'):
        stripped = line.strip(trim)
        if not stripped or stripped[0] == '#' or '=' not in line:
            continue
        key, value = line.split('=', 1)
        key, value = key.strip(trim).lower(), value.strip(trim)
        if key == 'language' and safe_code(value):
            language = value
        elif key == 'game_language' and safe_code(value):
            game = value
    return language, game


def discover(root, requested, override, os_code):
    """Which global.ini the launcher would choose. Returns path (or None), slot, label and source."""
    auto = requested in ('', 'auto')
    out = dict(path=None, slot='', label='', source='none', available=[])
    if root is None:
        out['ui'] = (os_code if auto else requested) if safe_code(os_code if auto else requested) else 'en'
        return out
    root = Path(root)
    labels = parse_settings(read_text(root / 'data' / 'languages.ini'))
    available = []
    try:
        entries = list(os.scandir(root / 'data' / 'Localization'))
    except OSError:
        entries = []
    for entry in entries:
        slot = entry.name
        try:
            if not slot.isascii() or not safe_code(slot) or not entry.is_dir():
                continue
            global_ini = Path(entry.path) / 'global.ini'
            if not global_ini.is_file():
                continue
        except OSError:
            continue
        label = labels.get(slot, slot)
        available.append((slot, label, language_code(slot, label), global_ini))
    available.sort(key=lambda a: a[0])
    settings = parse_settings(read_text(root / 'user.cfg', CONFIG_LIMIT, False))
    active = settings.get('g_language', '')
    if len(active) > 1 and active[0] == '"' and active[-1] == '"':
        active = active[1:-1]

    def find(slot):
        return next((a for a in available if a[0] == slot), None)
    selected = None if override in ('', 'auto') else find(override)
    source = 'game_language'
    from_cfg = bool(active) and safe_code(active)
    wanted = (language_code(active, labels.get(active, active)) if from_cfg else os_code) if auto else requested
    if not selected and auto:
        selected, source = find(active), 'user.cfg'
    if not selected:
        selected = next((a for a in available if a[2] == wanted), None)
        source = 'language' if not auto else ('user.cfg' if from_cfg else 'system')
    if selected:
        out.update(path=selected[3], slot=selected[0], label=selected[1], source=source)
    out['available'] = [a[0] for a in available]
    out['ui'] = wanted if safe_code(wanted) else 'en'
    return out


_system_language = None


def system_language():
    """The OS UI language code (GetUserDefaultLocaleName on Windows, as the launcher), else 'en'."""
    global _system_language
    if _system_language is None:
        code = ''
        try:
            if sys.platform == 'win32':
                import ctypes
                buffer = ctypes.create_unicode_buffer(85)
                if ctypes.windll.kernel32.GetUserDefaultLocaleName(buffer, 85):
                    code = buffer.value.split('-')[0]
            else:
                import locale
                code = (locale.getlocale()[0] or '').split('_')[0]
        except Exception:
            code = ''
        _system_language = code if safe_code(code) else 'en'
    return _system_language


def read_stable(path, limit):
    """('ok', bytes, stat) only for a whole regular file that did not change while it was read."""
    try:
        before = os.stat(path)
    except FileNotFoundError:
        return 'missing', None, None
    except OSError:
        return 'unavailable', None, None
    if not stat.S_ISREG(before.st_mode):
        return 'unavailable', None, before
    if before.st_size > limit:
        return 'too_big', None, before
    try:
        with open(path, 'rb') as f:
            data = f.read(limit + 1)
        after = os.stat(path)
    except FileNotFoundError:
        return 'changing', None, before
    except OSError:
        return 'unavailable', None, before
    if ((before.st_mtime_ns, before.st_size, before.st_ino) != (after.st_mtime_ns, after.st_size, after.st_ino)
            or len(data) != after.st_size):
        return 'changing', None, after
    if b'\0' in data:
        return 'incomplete', None, after
    return 'ok', data, after


def decode_list(parse):
    def run(data):
        return parse(data.decode('utf-8-sig'))   # UnicodeDecodeError -> broken_utf8
    return run


class Catalog:
    """Catalog(data_dir, localization=None) keeps its old meaning: an explicit global.ini wins.

    Without it the global.ini is chosen like the launcher (sc-offline.ini language/game_language,
    the game's user.cfg and languages.ini, then the OS language). settle: a file changed less than
    this many seconds ago is reported as 'updating' and the last complete version stays in use;
    grace: how long a file that vanished (rename/copy) is bridged by the last complete version.
    Both are 0 by default (old behaviour); server.py uses CATALOG_SETTLE and CATALOG_GRACE.
    """

    def __init__(self, data_dir, localization=None, *, config=None, ui_language=None,
                 game_language=None, os_language=None, settle=0.0, grace=0.0, clock=time.time):
        self.data_dir = Path(data_dir)
        self.localization = Path(localization) if localization else None
        self.config = Path(config) if config else self.data_dir.parent / 'sc-offline.ini'
        self.ui_language, self.game_language, self.os_language = ui_language, game_language, os_language
        self.settle, self.grace, self.clock = settle, grace, clock
        self.signature = None
        self.cached = None
        self._lock = threading.Lock()
        self._good = {}
        self._since = {}
        self._last_choice = None
        self._choice_inputs = {}
        self._choice_overrides = None
        self._choice_gap = None

    def locale_choice(self):
        if self.localization is not None:
            return dict(path=self.localization, slot='', label='', source='explicit', available=[], ui='')
        root = None
        text = read_text(self.data_dir / 'game-path.txt', 64 * 1024)
        if text and text.strip():
            root = Path(text.strip())
            if root.name.casefold() == 'bin64':
                root = root.parent
        language, game = launcher_settings(self.config)
        if self.ui_language is not None:
            language = self.ui_language
        if self.game_language is not None:
            game = self.game_language
        return discover(root, language, game, self.os_language or system_language())

    def _stable_locale_choice(self):
        """Bounded last source retention on auto-discovery gaps, without hiding a real setting change."""
        choice = self.locale_choice()
        overrides = (self.ui_language, self.game_language, self.os_language)
        previous = self._last_choice
        if previous is not None and self.grace > 0 and overrides == self._choice_overrides:
            changed = missing_input = False
            for path, old in self._choice_inputs.items():
                current = read_text(path, CONFIG_LIMIT, False)
                if current is None and old is not None:
                    missing_input = True
                elif current != old:
                    changed = True
            try:
                missing_locale = not Path(previous['path']).is_file()
            except OSError:
                missing_locale = True
            if not changed and (missing_input or missing_locale):
                if self._choice_gap is None:
                    self._choice_gap = self.clock()
                if self.clock() - self._choice_gap < self.grace:
                    held = dict(previous)
                    held['_discovery_gap'] = True
                    return held
            else:
                self._choice_gap = None
        if choice['path'] is not None:
            paths = [self.config, self.data_dir / 'game-path.txt']
            game_path = read_text(self.data_dir / 'game-path.txt', 64 * 1024)
            if game_path and game_path.strip():
                root = Path(game_path.strip())
                if root.name.casefold() == 'bin64':
                    root = root.parent
                paths += [root / 'user.cfg', root / 'data' / 'languages.ini']
            self._last_choice = dict(choice)
            self._choice_inputs = {p: read_text(p, CONFIG_LIMIT, False) for p in paths}
            self._choice_overrides = overrides
        else:
            self._last_choice = None
        return choice

    def _load(self, key, path, limit, parse, quick=False):
        """One source under the last-good policy -> (value or None, meta, digest)."""
        now = self.clock()
        good = self._good.get(key)
        if good is not None and good['path'] != str(path):
            good = None   # another file was chosen: the old one is not its last good version
            self._good.pop(key, None)
            self._since.pop(key, None)
        if quick and good is not None:
            try:
                info = os.stat(path)
                if good['token'] == (info.st_mtime_ns, info.st_size, info.st_ino):
                    self._since.pop(key, None)
                    return good['value'], dict(state='ok'), good['digest']
            except OSError:
                pass
        reason, data, info = read_stable(path, limit)
        if reason == 'ok':
            try:
                value = parse(data)
            except UnicodeDecodeError:
                reason = 'broken_utf8'
        if reason == 'ok':
            digest = hashlib.sha256(data).hexdigest()
            token = (info.st_mtime_ns, info.st_size, info.st_ino)
            if good is not None and good['digest'] == digest:
                good['token'] = token
                self._since.pop(key, None)
                return good['value'], dict(state='ok'), digest
            fresh = dict(path=str(path), digest=digest, token=token, value=value)
            if self.settle and 0 <= now - info.st_mtime_ns / 1e9 < self.settle:   # a future mtime: not waited for
                shown = good or fresh
                return shown['value'], dict(state='updating', reason='young'), shown['digest'] + '+updating'
            self._good[key] = fresh
            self._since.pop(key, None)
            return value, dict(state='ok'), digest
        since = self._since.setdefault(key, now)
        if reason == 'missing' and (good is None or now - since >= self.grace):
            self._good.pop(key, None)
            return None, dict(state='missing', reason='missing'), 'missing'
        if good is not None:
            return good['value'], dict(state='stale', reason=reason, since=int(since)), good['digest'] + '+stale'
        return None, dict(state='unavailable', reason=reason, since=int(since)), 'unavailable'

    def snapshot(self):
        with self._lock:
            return self._snapshot()

    def _snapshot(self):
        choice = self._stable_locale_choice()
        ships, ships_meta, ships_digest = self._load('ships', self.data_dir / 'ships.txt', LIST_LIMIT, decode_list(parse_ships))
        items, items_meta, items_digest = self._load('items', self.data_dir / 'items.txt', LIST_LIMIT, decode_list(parse_items))
        if choice['path'] is not None:
            names, locale_meta, names_digest = self._load('locale', choice['path'], LOCALE_LIMIT, parse_names, quick=True)
        else:
            names, locale_meta, names_digest = None, dict(state='fallback'), ''
        if choice.get('_discovery_gap') and locale_meta['state'] == 'ok':
            locale_meta = dict(state='stale', reason='changing', since=int(self._choice_gap))
        locale = dict(source=choice['source'], slot=choice['slot'], label=choice['label'],
                      available=choice['available'])
        signature = json.dumps([locale, ships_meta, items_meta, locale_meta,
                                ships_digest, items_digest, names_digest], sort_keys=True)
        if signature == self.signature and self.cached is not None:
            return self.cached
        result = self._build(ships, items, names, locale, dict(ships=ships_meta, items=items_meta, locale=locale_meta))
        self.cached, self.signature = result, signature
        return result

    def _build(self, ships, items, names, locale, sources):
        table, invalid = names if names else ({}, 0)
        ships = ships or dict(rows=[], duplicates=0, rejected=0, unknown_sections=[])
        items = items or dict(rows=[], duplicates=0, rejected=0, unknown_sections=[])
        categories = dict(CATEGORIES)
        for section in items['unknown_sections']:
            categories.setdefault(section, UNKNOWN_CATEGORY.format(section))
        result = {'ships': [], 'items': [], 'categories': categories,
                  'category_list': [[k, v] for k, v in categories.items()], 'note': NOTE}
        for cls, cats in ships['rows']:
            result['ships'].append({'class_name': cls, 'name': table.get(ascii_lower(cls), fallback(cls, True)),
                                    'category': cats[0]})
        for cls, cats in items['rows']:
            own = fallback(cls)
            name = table.get(ascii_lower(cls)) or own
            alias = ALIASES.get(ascii_lower(cls))
            terms = ([own] if name != own else []) + ([alias] if alias else [])
            row = {'class_name': cls, 'name': name, 'category': cats[0], 'categories': cats, 'terms': terms}
            if alias:
                row['alias'] = alias
            result['items'].append(row)
        sources['ships'].update(count=len(result['ships']))
        sources['items'].update(count=len(result['items']), duplicates=items['duplicates'],
                                rejected=items['rejected'], unknown_sections=items['unknown_sections'])
        if names:
            sources['locale'].update(count=len(table), invalid_lines=invalid)
        messages = []
        for key, meta in sources.items():
            text = self._message(key, meta)
            if text:
                meta['message'] = text
                messages.append(text)
        state = max((meta['state'] for meta in sources.values()), key=RANK.__getitem__)
        result['status'] = dict(state=state, message=' '.join(messages) or 'Каталог прочитан полностью.',
                                sources=sources)
        result['locale'] = locale
        body = json.dumps(result, ensure_ascii=False, sort_keys=True).encode()
        result['version'] = hashlib.sha256(body).hexdigest()[:24]
        return result

    @staticmethod
    def _message(key, meta):
        file, state, reason = FILES[key], meta['state'], REASONS.get(meta.get('reason'), '')
        if state == 'updating':
            return f'{file}: файл только что изменён; чтение повторится после паузы.'
        if state == 'stale':
            return f'{file}: {reason}. Показан последний полностью прочитанный вариант.'
        if state == 'unavailable':
            what = 'Используются встроенные названия.' if key == 'locale' else 'Список пуст до успешного чтения.'
            return f'{file}: {reason}. {what}'
        if state == 'missing':
            return f'{file} не найден.' + (' Используются встроенные названия.' if key == 'locale' else '')
        return ''
