#!/usr/bin/env python3
"""Offline boundary checks using immutable existing native source inputs when present."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('texture_parity_build', Path(__file__).with_name('build.py'))
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)


class Boundaries(unittest.TestCase):
    def test_logical_paths_fail_closed(self):
        for value in ['../file', '/data/a', 'data/a\\b', 'data/../a', 'data/A.texture', 'data/a\n']:
            with self.subTest(value=value), self.assertRaises(ValueError): m.name(value)
        self.assertEqual(m.name('data/hd/a.texture'), 'data/hd/a.texture')

    def test_duplicate_complete_cache_rejected(self):
        with self.assertRaises(ValueError): m.descriptor_map([{'filename': 'data/hd/A.texture'}, {'filename': 'data/hd/a.texture'}])

    def test_source_mutation_or_wrong_hash_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp) / 'source'; p.write_bytes(b'original')
            h = m.Inputs(); h.pin(p, m.digest(b'original'))
            p.write_bytes(b'changed')
            with self.assertRaises(ValueError): h.finish()
            with self.assertRaises(ValueError): m.Inputs().pin(p, m.digest(b'wrong'))

    def test_baseline_layer_is_independent_and_originals_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); raw = root / 'raw'; ui = root / 'ui'
            (raw / 'data').mkdir(parents=True); (ui / 'data').mkdir(parents=True)
            (raw / 'data/original.data').write_bytes(b'original')
            (raw / 'data/.build.config').write_bytes(b'raw')
            (ui / 'data/.build.config').write_bytes(b'ui-index')
            (ui / 'sidecar').write_bytes(b'ui')
            m.link_baseline(raw, ui, root / 'work')
            self.assertEqual((root / 'work/data/.build.config').read_bytes(), b'ui-index')
            self.assertEqual((raw / 'data/.build.config').read_bytes(), b'raw')
            self.assertEqual((root / 'work/data/original.data').read_bytes(), b'original')


class RealSourceBoundaries(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = m.ROOT / '.local/build/wsi-ubwc-game-resources-v2'
        if not (root / 'entries.json').is_file(): raise unittest.SkipTest('private source artifacts not present')
        entries = json.loads((root / 'entries.json').read_text())
        details = json.loads((root / 'native-pack/native-pack.json').read_text())['entries']
        cache = json.loads((m.ROOT / '.local/build/native-qualified-astc6-full2838-compose-v1/native-overlay/files/cache').read_text())
        by_name = {x['filename'].lower(): x for x in cache}; cls.inputs = {}
        for entry, detail in zip(entries, details):
            source = json.loads(Path(entry['source']).read_text()); fmt = source['game_format']
            if fmt in cls.inputs: continue
            cls.inputs[fmt] = (by_name[detail['logical'].lower()], source, detail, Path(entry['texture']).read_bytes())
            if len(cls.inputs) == 4: break

    def test_actual_ASTC4_and_ASTC6_UNORM_sRGB_layouts(self):
        self.assertEqual(set(self.inputs), {95, 81, 99, 85})
        for native, source, detail, data in self.inputs.values():
            desc = m.ordinary_descriptor(native, source, detail)
            self.assertEqual(m.validate_texture(data, desc, source), desc['levels'])

    def test_wrong_family_marker_and_schema_are_rejected(self):
        native, source, detail, _ = self.inputs[99]
        wrong = dict(native, layoutData=0)
        with self.assertRaises(ValueError): m.ordinary_descriptor(wrong, source, detail)
        source = copy.deepcopy(source); source['source_descriptor']['depth'] = 2
        with self.assertRaises(ValueError): m.ordinary_descriptor(native, source, detail)

    def test_mutated_header_and_mip_payload_are_rejected(self):
        native, source, detail, data = self.inputs[95]
        desc = m.ordinary_descriptor(native, source, detail)
        wrong_header = bytearray(data); wrong_header[4] ^= 1
        with self.assertRaises(ValueError): m.validate_texture(bytes(wrong_header), desc, source)
        _, first, _, _ = m.repack.full_texture(data, desc, nvn_format=source['NVN_format'])
        wrong_payload = bytearray(data); wrong_payload[first] ^= 1
        with self.assertRaises(ValueError): m.validate_texture(bytes(wrong_payload), desc, source)

    def test_source_mip_proof_cannot_be_silently_changed(self):
        native, source, detail, data = self.inputs[85]
        desc = m.ordinary_descriptor(native, source, detail)
        source = copy.deepcopy(source); source['mips'][0]['sha256'] = '0' * 64
        with self.assertRaises(ValueError): m.validate_texture(data, desc, source)


if __name__ == '__main__': unittest.main()
