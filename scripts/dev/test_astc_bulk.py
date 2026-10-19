#!/usr/bin/env python3
"""Owned offline batch recovery/merge oracles, using the real encoder if supplied."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'dm/tools'), str(ROOT / 'dm/ios/tools')]
import astc_bulk as bulk
import astc_repack as repack
from test_astc_repack import owned_texture


class Bulk(unittest.TestCase):
    def test_quality_policy_uses_existing_metrics(self):
        mip = {'level': 0, 'dimensions': [64, 32],
               'official_astcenc_metrics': {'PSNR (LDR-RGB)': '38.5 dB'},
               'channels': {'RGBA_max_abs': [19, 18, 20, 0],
                            'alpha_coverage': {'128': {'before': 1024, 'after': 1024}}}}
        policy = {'min_rgb_psnr': 34, 'max_alpha_error': 8, 'max_alpha_coverage_fraction': 0.001}
        self.assertEqual(bulk.quality_failures({'mips': [mip]}, policy), [])
        mip['official_astcenc_metrics']['PSNR (LDR-RGB)'] = '32 dB'
        self.assertTrue(bulk.quality_failures({'mips': [mip]}, policy))
        mip['official_astcenc_metrics']['PSNR (LDR-RGB)'] = 'inf dB'
        mip['channels']['RGBA_max_abs'][3] = 9
        self.assertTrue(bulk.quality_failures({'mips': [mip]}, policy))
        mip['channels']['RGBA_max_abs'][3] = 0
        mip['channels']['alpha_coverage']['128']['after'] = 1030
        self.assertTrue(bulk.quality_failures({'mips': [mip]}, policy))
        mip['channels']['alpha_coverage'] = {'255': {'before': 0, 'after': 2048}}
        mip['channels']['RGBA_max_abs'][3] = 1
        mip['source_alpha_range'] = [254, 255]
        self.assertEqual(bulk.quality_failures({'mips': [mip]}, policy), [])
        mip['source_alpha_range'] = [254, 254]
        self.assertTrue(bulk.quality_failures({'mips': [mip]}, policy))
        self.assertEqual(bulk.category('data/own_nrm.texture', {'format': 62}), 'normal_keep4')
        for name in ('data/Own_Normal.texture', 'data/decal_NRM$NRM.texture'):
            self.assertEqual(bulk.category(name, {'format': 61}), 'normal_keep4')
        self.assertEqual(bulk.category('data/detailnormal/rock_alb.texture', {'format': 62}), 'albedo')

    @unittest.skipUnless(os.environ.get('ASTCENC_TEST'), 'set ASTCENC_TEST to actual astcenc')
    def test_interruption_resume_corruption_and_exact_baseline(self):
        with tempfile.TemporaryDirectory(prefix='astc-bulk-owned-') as temporary:
            root = Path(temporary)
            data, entries, rows = {}, {}, {}
            for suffix in ('a_alb', 'b_orm', 'unsupported_alb', 'untouched_nrm'):
                name = 'data/own_' + suffix + '.texture'
                raw, entry = owned_texture(7, 11)
                entry['filename'] = name
                if suffix == 'unsupported_alb':
                    entry['layoutType'] = 1
                data[name], entries[name] = raw, entry
                rows[name] = [None] * 11
            baseline = [dict(e, format=95) for e in entries.values()]
            cache = root / 'full4-cache.json'
            cache.write_text(json.dumps(baseline))
            index = root / 'full4-index.tsv'
            index.write_text(''.join(f'{name}\t{bytes(16).hex()}\t16\t16\n' for name in entries))
            selection = root / 'selection.txt'
            selection.write_text('\n'.join(list(entries)[:3]))
            options = argparse.Namespace(romfs=root, out=root / 'out', baseline_cache=cache,
                baseline_index=index, selection=selection, limit=3, astcenc=os.environ['ASTCENC_TEST'],
                preset='fast', threads=2, min_rgb_psnr=0, max_alpha_error=255,
                max_alpha_coverage_fraction=1, constant_alpha_max_error=255)
            original_convert = repack.convert
            calls = []
            def interrupted(*args, **kwargs):
                calls.append(args[1]['filename'])
                if len(calls) == 2:
                    raise RuntimeError('owned simulated interruption')
                return original_convert(*args, **kwargs)
            source = (rows, entries, {'owned_fixture': True})
            with mock.patch.object(bulk, 'load_source', return_value=source), \
                 mock.patch.object(bulk.touch_layout, '_read_entry', side_effect=lambda _, row: data[next(k for k, v in rows.items() if v is row)]):
                with mock.patch.object(repack, 'convert', side_effect=interrupted), self.assertRaises(RuntimeError):
                    bulk.batch(options)
                self.assertEqual(len(list((options.out / 'checkpoints').glob('*.json'))), 1)
                self.assertFalse((options.out / 'repl.tsv').exists())
                with mock.patch.object(repack, 'convert', wraps=original_convert) as convert:
                    bulk.batch(options)
                    self.assertEqual(convert.call_count, 1)
                report = json.loads((options.out / 'batch-report.json').read_text())
                self.assertEqual(report['checkpoints_reused'], 1)
                self.assertEqual([x['status'] for x in report['results']],
                                 ['converted', 'converted', 'unsupported_fallback'])
                merged = json.loads((options.out / 'files' / bulk.CACHE).read_text())
                self.assertEqual(merged[2:], baseline[2:])
                with mock.patch.object(repack, 'convert', side_effect=AssertionError('must not encode on cache hit')):
                    bulk.batch(options)
                report = json.loads((options.out / 'batch-report.json').read_text())
                self.assertEqual(report['checkpoints_reused'], 3)
                texture = options.out / 'items' / repack.sha(list(entries)[0].encode()) / 'texture'
                texture.write_bytes(b'owned corruption')
                with self.assertRaisesRegex(ValueError, 'corrupt'):
                    bulk.batch(options)
                self.assertFalse((options.out / 'repl.tsv').exists())
                self.assertFalse((options.out / 'batch-report.json').exists())
                self.assertEqual(json.loads(cache.read_text()), baseline)


if __name__ == '__main__':
    unittest.main()
