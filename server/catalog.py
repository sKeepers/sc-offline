"""Read-only UI catalogs from the mod lists and installed localization."""
from pathlib import Path
import re

MAKERS = dict(AEGS='Aegis', ANVL='Anvil', ARGO='Argo', BANU='Banu',
              CNOU='Consolidated Outland', CRUS='Crusader', DRAK='Drake',
              ESPR='Esperia', GAMA='Gatac', GRIN='Greycat', KRIG='Kruger',
              MISC='MISC', MRAI='Mirai', ORIG='Origin', RSI='RSI',
              TMBL='Tumbril', VNCL='Vanduul', XIAN='Aopoa')
CATEGORIES = dict(undersuit='Поддоспешники', helmet='Шлемы', torso='Броня корпуса',
                  arms='Броня рук', legs='Броня ног', backpack='Рюкзаки',
                  primary='Основное оружие', sidearm='Пистолеты', ammo='Боеприпасы',
                  grenade='Гранаты')
WORDS = dict(armor='Броня', light='Лёгкая', medium='Средняя', heavy='Тяжёлая',
             core='Корпус', helmet='Шлем', arms='Руки', legs='Ноги',
             undersuit='Поддоспешник', backpack='Рюкзак', rifle='Винтовка',
             pistol='Пистолет', grenade='Граната', mag='Магазин')


def localized_names(path):
    result = {}
    if path and path.is_file():
        for line in path.read_text(encoding='utf-8-sig', errors='replace').splitlines():
            key, sep, value = line.partition('=')
            if not sep or not value or value.startswith('@') or '\\n' in value:
                continue
            for prefix in ('item_Name_', 'item_Name', 'vehicle_Name_', 'vehicle_Name'):
                if key.startswith(prefix):
                    result[key[len(prefix):].casefold()] = value
                    break
    return result


def fallback(raw, ship=False):
    tokens = raw.split('_')
    if ship:
        tokens[0] = MAKERS.get(tokens[0], tokens[0])
    else:
        tokens = [WORDS.get(t.casefold(), t) for t in tokens]
    return ' '.join(tokens)


class Catalog:
    def __init__(self, data_dir, localization=None):
        self.data_dir = Path(data_dir)
        self.localization = Path(localization) if localization else None
        self.signature = None
        self.cached = None

    def snapshot(self):
        game_path = self.data_dir / 'game-path.txt'
        locale = self.localization
        if locale is None and game_path.is_file():
            root = Path(game_path.read_text(encoding='utf-8-sig').strip())
            if root.name.casefold() == 'bin64':
                root = root.parent
            locale = root / 'data/Localization/korean_(south_korea)/global.ini'
        paths = [self.data_dir / 'ships.txt', self.data_dir / 'items.txt', locale]
        signature = tuple((str(p), p.stat().st_mtime_ns, p.stat().st_size) if p and p.is_file() else (str(p), 0, 0) for p in paths)
        if signature == self.signature:
            return self.cached
        names = localized_names(locale)
        result = {'ships': [], 'items': [], 'categories': CATEGORIES,
                  'note': 'Каталог доступных классов, не список принадлежащих вам вещей. Некоторые варианты тестовые или предназначены для NPC.'}
        for kind, path in zip(('ships', 'items'), paths):
            seen, category = set(), ''
            if not path.is_file():
                continue
            for raw in path.read_text(encoding='utf-8-sig').splitlines():
                raw = raw.strip()
                if not raw or raw.startswith(('#', ';')):
                    continue
                if raw.startswith('[') and raw.endswith(']'):
                    category = raw[1:-1]
                    continue
                if raw in seen or not re.fullmatch(r'[A-Za-z0-9_\-.]+', raw):
                    continue
                seen.add(raw)
                result[kind].append({'class_name': raw, 'name': names.get(raw.casefold(), fallback(raw, kind == 'ships')), 'category': category})
        self.cached, self.signature = result, signature
        return result
