from pathlib import Path
import tempfile
import unittest
from catalog import Catalog


class CatalogTests(unittest.TestCase):
    def test_exact_classes_localization_categories_and_refresh(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'ships.txt').write_text('# comment\nAEGS_Avenger_Titan\nAEGS_Avenger_Titan\n', encoding='utf-8')
            (root/'items.txt').write_text('[helmet]\nhelmet_01\n[ammo]\nammo_test\n', encoding='utf-8')
            locale = root/'global.ini'
            locale.write_text('\ufeffvehicle_Name_AEGS_Avenger_Titan=Титан\nitem_Name_helmet_01=Шлем\n', encoding='utf-8')
            catalog = Catalog(root, locale)
            first = catalog.snapshot()
            self.assertEqual(first['ships'], [{'class_name':'AEGS_Avenger_Titan','name':'Титан','category':'','manufacturer':'Aegis','ship_type':'Транспортные'}])
            self.assertEqual(first['items'][0]['name'], 'Шлем')
            self.assertEqual(first['items'][1]['category'], 'ammo')
            locale.write_text('item_Name_helmet_01=Новый шлем\n', encoding='utf-8')
            self.assertEqual(catalog.snapshot()['items'][0]['name'], 'Новый шлем')
            self.assertEqual(catalog.snapshot()['ships'][0]['name'], 'Aegis Avenger Titan')

    def test_ship_groups_keep_unknown_and_exact_class(self):
        from catalog import ship_group
        self.assertEqual(ship_group('RSI_Constellation_Mk5_Centaurus'),dict(manufacturer='RSI',ship_type='Многоцелевые'))
        self.assertEqual(ship_group('AEGS_Avenger_Stalker')['ship_type'],'Боевые')
        self.assertEqual(ship_group('NEW_Unknown_Model'),dict(manufacturer='Other / unknown',ship_type='Не определён'))
        self.assertEqual(ship_group('RSI_Future_Model'),dict(manufacturer='RSI',ship_type='Не определён'))

    def test_missing_files_are_empty_not_fake_owned_items(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Catalog(folder).snapshot()
            self.assertEqual(data['ships'], [])
            self.assertEqual(data['items'], [])


if __name__ == '__main__':
    unittest.main()
