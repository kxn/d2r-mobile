#!/usr/bin/env python3
"""Failure-oriented checks for the capture/comparison boundary."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'dm/verify'))
sys.path.insert(0, str(ROOT / 'dm/verify/oracle'))
from selfcmp import compare, load_ticks
from record import StrictReader
from control import save_frame
from swgdb import Gdb


class OracleTools(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rows = [{'tick': tick, 'n': [0] * 5, 'h': ['cbf29ce484222325'] * 6, 'bad': 0} for tick in range(20)]

    def load(self, rows):
        path = self.root / 'ticks.jsonl'
        path.write_text(''.join(json.dumps(row) + '\n' for row in rows))
        return load_ticks(path)

    def test_reject_duplicate_gap_rewind_invalid_fields_and_corruption(self):
        variants = [self.rows[:5] + self.rows[6:], self.rows[:5] + self.rows[4:], self.rows[::-1]]
        for field, value in [('tick', True), ('tick', -1), ('bad', 1), ('bad', False),
                             ('n', [0] * 4), ('n', [0, 0, -1, 0, 0]), ('h', ['xyz'] * 6)]:
            rows = copy.deepcopy(self.rows)
            rows[7][field] = value
            variants.append(rows)
        for rows in variants:
            with self.subTest(rows=rows[7]):
                with self.assertRaises(ValueError):
                    self.load(rows)
        with self.assertRaises(ValueError):
            self.load(self.rows + [{'t': 20, 'u': {}}])

    def test_partial_overlap_requires_explicit_opt_in(self):
        whole = self.load(self.rows)
        partial = self.load(self.rows[5:])
        self.assertEqual(compare(whole, partial)[0], 2)
        code, result = compare(whole, partial, allow_partial=True)
        self.assertEqual(code, 0)
        self.assertTrue(result['partial'])
        self.assertEqual(result['common_ticks'], 15)

    def test_counts_are_compared_even_when_hashes_match(self):
        left = self.load(self.rows)
        self.rows[9]['n'][2] = 1
        code, result = compare(left, self.load(self.rows))
        self.assertEqual(code, 1)
        self.assertEqual(result['differences']['n[2]']['tick'], 9)

    def test_empty_and_short_capture_never_pass(self):
        for size in (0, 1, 9):
            rows = self.load(self.rows[:size])
            self.assertEqual(compare(rows, rows)[0], 3)

    def test_metadata_is_not_compared(self):
        left = self.load(self.rows)
        for row in self.rows:
            row.update(game='0xdeadbeef', dt=123, reads=999)
        self.assertEqual(compare(left, self.load(self.rows))[0], 0)

    def test_failed_and_short_reads_cannot_turn_into_zero_hashes(self):
        class Short:
            def read(self, address, size):
                return b'\0' * (size - 1)
        class Broken:
            def read(self, address, size):
                raise IOError('unmapped')
        for backend in (Short(), Broken()):
            reader = StrictReader(backend)
            self.assertIsNone(reader.rd(0, 4))
            with self.assertRaises(IOError):
                reader.rd(0x1000, 4)

    def test_capture_channel_order_and_orientation(self):
        from PIL import Image
        # Input BGRA pixels: red, green / blue, white. Flip both axes.
        raw = self.root / 'frame.rgba'
        raw.write_bytes(bytes([0, 0, 255, 255, 0, 255, 0, 255,
                              255, 0, 0, 255, 255, 255, 255, 255]))
        target = self.root / 'frame.png'
        save_frame(dict(path=str(raw), width=2, height=2, bgra=True, flipX=True, flipY=True), target)
        with Image.open(target) as image:
            self.assertEqual([image.getpixel((x, y)) for y in range(2) for x in range(2)],
                             [(255, 255, 255, 255), (0, 0, 255, 255), (0, 255, 0, 255), (255, 0, 0, 255)])
        self.assertFalse(raw.exists())

    def test_modules_do_not_include_heap_or_layout_headers(self):
        class Reply:
            def monitor(self, text):
                return 'Layout:\n  Heap: 0x100 - 0x200\n  Aslr: 0x300 - 0x400\nModules:\n  0x500 - 0x600 Game.nss\n'
        modules, _ = Gdb.modules(Reply())
        self.assertEqual(modules, {'Game.nss': (0x500, 0x600)})


if __name__ == '__main__':
    unittest.main()
