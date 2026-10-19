#!/usr/bin/env python3
"""Declared-array format closure, including original CASC/CFT byte oracles."""
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'dm/tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import astc_array_groups as arrays
import astc_compose as comp
import casc_overlay as casc
from test_astc_compose import owned_romfs


def biome(layers):
    return {'type': 'Biome', **{tier: {'type': 'TerrainData', 'terrainLayers': layers}
                               for tier in arrays.TIERS}}


def layer(name):
    return {'type': 'TerrainLayer', **{role: name for role in arrays.ROLES}}


def group(name, members, unknown=None):
    return {'id': name, 'source': 'owned', 'pointer': '/' + name, 'role': 'ORM',
            'ordered_members': members, 'members': sorted(set(members)), 'unknown': unknown or []}


class ArrayClosure(unittest.TestCase):
    def test_shared_members_fixed_point_and_whole_qualified_group(self):
        names = ['data/' + c + '.texture' for c in 'abcdef']
        before = {n: {'filename': n, 'format': 95} for n in names}
        after = {n: dict(v, format=99) for n, v in before.items()}
        # Reverse topological order requires a later round, rather than merely
        # checking groups independently against the encoder accepted set.
        groups = [group('cd', names[2:4]), group('ac', [names[0], names[2]]),
                  group('ab', names[:2]), group('ef', names[4:])]
        chosen, proof = arrays.close_selection(before, after, {n: n for n in names if n != names[1]},
                                               groups, {}, {names[1]: 'quality-fallback'})
        self.assertEqual(set(chosen), set(names[4:]))
        self.assertEqual([len(r) for r in proof['rounds']], [1, 1, 1])
        self.assertEqual(proof['events'][0]['absent_members'], {names[1]: 'quality-fallback'})
        self.assertEqual(arrays.close_selection(before, after, chosen, groups, {})[0], chosen)

    def test_normal_missing_metadata_unknown_and_mixed_base_are_preserved(self):
        names = ['data/a.texture', 'data/b.texture']
        before = {n: {'filename': n, 'format': 95} for n in names}
        after = {n: dict(v, format=99) for n, v in before.items()}
        scenarios = [(before, after, {names[0]: 'payload'}, group('normal', names)),
                     ({names[0]: before[names[0]]}, after, {n: n for n in names}, group('missing', names)),
                     (before, after, {n: n for n in names}, group('partial', names, ['missing-layer'])),
                     ({**before, names[1]: dict(before[names[1]], format=81)}, after,
                      {n: n for n in names}, group('mixed', names)),
                     (before, {**after, names[1]: dict(after[names[1]], format=85)},
                      {n: n for n in names}, group('mixed-target', names))]
        for original, candidate, selected, declaration in scenarios:
            with self.subTest(declaration=declaration['id']):
                self.assertEqual(arrays.close_selection(original, candidate, selected, [declaration], {})[0], {})

    def test_original_declaration_hash_and_unknown_schema_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            raw = Path(temporary)
            name = 'data/owned_definition.json'  # Membership does not rely on scene/directory names.
            source = json.dumps(biome([layer('data/a.texture'), layer('data/b.texture')])).encode()
            owned_romfs(raw, {name: source, 'data/unrelated_ui.json': b'{"rect": [0,],}'})
            manifest = casc.read_manifest(raw)
            groups, proof = arrays.discover(manifest, lambda row: comp.touch_layout._read_entry(str(raw), row))
            self.assertEqual(len(groups), 20)
            self.assertEqual(groups[0]['members'], ['data/a.texture', 'data/b.texture'])
            self.assertEqual(proof['group_sources'][0]['sha256'], comp.byte_sha(source))
            self.assertEqual(len(proof['json_census']), 2)
            self.assertEqual(len(proof['unparsed_unrelated_json']), 1)
            for value in ({'type': 'TextureArray', 'textures': ['data/a.texture']},
                          {'type': 'NewSchema', 'texture_arrays': []}):
                with self.assertRaisesRegex(ValueError, 'unsupported explicit TextureArray'):
                    arrays.discover({name: manifest[name]}, lambda _: json.dumps(value).encode())
            with self.assertRaisesRegex(ValueError, 'unparsed possible array'):
                arrays.discover({name: manifest[name]}, lambda _: b'{"type":"Biome","terrainLayers": [],}')
            bad = biome([layer('data/a.texture')]); bad['terrainDataOther'] = bad['terrainDataLow']
            with self.assertRaisesRegex(ValueError, 'unrecognized TerrainData'):
                arrays.biome_groups(name, bad)

    @unittest.skipUnless(os.environ.get('ASTC_ARRAY_ORIGINAL_ROOT'), 'optional original resource regression')
    def test_original_terrain_mixture_and_shared_closure(self):
        evidence_root = Path(os.environ['ASTC_ARRAY_ORIGINAL_ROOT'])
        raw = evidence_root / 'work/data/switch/3.3.94012/Update/RomFS'
        composition_path = evidence_root / '.local/build/astc6-full-composition-v1/composition.json'
        composition = json.loads(composition_path.read_text())
        before_path = evidence_root / '.local/build/android-astc-full/work/files' / comp.CACHE
        batch_path = evidence_root / '.local/build/astc6-bulk-full-v1/files' / comp.CACHE
        # The historical complete composition is evidence only, never modified.
        self.assertTrue(composition['complete'])
        self.assertEqual(comp.Hashes().file(before_path), composition['inputs'][str(before_path)])
        before = comp.descriptor_map(json.loads(before_path.read_text()))
        candidate = comp.descriptor_map(json.loads(batch_path.read_text()))
        manifest = casc.read_manifest(raw)
        groups, proof = arrays.discover(manifest, lambda row: comp.touch_layout._read_entry(str(raw), row))
        selected, proof = arrays.close_selection(before, candidate,
            {n: n for n in composition['selected']}, groups, proof)
        self.assertEqual((len(proof['group_sources']), len(groups)), (58, 1160))
        self.assertEqual((len(composition['selected']), len(selected)), (11638, 11605))
        self.assertEqual([len(r) for r in proof['rounds']], [31, 2])
        for path, role in [('act1_outdoors.json', 'ORM'), ('act1_outdoors.json', 'GrassTexture'),
                           ('act2_frontend_biome.json', 'ORM')]:
            matches = [g for g in groups if g['source'].endswith('/' + path) and g['role'] == role]
            self.assertEqual(len(matches), 4)
            for g in matches:
                self.assertFalse(set(g['members']) & selected.keys())
        for g in groups:
            active = set(g['members']) & selected.keys()
            if active:
                self.assertEqual(active, set(g['members']))
                self.assertEqual(len({candidate[n]['format'] for n in active}), 1)
        output = os.environ.get('ASTC_ARRAY_PROOF_OUTPUT')
        if output:
            Path(output).write_text(json.dumps(proof, indent=2) + '\n')

    @unittest.skipUnless(sys.version_info >= (3, 13), 'bounded original CASC requires Python3.13')
    def test_eight_owned_members_preserve_fallback_descriptors_and_original_cft(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            names = ['data/' + c + '.texture' for c in 'abcdef'] + ['data/g_nrm.texture', 'data/h.texture']
            baseline = [{'filename': n, 'format': 95, 'width': 8, 'height': 8} for n in names]
            before = comp.descriptor_map(baseline)
            converted = {n: dict(v, format=99) for n, v in before.items()}
            candidates = {}
            for n in names:
                if n == names[1]:
                    converted[n] = before[n]  # Original quality fallback.
                    continue
                path = root / Path(n).name
                path.write_bytes(('owned ASTC6 payload: ' + n).encode())
                candidates[n] = path
            selected, normals, _ = comp.final_selection(baseline, converted, candidates)
            self.assertEqual(normals, [names[6]])
            groups = [group('cd', names[2:4]), group('ac', [names[0], names[2]]),
                      group('ab', names[:2]), group('ef', names[4:6]), group('gh', names[6:])]
            selected, proof = arrays.close_selection(before, converted, selected, groups, {})
            self.assertEqual(set(selected), set(names[4:6]))
            self.assertEqual(set(proof['preserved']), {names[i] for i in (0, 2, 3, 7)})
            cache = [converted[n] if n in selected else before[n] for n in names]
            contents = {n: ('owned original full4: ' + n).encode() for n in names}
            contents[comp.CACHE] = json.dumps(baseline).encode()
            raw = root / 'raw'; owned_romfs(raw, contents)
            cache_path = root / 'cache'; cache_path.write_text(json.dumps(cache))
            replacements = selected | {comp.CACHE: cache_path}
            declaration = root / 'repl.tsv'
            declaration.write_text(''.join(f'{n}\t{p}\n' for n, p in replacements.items()))
            comp.run_casc(['pack', raw, root / 'pack9', declaration, '0-9'], root, 'pack.log')
            updates = casc.read_prebuilt(root / 'pack9/pack_index.tsv')
            self.assertEqual(set(updates), set(names[4:6]) | {comp.CACHE})
            (raw / 'data' / comp.PACK9).symlink_to(root / 'pack9' / comp.PACK9)
            empty = root / 'empty.tsv'; empty.write_text('')
            overlay = root / 'overlay'
            comp.run_casc([raw, overlay, empty, '--prebuilt', root / 'pack9/pack_index.tsv'], root, 'overlay.log')
            self.assertTrue(comp.prove_tvfs(raw, overlay, updates)['VFS_only_selected_CFT_fields_changed'])
            original, changed = bytes(casc.load_vfs1(raw)), bytes(casc.load_vfs1(overlay))
            manifest = casc.read_manifest(raw)
            start, size = struct.unpack_from('>II', original, 28)
            key, encoded, _ = updates[comp.CACHE]
            rebuilt = comp.descriptor_map(json.loads(casc.read_data_file(raw / 'data', key, encoded)))
            for name in set(names) - selected.keys():
                self.assertEqual(rebuilt[name], before[name])
                position = casc.cft_position(original, start, start + size, manifest, name)
                self.assertEqual(original[position:position + 25], changed[position:position + 25])
                self.assertEqual(comp.touch_layout._read_entry(str(raw), manifest[name]), contents[name])


if __name__ == '__main__':
    unittest.main()
