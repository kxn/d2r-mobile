#!/usr/bin/env python3
"""Boundary checks for composition selection; real CASC readback is separate."""
import copy
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).with_name('compose_game_overlay.py')
spec = importlib.util.spec_from_file_location('bc3_overlay', path)
composition = importlib.util.module_from_spec(spec)
spec.loader.exec_module(composition)


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.original = [dict(filename='data/a.texture', format=61, layoutData=123, thumbnailBase64='AA=='),
                         dict(filename='data/b.texture', format=62, layoutData=456),
                         dict(filename='data/fallback.texture', format=61, layoutData=789)]
        self.entries = [dict(id=1, logical='data/a.texture', original_layout_data=123, layout8='444d4e0201000000'),
                        dict(id=2, logical='data/b.texture', original_layout_data=456, layout8='444d4e0202000000')]
        self.updated = copy.deepcopy(self.original)
        for n, row in enumerate(self.entries):
            self.updated[n]['layoutData'] = int.from_bytes(bytes.fromhex(row['layout8']), 'little')

    def test_only_layout_changes_and_complete_fallback(self):
        selected, formats = composition.cache_contract(self.original, self.updated, self.entries)
        self.assertEqual(selected, {'data/a.texture': 1, 'data/b.texture': 2})
        self.assertEqual(formats, {'61': 1, '62': 1})

    def test_selected_format_or_thumbnail_change_rejected(self):
        for key, value in [('format', 95), ('thumbnailBase64', 'Ag==')]:
            bad = copy.deepcopy(self.updated); bad[0][key] = value
            with self.assertRaisesRegex(ValueError, 'beyond family layout marker'):
                composition.cache_contract(self.original, bad, self.entries)

    def test_original_mixed_case_names_use_existing_CASC_normalization(self):
        entries = copy.deepcopy(self.entries); entries[0]['logical'] = 'data/A.texture'
        selected, _ = composition.cache_contract(self.original, self.updated, entries)
        self.assertEqual(selected['data/a.texture'], 1)

    def test_unselected_change_rejected(self):
        bad = copy.deepcopy(self.updated); bad[2]['format'] = 62
        with self.assertRaisesRegex(ValueError, 'unselected'):
            composition.cache_contract(self.original, bad, self.entries)

    def test_duplicate_id_rejected(self):
        bad = copy.deepcopy(self.entries); bad[1]['id'] = 1
        with self.assertRaisesRegex(ValueError, 'family id'):
            composition.cache_contract(self.original, self.updated, bad)

    def test_marker_id_mismatch_rejected(self):
        bad = copy.deepcopy(self.entries); bad[0]['layout8'] = '444d4e0207000000'
        with self.assertRaisesRegex(ValueError, 'marker/id'):
            composition.cache_contract(self.original, self.updated, bad)

    def test_cache_order_or_missing_rows_rejected(self):
        for bad in [list(reversed(self.updated)), self.updated[:-1]]:
            with self.assertRaisesRegex(ValueError, 'order/names'):
                composition.cache_contract(self.original, bad, self.entries)

    def test_original_layout_and_selection_are_required(self):
        for key, value in [('logical', 'data/missing.texture'), ('original_layout_data', 999)]:
            bad = copy.deepcopy(self.entries); bad[0][key] = value
            with self.assertRaises(ValueError):
                composition.cache_contract(self.original, self.updated, bad)


if __name__ == '__main__':
    unittest.main()
