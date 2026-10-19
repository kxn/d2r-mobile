#!/usr/bin/env python3
"""Owned BLTE/TVFS composition, normal aliases and fail-closed verification."""
import argparse
import fcntl
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'dm/tools'))
import astc_compose as comp
import casc_overlay as casc


def make_tvfs(entries):
    data = bytearray(46)
    data[:4] = b'TVFS'
    struct.pack_into('>II', data, 28, 46, len(entries) * 25)
    struct.pack_into('>II', data, 38, 46 + len(entries) * 25, 4)
    for key, encoded, decoded in entries:
        data += key + struct.pack('>IBI', encoded, 0, decoded)
    return data + b'b:z\0'


def owned_romfs(root, contents):
    data = root / 'data'
    data.mkdir(parents=True)
    archive, entries, rows = bytearray(), [], []
    for name, content in contents.items():
        packed = casc.blte_encode(content)
        key = casc.ekey_for(packed, 0, 1, len(archive))
        archive += packed
        entries.append((key, len(packed), len(content)))
        rows.append('|'.join(['1', '0', '0', '1', hashlib.md5(content).hexdigest(), str(len(content)),
                              key.hex(), str(len(packed)), 'b:z', 'data:' + name, '0']))
    vfs = make_tvfs(entries)
    packed = casc.blte_encode(vfs)
    vfs_key = casc.ekey_for(packed, 0, 1, len(archive))
    archive += packed
    root_vfs = make_tvfs([(vfs_key, len(packed), len(vfs))])
    root_packed = casc.blte_encode(root_vfs)
    root_key = casc.ekey_for(root_packed, 0, 1, len(archive))
    archive += root_packed
    (data / '00-00000001.data').write_bytes(archive)
    lines = []
    for name, content, packed, key in [('vfs-1', vfs, packed, vfs_key),
                                      ('vfs-root', root_vfs, root_packed, root_key),
                                      ('vfs-2', root_vfs, root_packed, root_key)]:
        lines.extend([f'{name} = {hashlib.md5(content).hexdigest()} {key.hex()}\n',
                      f'{name}-size = {len(content)} {len(packed)}\n'])
    (data / '.build.config').write_text(''.join(lines))
    (data / 'build_manifest.psv').write_text('# owned fixture\n' + '\n'.join(rows) + '\n')
    for number in range(2, 9):
        (data / f'00-{number:08x}.data').write_bytes(b'owned occupied slot')


class Compose(unittest.TestCase):
    def test_partial_and_active_batch_cannot_create_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            batch = root / 'batch'
            batch.mkdir()
            (batch / '.lock').touch()
            options = argparse.Namespace(batch=batch, raw=root, out=root / 'output',
                                         baseline_cache=root / 'unused', baseline_index=root / 'unused')
            with self.assertRaisesRegex(ValueError, 'partial'):
                comp.compose(options)
            (batch / 'batch-report.json').write_text('{"complete":false}')
            with self.assertRaisesRegex(ValueError, 'partial'):
                comp.compose(options)
            with (batch / '.lock').open('r') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                with self.assertRaises(BlockingIOError):
                    comp.compose(options)
            self.assertFalse(options.out.exists())

    def test_artifact_hash_and_extra_files_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifact = root / 'texture'
            artifact.write_bytes(b'owned expected bytes')
            expected = {'texture': comp.Hashes().file(artifact)}
            comp.verify_map(root, expected, comp.Hashes())
            artifact.write_bytes(b'owned corrupt bytes')
            with self.assertRaisesRegex(ValueError, 'corrupt'):
                comp.verify_map(root, expected, comp.Hashes())
            artifact.write_bytes(b'owned expected bytes')
            (root / 'unrecorded').touch()
            with self.assertRaisesRegex(ValueError, 'unrecorded'):
                comp.verify_map(root, expected, comp.Hashes())

    @unittest.skipUnless(sys.version_info >= (3, 13), 'bounded original CASC child requires Python3.13')
    def test_normal_aliases_and_actual_casc_roundtrip(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            names = ['data/own_nrm.texture', 'data/own_Normal.texture', 'data/own_nrm$nrm.texture',
                     'data/detailnormal/own_alb.texture']
            names = [name.lower() for name in names]
            baseline = [{'filename': name, 'format': 95, 'width': 8} for name in names]
            converted = {name: dict(entry, format=99) for name, entry in zip(names, baseline)}
            candidates = {}
            for number, name in enumerate(names):
                path = root / f'candidate-{number}'
                path.write_bytes(('owned converted ' + name).encode())
                candidates[name] = path
            accepted, aliases, merged = comp.final_selection(baseline, converted, candidates)
            self.assertEqual(list(accepted), [names[3]])
            self.assertEqual(set(aliases), set(names[:3]))
            self.assertEqual(merged[:3], baseline[:3])
            self.assertEqual(merged[3]['format'], 99)
            contents = {name: ('owned baseline ' + name).encode() for name in names}
            contents[comp.CACHE] = json.dumps(baseline).encode()
            raw = root / 'raw'
            owned_romfs(raw, contents)
            merged_cache = root / 'merged-cache'
            merged_cache.write_text(json.dumps(merged))
            replacements = accepted | {comp.CACHE: merged_cache}
            declaration = root / 'repl.tsv'
            declaration.write_text(''.join(f'{n}\t{p}\n' for n, p in replacements.items()))
            comp.run_casc(['pack', raw, root / 'pack9', declaration, '0-9'], root, 'pack.log')
            updates = casc.read_prebuilt(root / 'pack9/pack_index.tsv')
            self.assertEqual(set(updates), {names[3], comp.CACHE})
            self.assertFalse(set(names[:3]) & set(updates))
            (raw / 'data' / comp.PACK9).symlink_to(root / 'pack9' / comp.PACK9)
            empty = root / 'empty.tsv'
            empty.write_text('')
            overlay = root / 'overlay'
            comp.run_casc([raw, overlay, empty, '--prebuilt', root / 'pack9/pack_index.tsv'], root, 'overlay.log')
            proof = comp.prove_tvfs(raw, overlay, updates)
            self.assertTrue(proof['VFS_only_selected_CFT_fields_changed'])
            self.assertTrue((overlay / 'data' / comp.ROOT10).is_file())
            key, encoded, _ = updates[comp.CACHE]
            decoded_cache = json.loads(casc.read_data_file(raw / 'data', key, encoded))
            self.assertEqual(decoded_cache[:3], baseline[:3])
            original = bytes(casc.load_vfs1(raw))
            changed = bytes(casc.load_vfs1(overlay))
            manifest = casc.read_manifest(raw)
            start, size = struct.unpack_from('>II', original, 28)
            for name in names[:3]:
                position = casc.cft_position(original, start, start + size, manifest, name)
                self.assertEqual(original[position:position + 25], changed[position:position + 25])
            corrupt = bytearray(changed)
            corrupt[7] ^= 1  # Outside all allowed CFT fields.
            with mock.patch.object(casc, 'load_vfs1', side_effect=lambda p: original if p == raw else corrupt), \
                 self.assertRaisesRegex(ValueError, 'outside selected'):
                comp.prove_tvfs(raw, overlay, updates)


if __name__ == '__main__':
    unittest.main()
