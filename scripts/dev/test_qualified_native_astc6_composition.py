#!/usr/bin/env python3
"""Synthetic identity/order and immutable family tests; no GPU/game claims."""
import hashlib
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('composition', Path(__file__).with_name('compose-qualified-native-astc6.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class CompositionPolicy(unittest.TestCase):
    def entry(self, name):
        return {'logical': name, 'id': hashlib.sha256(name.lower().encode()).hexdigest(),
                'source': {'path': name + '/source'}, 'texture': {'path': name + '/texture'}}

    def test_case_identity_preserves_original_family_order(self):
        old = [self.entry('data/z.texture'), self.entry('data/a.texture')]
        new = [self.entry('data/A.texture')]
        old_specs = [{'source': 'Z', 'texture': 'ZT', 'baked': 'ZB'}, {'source': 'A', 'texture': 'AT', 'baked': 'AB'}]
        names, replacements, specs = c.ordered_substitution(old, new, old_specs, {new[0]['id']: {'baked': 'NEW'}})
        self.assertEqual(names, ['data/z.texture', 'data/a.texture'])
        self.assertEqual(specs[0], old_specs[0])
        self.assertEqual(specs[1]['source'], 'data/A.texture/source')
        self.assertEqual(specs[1]['baked'], 'NEW')
        self.assertEqual(new[0]['logical'], 'data/A.texture')

    def test_duplicate_original_case_alias_rejected(self):
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            c.ordered_substitution([self.entry('data/A'), self.entry('data/a')], [], [{}, {}], {})

    def test_duplicate_replacement_case_alias_rejected(self):
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            c.ordered_substitution([self.entry('data/a')], [self.entry('data/A'), self.entry('data/a')], [{}], {})

    def test_outside_original_family_rejected(self):
        with self.assertRaisesRegex(ValueError, 'outside'):
            c.ordered_substitution([self.entry('data/a')], [self.entry('data/b')], [{}], {})

    def test_bad_source_ID_rejected(self):
        entry = self.entry('data/a')
        entry['id'] = 'bad'
        with self.assertRaisesRegex(ValueError, 'ID'):
            c.ordered_substitution([self.entry('data/a')], [entry], [{}], {})

    def test_incomplete_original_order_rejected(self):
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            c.ordered_substitution([self.entry('data/a')], [], [], {})

    def test_missing_actual_output_rejected(self):
        with self.assertRaises(KeyError):
            c.ordered_substitution([self.entry('data/a')], [self.entry('data/a')], [{}], {})

    def test_original_table_entry_exact(self):
        table = bytes(160 + 2 * 1056)
        c.preserved_entry({'id': 1}, {'id': 1}, table, table, 0, 2)

    def test_unselected_recipe_mutation_rejected(self):
        table = bytes(160 + 2 * 1056)
        changed = bytearray(table)
        changed[160 + 100] = 1
        with self.assertRaisesRegex(ValueError, 'recipe'):
            c.preserved_entry({}, {}, table, changed, 0, 2)

    def test_unselected_tail_family_mutation_rejected(self):
        table = bytes(160 + 2 * 1056)
        changed = bytearray(table)
        changed[160 + 2 * 1040] = 1
        with self.assertRaisesRegex(ValueError, 'family'):
            c.preserved_entry({}, {}, table, changed, 0, 2)

    def test_unselected_manifest_mutation_rejected(self):
        table = bytes(160 + 1056)
        with self.assertRaisesRegex(ValueError, 'detail'):
            c.preserved_entry({'image_size': 5}, {'image_size': 6}, table, table, 0, 1)

    def test_unsafe_logical_rejected(self):
        for name in ['data/../x', '/data/a', 'data/a\\b', 'data/a\tb']:
            with self.subTest(name=name), self.assertRaises(ValueError):
                c.normalized(name)


if __name__ == '__main__':
    unittest.main()
