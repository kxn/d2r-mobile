#!/usr/bin/env python3
"""Offline owned BC3/container boundary oracles; no game files or GPU needed."""
import base64
import ctypes as C
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'dm/tools'), str(ROOT / 'dm/ios/tools')]
import astc_repack as repack


def owned_texture(w, h, srgb=False):
    levels = max(w, h).bit_length()
    entry = {'filename': 'own-NPOT.texture', 'type': 2, 'width': w, 'height': h,
             'depth': 1, 'levels': levels, 'format': 62 if srgb else 61,
             'layoutType': 3, 'layoutData': 0x1000700000002, 'treatAsDecal': False}
    d = repack.desc(entry, repack.NVN_BC3[entry['format']])
    offsets = repack.geometry(d)
    tiled = C.create_string_buffer(offsets[-1])
    linear_mips = []
    for level in range(levels):
        mw, mh = repack.dimensions(w, h, level)
        blocks = []
        for k in range(repack.block_bytes(mw, mh, 4) // 16):
            # Existing BC3 codec consumes these valid test bit patterns. No
            # new compressor: mixed alpha palette modes and distinct colors.
            alpha = bytes(((k * 29 + level * 17) % 256, (255 - k * 7) % 256))
            indices = bytes((k * 13 + j * 31) % 256 for j in range(6))
            color = struct.pack('<HHI', (0xf800 + k * 89) & 65535,
                                (0x07e0 + k * 211) & 65535, (k * 0x5a5aa5a5) & 0xffffffff)
            blocks.append(alpha + indices + color)
        raw = b''.join(blocks)
        repack.swizzle(d, tiled, level, raw, 4)
        if repack.deswizzle(d, tiled.raw, level, 4) != raw:
            raise AssertionError('owned original block identifiers changed')
        linear_mips.append(raw)
    first = 36 + 8 * levels
    header = bytearray(first)
    header[:4] = b'<DE(';header[4] = entry['format'];header[6] = 2;header[7] = levels
    struct.pack_into('<II', header, 8, w, h);struct.pack_into('<H', header, 16, 1)
    header[18] = 3;struct.pack_into('<Q', header, 19, entry['layoutData'])
    struct.pack_into('<II', header, 28, levels, 4)
    data = repack.rebuild_table(header, 36, [tiled.raw[offsets[i]:offsets[i + 1]] for i in range(levels)])
    base = max(0, levels - 4);n = levels - base
    thumb_header = bytearray(8 + 8 * n);struct.pack_into('<Ii', thumb_header, 0, n, 4)
    entry['thumbnailMip'] = base
    entry['thumbnailBase64'] = base64.b64encode(repack.rebuild_table(thumb_header, 8, linear_mips[base:])).decode()
    return data, entry


class Containers(unittest.TestCase):
    def test_supported_sizes_and_original_layout(self):
        for w, h in ((1, 1), (3, 7), (5, 5), (6, 6), (7, 11), (17, 13), (65, 33), (257, 129)):
            with self.subTest(dimensions=(w, h)):
                data, entry = owned_texture(w, h)
                tab, first, d, offsets = repack.full_texture(data, entry)
                self.assertEqual(tab, 36)
                self.assertEqual(first, 36 + 8 * entry['levels'])
                self.assertEqual(offsets[-1], len(data) - first)
                for level in range(d.levels):
                    mw, mh = repack.dimensions(w, h, level)
                    self.assertEqual(repack.upload_geometry(d, level)['linear_upload_content_bytes'],
                                     repack.block_bytes(mw, mh, 4))

    def test_unsupported_and_header_disagreement(self):
        data, entry = owned_texture(17, 13)
        for key, value in [('type', 8), ('depth', 2), ('layoutType', 1), ('format', 63)]:
            other = dict(entry);other[key] = value
            with self.subTest(key=key), self.assertRaises(repack.Unsupported):
                repack.desc(other, 0x44)
        other = dict(entry);other['width'] += 1
        with self.assertRaises(ValueError):
            repack.full_texture(data, other)

    def test_mip_table_invalid_boundaries(self):
        data, entry = owned_texture(17, 13)
        for field, value in [(32, 0xffffffff), (36, 0xffffffff), (40, 0xffffffff), (36, 1)]:
            broken = bytearray(data);struct.pack_into('<I', broken, field, value)
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                repack.full_texture(bytes(broken), entry)
        with self.assertRaises(ValueError):
            repack.full_texture(data + bytes(16), entry)

    @unittest.skipUnless(os.environ.get('ASTCENC_TEST'), 'set ASTCENC_TEST to the real astcenc binary')
    def test_actual_codec_NPOT_mip_thumbnail_and_retile(self):
        encoder = repack.Encoder(os.environ['ASTCENC_TEST'], 'fast', 2)
        with tempfile.TemporaryDirectory(prefix='astc6-owned-') as tmp:
            for number, (w, h) in enumerate(((1, 1), (3, 7), (5, 5), (6, 6), (7, 11), (17, 13), (65, 33), (257, 129))):
                with self.subTest(dimensions=(w, h)):
                    data, entry = owned_texture(w, h, number % 2 == 1)
                    output, changed, report = repack.convert(data, entry, encoder, Path(tmp) / str(number))
                    self.assertEqual(changed['format'], repack.GAME_FORMAT[entry['format']])
                    self.assertEqual(report['dimensions'], [w, h])
                    self.assertTrue(all(x['retile_block_and_pixel_roundtrip'] for x in report['mips']))
                    self.assertTrue(report['thumbnail']['source_matches_full_original_linear_mips'])
                    for key in entry.keys() - {'format', 'thumbnailBase64'}:
                        self.assertEqual(changed[key], entry[key])
                    spans = repack.mip_table(output, entry['levels'], 36)
                    self.assertEqual(spans[-1][0] + spans[-1][1], len(output))
            # Normal4x4 is an explicit mixed-recipe alternative, not a game patch.
            data, entry = owned_texture(17, 13)
            entry['filename'] = 'own_nrm.texture'
            output, changed, report = repack.convert(data, entry, encoder, Path(tmp) / 'normal4', 4)
            self.assertEqual(changed['format'], 95)
            self.assertEqual(report['ASTC_block_size'], '4x4')
            self.assertEqual(len(output), len(data))


if __name__ == '__main__':
    unittest.main()
