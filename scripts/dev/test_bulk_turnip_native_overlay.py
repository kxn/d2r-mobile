#!/usr/bin/env python3
"""Owned offline guards and actual existing CASC prebuilt roundtrip, no GPU claims."""
import base64
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

HERE = Path(__file__).parent
spec = importlib.util.spec_from_file_location('bulk_native_composer', HERE / 'compose-bulk-turnip-native-overlay.py')
bulk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bulk)
spec = importlib.util.spec_from_file_location('owned_casc_fixture', HERE / 'test_astc_compose.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class Bulk(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def descriptor(self):
        before = dict(filename='data/hd/env/texture/act1/owned.texture', format=62,
                      width=16, height=16, depth=1, levels=5, type=2, layoutType=3,
                      layoutData=3, thumbnailMip=2,
                      thumbnailBase64=base64.b64encode(b'owned original BC tail').decode())
        astc = dict(before, format=81, thumbnailBase64=base64.b64encode(b'owned matching ASTC tail').decode())
        native = dict(astc, layoutData=int.from_bytes(b'DMN\2\1\0\0\0', 'little'))
        return before, astc, native

    def native_table(self):
        details = [dict(id=n, logical=f'data/hd/character/owned-{n}.texture') for n in [1, 2]]
        record = dict(entries=details, driver_binary_sha256='ab' * 32,
                      producer_source_sha256='cd' * 32)
        record['pack_content_sha256'] = bulk.digest(bulk.canonical(dict(entries=details,
            driver_sha256=record['driver_binary_sha256'], producer_source_sha256=record['producer_source_sha256'])))
        table = bytearray(struct.pack('<8sIIIIQ', b'DMNTBL2\0', 2, 160, 2, 1040, 2272))
        table += bytes.fromhex(record['pack_content_sha256'] + record['driver_binary_sha256'] + record['producer_source_sha256']) + bytes(32)
        table += bytes(2 * 1056)
        table[128:160] = hashlib.sha256(table).digest()
        return table, record

    def test_multi_entry_table_authenticated(self):
        table, record = self.native_table()
        bulk.authenticate_table(table, record, record['entries'])

    def test_table_truncation_rejected(self):
        table, record = self.native_table()
        with self.assertRaisesRegex(ValueError, 'shape'):
            bulk.authenticate_table(table[:-1], record)

    def test_table_digest_corruption_rejected(self):
        table, record = self.native_table()
        table[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'digest'):
            bulk.authenticate_table(table, record)

    def test_table_relabel_driver_rejected(self):
        table, record = self.native_table()
        record['driver_binary_sha256'] = 'ff' * 32
        with self.assertRaisesRegex(ValueError, 'identities'):
            bulk.authenticate_table(table, record)

    def test_pack_entry_reordering_rejected(self):
        table, record = self.native_table()
        record['entries'].reverse()
        with self.assertRaisesRegex(ValueError, 'content digest'):
            bulk.authenticate_table(table, record)

    def test_reproduction_difference_rejected(self):
        table, record = self.native_table()
        with self.assertRaisesRegex(ValueError, 'reproduced'):
            bulk.authenticate_table(table, record, record['entries'][:1])

    def test_only_required_cache_changes_allowed(self):
        self.assertEqual(bulk.merge_descriptor(*self.descriptor()), ['format', 'layoutData', 'thumbnailBase64'])

    def test_dimensions_cannot_be_smuggled(self):
        before, astc, native = self.descriptor()
        astc['width'] = native['width'] = 32
        with self.assertRaisesRegex(ValueError, 'transformation'):
            bulk.merge_descriptor(before, astc, native)

    def test_old_BC_thumbnail_under_ASTC_rejected(self):
        before, astc, native = self.descriptor()
        native['thumbnailBase64'] = before['thumbnailBase64']
        with self.assertRaisesRegex(ValueError, 'thumbnail/metadata'):
            bulk.merge_descriptor(before, astc, native)

    def test_fullshape_thumbnail_rejected(self):
        values = self.descriptor()
        for value in values:
            value['thumbnailMip'] = 0
        with self.assertRaisesRegex(ValueError, 'independent'):
            bulk.merge_descriptor(*values)

    def test_filename_escaping_and_duplicate_fail_closed(self):
        for name in ['../escape', 'data/../escape', '/data/file', 'data/UPPER', 'data/a\tb', 'data/a\\b']:
            with self.assertRaises(ValueError):
                bulk.normalized(name)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            bulk.unique_entries([{'logical': 'data/owned'}, {'logical': 'data/owned'}])

    def test_empty_selection_rejected(self):
        with self.assertRaisesRegex(ValueError, 'empty'):
            bulk.unique_entries([])

    def test_original_case_preserved_in_pack_normalized_only_for_CASC(self):
        entries = [{'logical': 'data/hd/character/owned_HRT.texture'}]
        self.assertEqual(bulk.unique_entries(entries, allow_original_case=True),
                         ['data/hd/character/owned_hrt.texture'])
        self.assertEqual(entries[0]['logical'], 'data/hd/character/owned_HRT.texture')
        entries.append({'logical': 'data/hd/character/owned_hrt.texture'})
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            bulk.unique_entries(entries, allow_original_case=True)

    def test_source_CFT_matches_global_pinned_index(self):
        key = bytes.fromhex('aa' * 16)
        index = {'data/owned': (key, 19, 73)}
        info = dict(logical='data/owned', ekey=key.hex(), encoded_bytes=19, decoded_bytes=73)
        bulk.source_index_matches(index, 'data/owned', info)
        for change in [{'logical': 'data/other'}, {'ekey': 'bb' * 16},
                       {'encoded_bytes': 20}, {'decoded_bytes': 74}]:
            with self.assertRaises(ValueError):
                bulk.source_index_matches(index, 'data/owned', info | change)

    def test_hash_memo_once_and_mutation_rejected(self):
        path = self.root / 'archive'
        path.write_bytes(b'owned large source')
        other = self.root / 'same-archive'
        other.symlink_to(path)
        hashes = bulk.Fingerprints()
        self.assertEqual(hashes.file(path), hashes.file(other))
        self.assertEqual(hashes.hashed_bytes, len(b'owned large source'))
        self.assertEqual(len(hashes.finish()), 1)
        path.write_bytes(b'changed source content')
        with self.assertRaisesRegex(ValueError, 'changed'):
            hashes.file(path)

    def test_mutation_before_manifest_publication_rejected(self):
        path = self.root / 'source'
        path.write_bytes(b'first')
        hashes = bulk.Fingerprints()
        hashes.file(path)
        path.write_bytes(b'second')
        with self.assertRaisesRegex(ValueError, 'before publication'):
            hashes.finish()

    def test_encoded_offset_overflow_rejected(self):
        class Offset:
            def tell(self):
                return (1 << 40) - 1
        with self.assertRaisesRegex(ValueError, 'offset'):
            bulk.encode_one(Offset(), b'owned', 'b:z', (0, 9))

    def test_real_CASC_multi_prebuilt_roundtrip_and_untouched_CFT(self):
        raw = self.root / 'raw'
        contents = {'data/own-one.texture': b'original one', 'data/own-two.texture': b'original two',
                    'data/untouched.texture': b'original untouched', bulk.CACHE: b'[]'}
        fixture.owned_romfs(raw, contents)
        manifest = bulk.casc.read_manifest(raw)
        baseline = bytes(bulk.casc.load_vfs1(raw))
        cft, size = struct.unpack_from('>II', baseline, 28)
        location = bulk.casc.available_location(raw / 'data')
        name = bulk.casc.data_filename(*location)
        archive = raw / 'data' / name
        updates = {}
        replacements = {'data/own-one.texture': b'owned native marker one' * 100,
                        'data/own-two.texture': b'owned native marker two' * 321}
        with archive.open('wb') as stream:
            for logical, texture in replacements.items():
                entry = bulk.encode_one(stream, texture, manifest[logical][8], location)
                updates[logical] = entry
                bulk.read_indexed(raw, manifest[logical], dict(ekey=entry[0].hex(),
                    encoded_bytes=entry[1], encoding=manifest[logical][8]), texture)
        cache = self.root / 'new-cache'
        cache.write_bytes(b'["owned cache delta"]')
        overlay = self.root / 'overlay'
        bulk.casc.make_overlay(raw, overlay, {bulk.CACHE: cache}, updates)
        after = bytes(bulk.casc.load_vfs1(overlay))
        cp = bulk.casc.cft_position(baseline, cft, cft + size, manifest, bulk.CACHE)
        updates[bulk.CACHE] = (after[cp:cp + 16], struct.unpack_from('>I', after, cp + 16)[0],
                              struct.unpack_from('>I', after, cp + 21)[0])
        proof = bulk.comp.prove_tvfs(raw, overlay, updates)
        self.assertTrue(proof['VFS_only_selected_CFT_fields_changed'])
        untouched = bulk.casc.cft_position(baseline, cft, cft + size, manifest, 'data/untouched.texture')
        self.assertEqual(baseline[untouched:untouched + 25], after[untouched:untouched + 25])
        for logical, texture in replacements.items():
            entry = updates[logical]
            bulk.read_indexed(overlay, manifest[logical], dict(ekey=entry[0].hex(), encoded_bytes=entry[1],
                              encoding=manifest[logical][8]), texture)
        broken = dict(updates)
        del broken['data/own-two.texture']
        with self.assertRaisesRegex(ValueError, 'outside selected'):
            bulk.comp.prove_tvfs(raw, overlay, broken)


if __name__ == '__main__':
    unittest.main()
