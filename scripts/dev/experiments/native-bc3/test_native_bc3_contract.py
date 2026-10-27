#!/usr/bin/env python3
"""Owned protocol/packer tests. Synthetic opaque bytes are never GPU evidence."""
import argparse
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
SPEC = importlib.util.spec_from_file_location('native_packer', ROOT / 'scripts/dev/pack-native-texture.py')
PACK = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(PACK)
FORMATS = [(137, 61, 0x44), (138, 62, 0x48), (157, 95, 0x79), (158, 81, 0x87), (165, 99, 0x7d), (166, 85, 0x8b)]
CC, SANITIZE = 'cc', False

def digest(data): return hashlib.sha256(data).digest()
def canonical(data): return json.dumps(data, sort_keys=True, separators=(',', ':')).encode()

def owned_entry(folder, index, vk, game, width=8, height=8):
    folder.mkdir(parents=True)
    logical = f'data/hd/owned-{index}.texture'
    thumbnail = bytes(range(256)) * 4
    desc = {'format': game, 'width': width, 'height': height, 'levels': 4, 'type': 2, 'depth': 1,
            'layoutType': 3, 'layoutData': 0, 'filename': logical, 'thumbnailMip': 2,
            'thumbnailBase64': base64.b64encode(thumbnail).decode()}
    # Original source payloads are whole original GOB-sized mip spans.
    original = bytearray(36 + 4 * 8)
    original[:4] = b'<DE('; original[4] = game; original[6] = 2
    struct.pack_into('<IIH', original, 8, width, height, 1); original[18] = 3
    struct.pack_into('<Q', original, 19, 0); struct.pack_into('<I', original, 28, 4); struct.pack_into('<I', original, 32, 4)
    for n in range(4):
        struct.pack_into('<Ii', original, 36 + n * 8, 512, 68 + n * 512 - (40 + n * 8))
        original.extend(bytes([index * 4 + n]) * 512)
    texture = folder / 'source.texture'; texture.write_bytes(original)
    declaration = {'source_descriptor': desc, 'source_texture': {'sha256': digest(original).hex()}, 'scope': 'owned synthetic metadata only'}
    source = folder / 'source.json'; source.write_bytes(canonical(declaration))
    linear = bytes(range(112)); (folder / 'resource-input.bin').write_bytes(linear)
    baked = folder / 'baked'; baked.mkdir(); payload = bytes([index]) * 16384
    metadata = canonical({'owned': True, 'GPU_executed': False, 'format': vk})
    wire = bytearray(992); struct.pack_into('<8sIIQQ', wire, 0, b'DMNRES1\0', 1, 992, len(payload), 4096)
    values = [vk, width, height, 1, 4, 1, 1, 1, 0, 0x400007, 0, 0, 0, 128, 1, 0xfffffffd, 0x5143, 0, 0, 18]
    struct.pack_into('<20I', wire, 32, *values)
    wire[112:128] = bytes.fromhex('11' * 16); wire[128:144] = bytes.fromhex('22' * 16); wire[144:160] = bytes.fromhex('11' * 16)
    wire[160:192] = digest(linear); wire[192:224] = digest(payload); wire[224:256] = digest(source.read_bytes()); wire[256:288] = digest(metadata); wire[288:320] = bytes.fromhex('33' * 32)
    for n in range(4): struct.pack_into('<5Q', wire, 320 + 40 * n, n * 4096, 4096, 0, 0, 0)
    wire[960:992] = digest(wire)
    for name, data in [('native-resource-layout.bin', wire), ('native-resource.bin', payload), ('native-resource.json', metadata)]: (baked / name).write_bytes(data)
    return {'source': str(source), 'texture': str(texture), 'baked': str(baked)}

class NativeBC3Contract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler_tmp = tempfile.TemporaryDirectory(); cls.binary = Path(cls.compiler_tmp.name) / 'metadata-test'
        command = [CC, '-std=gnu11', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-DMBEDTLS_CONFIG_FILE="dm_sha_config.h"',
                   '-I' + str(ROOT / 'dm/gfx/nvn'), '-I' + str(ROOT / 'dm/third_party/runtime-codecs'),
                   str(Path(__file__).with_name('t_native_bc3_metadata.c')), str(ROOT / 'dm/gfx/nvn/nvn_native_layout.c'),
                   str(ROOT / 'dm/gfx/nvn/nvn_tex.c'), str(ROOT / 'dm/third_party/runtime-codecs/sha256.c'), '-o', str(cls.binary)]
        if SANITIZE: command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError('owned fixture compile failed: ' + result.stderr)
    @classmethod
    def tearDownClass(cls): cls.compiler_tmp.cleanup()
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.path = Path(self.tmp.name)
        self.entries = [owned_entry(self.path / str(n), n, vk, game) for n, (vk, game, _) in enumerate(FORMATS)]
        self.driver = self.path / 'owned-driver-not-executable'; self.driver.write_bytes(b'owned fake driver identity, never selected by runtime')
        self.manifest = self.path / 'driver.json'; self.manifest.write_text(json.dumps({'driver': {'path': str(self.driver), 'sha256': digest(self.driver.read_bytes()).hex()}, 'build_id': ['11' * 20]}))
    def tearDown(self): self.tmp.cleanup()
    def mutate_wire(self, entry, change):
        p = Path(entry['baked']) / 'native-resource-layout.bin'; b = bytearray(p.read_bytes()); change(b); b[960:] = bytes(32); b[960:] = digest(b); p.write_bytes(b)
    def test_all_six_formats_pack_and_real_C_parser_storage_tail(self):
        out = self.path / 'pack'; result = PACK.build(self.entries, out, self.manifest)
        self.assertFalse(result['game_modules_executed']); self.assertFalse(result['deployable_game_pack']); self.assertFalse(result['runtime_default_enabled'])
        table = out / 'native-layouts.bin'
        for n, (_, game, nvn) in enumerate(FORMATS):
            wire = table.read_bytes(); self.assertEqual(struct.unpack_from('<i', wire, 160 + n * 1040 + 12)[0], nvn)
            desc = json.loads((out / f'data/hd/owned-{n}.descriptor.json').read_text()); self.assertEqual(desc['format'], game)
            self.assertEqual(desc['thumbnailBase64'], json.loads(Path(self.entries[n]['source']).read_text())['source_descriptor']['thumbnailBase64'])
        run = subprocess.run([str(self.binary), str(table)], check=True, capture_output=True, text=True)
        report = json.loads(run.stdout); self.assertEqual(report['result'], 'passed'); self.assertFalse(report['GPU_executed'])
    def test_srgb_cannot_relabel_unorm_source(self):
        self.mutate_wire(self.entries[0], lambda w: struct.pack_into('<I', w, 32, 138))
        with self.assertRaisesRegex(ValueError, 'declaration mismatch'): PACK.load_entry(self.entries[0], 1)
    def test_other_BC_formats_remain_outside_native_contract(self):
        self.mutate_wire(self.entries[0], lambda w: struct.pack_into('<I', w, 32, 131))
        with self.assertRaisesRegex(ValueError, 'unsupported'): PACK.load_entry(self.entries[0], 1)
    def test_digest_payload_and_source_corruption_rejected(self):
        for key in ['native-resource-layout.bin', 'native-resource.bin', 'native-resource.json']:
            e = owned_entry(self.path / ('corrupt-' + key), 7, 137, 61); p = Path(e['baked']) / key; b = bytearray(p.read_bytes()); b[-1] ^= 1; p.write_bytes(b)
            with self.assertRaises(ValueError): PACK.load_entry(e, 1)
    def test_native_physical_overlap_and_unused_mips_rejected(self):
        self.mutate_wire(self.entries[0], lambda w: struct.pack_into('<Q', w, 360, 0))
        with self.assertRaisesRegex(ValueError, 'bounds'): PACK.load_entry(self.entries[0], 1)
        self.mutate_wire(self.entries[1], lambda w: struct.pack_into('<Q', w, 320 + 15 * 40, 1))
        with self.assertRaisesRegex(ValueError, 'unused'): PACK.load_entry(self.entries[1], 1)
    def test_capacity_alignment_and_table_bounds_unchanged(self):
        with self.assertRaisesRegex(ValueError, 'empty/oversized'): PACK.build([], self.path / 'none', self.manifest)
        with self.assertRaisesRegex(ValueError, 'empty/oversized'): PACK.build([None] * 65537, self.path / 'too-many', self.manifest)
        self.mutate_wire(self.entries[0], lambda w: struct.pack_into('<Q', w, 24, 2048))
        with self.assertRaisesRegex(ValueError, 'unsupported'): PACK.load_entry(self.entries[0], 1)
        self.mutate_wire(self.entries[1], lambda w: struct.pack_into('<Q', w, 16, (16 << 20) + 1))
        with self.assertRaisesRegex(ValueError, 'unsupported'): PACK.load_entry(self.entries[1], 1)
    def test_malformed_original_mip_table_rejected_after_correct_source_hash(self):
        e = self.entries[0]; p = Path(e['texture']); b = bytearray(p.read_bytes()); struct.pack_into('<i', b, 40, -1); p.write_bytes(b)
        src = Path(e['source']); v = json.loads(src.read_text()); v['source_texture']['sha256'] = digest(b).hex(); src.write_bytes(canonical(v))
        self.mutate_wire(e, lambda w: w.__setitem__(slice(224, 256), digest(src.read_bytes())))
        with self.assertRaisesRegex(ValueError, 'contiguous'): PACK.load_entry(e, 1)
    def test_invalid_dimensions_match_runtime_capacity_contract(self):
        for n, (width, height) in enumerate([(0, 8), (8, 0), (4097, 8), (8, 4097)]):
            e = owned_entry(self.path / f'bad-dim-{n}', 6, 137, 61, width, height)
            with self.assertRaisesRegex(ValueError, 'unsupported'): PACK.load_entry(e, 1)
    def test_explicit_new_source_producer_matches_native_wire(self):
        e = self.entries[0]; source = Path(e['source']); declaration = json.loads(source.read_text())
        for producer, valid in [('33' * 32, True), ('34' * 32, False), ('G' * 64, False), (None, False)]:
            declaration['source_profile'] = 'original-bc3-raw-casc-v1'
            declaration['intended_producer_sha256'] = producer
            source.write_bytes(canonical(declaration))
            self.mutate_wire(e, lambda w: w.__setitem__(slice(224, 256), digest(source.read_bytes())))
            if valid:
                entry, *_ = PACK.load_entry(e, 1)
                self.assertEqual(entry[48 + 288:48 + 320], bytes.fromhex('33' * 32))
            else:
                with self.assertRaisesRegex(ValueError, 'intended producer'): PACK.load_entry(e, 1)
    def test_complete_descriptor_cache_keeps_unselected_and_thumbnail_exact(self):
        selected = json.loads(Path(self.entries[0]['source']).read_text())['source_descriptor']; untouched = dict(selected, filename='data/hd/untouched.texture', format=62)
        cache = self.path / 'cache.json'; cache.write_bytes(canonical([selected, untouched])); out = self.path / 'cache-pack'
        result = PACK.build([self.entries[0]], out, self.manifest, cache); actual = json.loads((out / 'data/hd/texture_desc_cache.json').read_text())
        self.assertEqual(actual[1], untouched); self.assertEqual(actual[0]['thumbnailBase64'], selected['thumbnailBase64']); self.assertNotEqual(actual[0]['layoutData'], selected['layoutData'])
        self.assertTrue(result['complete_cache_merge']['unselected_entries_exact'])

if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--cc', default='cc'); parser.add_argument('--sanitize', action='store_true'); args, rest = parser.parse_known_args(); CC, SANITIZE = args.cc, args.sanitize
    unittest.main(argv=[__file__, *rest])
