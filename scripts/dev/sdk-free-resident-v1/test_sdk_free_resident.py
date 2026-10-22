"""Offline exact-layout decoder/offset fixtures. No LLDB target/device required."""
import importlib.util
from pathlib import Path
import struct
import unittest

p = Path(__file__).with_name('sdk_free_resident.py')
spec = importlib.util.spec_from_file_location('sdk_free_resident_under_test', p)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def fixture():
    c = 0x100000
    nodes = [c + 0x100 + i * 0x30 for i in range(3)]
    table = bytearray(8 * 8)
    for i, node in [(0, nodes[0]), (1, nodes[1]), (4, nodes[2])]:
        struct.pack_into('<Q', table, i * 8, node)
    payload = {}
    for node, begin, pages, state in [(nodes[0], c, 1, 3), (nodes[1], c + 4096, 3, 1), (nodes[2], c + 4 * 4096, 4, 2)]:
        b = bytearray(0x30)
        b[0x13] = state
        struct.pack_into('<QQ', b, 0x18, begin, pages)
        payload[node] = b
    return c, nodes, table, payload


class DecoderTests(unittest.TestCase):
    def decode(self, c, nodes, table, payload):
        return m.decode_spans(c, 8, table, lambda a, n: bytes(payload[a][:n]))

    def test_valid_full_partition(self):
        c, nodes, table, payload = fixture()
        spans = self.decode(c, nodes, table, payload)
        self.assertEqual([s['state'] for s in spans], [3, 1, 2])
        self.assertEqual(sum(s['pages'] for s in spans), 8)
        self.assertEqual(spans[-1]['end'], c + 8 * 4096)

    def test_boundary_table_short(self):
        c, nodes, table, payload = fixture()
        with self.assertRaisesRegex(ValueError, 'table length'):
            self.decode(c, nodes, table[:-8], payload)

    def test_cycle(self):
        c, nodes, table, payload = fixture()
        struct.pack_into('<Q', table, 8, nodes[0])
        with self.assertRaisesRegex(ValueError, 'cycle'):
            self.decode(c, nodes, table, payload)

    def test_null_before_end(self):
        c, nodes, table, payload = fixture()
        struct.pack_into('<Q', table, 8, 0)
        with self.assertRaisesRegex(ValueError, 'null span'):
            self.decode(c, nodes, table, payload)

    def test_unknown_state(self):
        c, nodes, table, payload = fixture()
        payload[nodes[1]][0x13] = 0
        with self.assertRaisesRegex(ValueError, 'unknown span state'):
            self.decode(c, nodes, table, payload)

    def test_partition_hole(self):
        c, nodes, table, payload = fixture()
        struct.pack_into('<Q', payload[nodes[1]], 0x18, c + 8192)
        with self.assertRaisesRegex(ValueError, 'partition hole'):
            self.decode(c, nodes, table, payload)

    def test_partition_overlap(self):
        c, nodes, table, payload = fixture()
        struct.pack_into('<Q', payload[nodes[1]], 0x18, c)
        with self.assertRaisesRegex(ValueError, 'overlap'):
            self.decode(c, nodes, table, payload)

    def test_nonprogress_zero_pages(self):
        c, nodes, table, payload = fixture()
        struct.pack_into('<Q', payload[nodes[1]], 0x20, 0)
        with self.assertRaisesRegex(ValueError, 'nonprogress'):
            self.decode(c, nodes, table, payload)

    def test_end_overflow(self):
        c, nodes, table, payload = fixture()
        struct.pack_into('<Q', payload[nodes[2]], 0x20, 5)
        with self.assertRaisesRegex(ValueError, 'out of bounds'):
            self.decode(c, nodes, table, payload)

    def test_metadata_address_outside(self):
        c, nodes, table, payload = fixture()
        struct.pack_into('<Q', table, 8, c + 8 * 4096)
        with self.assertRaisesRegex(ValueError, 'pointer out of heap'):
            self.decode(c, nodes, table, payload)

    def test_short_metadata(self):
        c, nodes, table, payload = fixture()
        payload[nodes[1]] = payload[nodes[1]][:-1]
        with self.assertRaisesRegex(ValueError, 'short span'):
            self.decode(c, nodes, table, payload)

    def test_checks_deadline_without_zero_fallback(self):
        c, nodes, table, payload = fixture()
        def expired():
            raise TimeoutError('control budget')
        with self.assertRaises(TimeoutError):
            m.decode_spans(c, 8, table, lambda a, n: bytes(payload[a]), expired)

    def test_intersection_counts_alias_union_once(self):
        c, nodes, table, payload = fixture()
        spans = self.decode(c, nodes, table, payload)
        primary, alias = bytearray(64), bytearray(64)
        for j in [5, 7]:
            struct.pack_into('<Q', primary, j * 8, 1 << 63)
        for j in [6, 7]:
            struct.pack_into('<Q', alias, j * 8, 1 << 63)
        struct.pack_into('<Q', primary, 4 * 8, 1 << 62)
        r = m.intersect_free(spans, c, 32768, 4096, primary, alias)
        self.assertEqual(r['free_whole_host_page_bytes'], 16384)
        self.assertEqual(r['free_union_present_bytes'], 12288)
        self.assertEqual(r['free_union_neither_present_bytes'], 4096)
        self.assertEqual(r['free_primary_only_present_bytes'], 4096)
        self.assertEqual(r['free_alias_only_present_bytes'], 4096)
        self.assertEqual(r['free_both_present_bytes'], 4096)
        self.assertEqual(r['free_primary_swap_flag_bytes'], 4096)
        # Allocated span present bits are deliberately ignored.
        struct.pack_into('<Q', primary, 2 * 8, 1 << 63)
        self.assertEqual(m.intersect_free(spans, c, 32768, 4096, primary, alias), r)

    def test_host16k_only_whole_pages_not_small_free_slots(self):
        c = 0x100000
        spans = [dict(state=2, begin=c + 4096, end=c + 4096 * 7)]
        r = m.intersect_free(spans, c, 32768, 16384, bytes(16), bytes(16))
        self.assertEqual(r['free_whole_host_page_bytes'], 0)
        self.assertEqual(r['free_host_alignment_excluded_bytes'], 24576)

    def test_short_pagemap_is_unknown_not_zero(self):
        c, nodes, table, payload = fixture()
        with self.assertRaisesRegex(ValueError, 'payload length'):
            m.intersect_free(self.decode(c, nodes, table, payload), c, 32768, 4096, bytes(63), bytes(64))

    def test_general_pagemap_geometry_not_2MiB_aligned(self):
        g = m.pagemap_geometry(0x40100000, 32768, 4096)
        self.assertEqual(g['prefix'], 2048)
        self.assertEqual(g['raw_bytes'], 4096)
        self.assertEqual(g['offset'], g['skip'] * 4096 + g['prefix'])

    def test_unaligned_pagemap_view_refused(self):
        with self.assertRaisesRegex(ValueError, 'not host-page aligned'):
            m.pagemap_geometry(0x100001, 32768, 4096)

    def test_same_backing_two_views_and_split_ro_pages(self):
        rows = m.parse_maps('100000-101000 rw-s 00000000 00:01 7 /memfd:x\n'
                            '101000-102000 r--s 00001000 00:01 7 /memfd:x\n'
                            '200000-202000 rw-s 00000000 00:01 7 /memfd:x\n')
        p, a = m.view_identity(rows, 0x100000, 8192), m.view_identity(rows, 0x200000, 8192)
        self.assertEqual((p['dev'], p['inode']), (a['dev'], a['inode']))

    def test_alias_backing_offset_mismatch(self):
        rows = m.parse_maps('200000-202000 rw-s 00001000 00:01 7 /memfd:x\n')
        with self.assertRaisesRegex(ValueError, 'offset mismatch'):
            m.view_identity(rows, 0x200000, 8192)

    def test_view_hole_refused(self):
        rows = m.parse_maps('200000-201000 rw-s 00000000 00:01 7 /memfd:x\n'
                            '202000-203000 rw-s 00002000 00:01 7 /memfd:x\n')
        with self.assertRaisesRegex(ValueError, 'mapping hole'):
            m.view_identity(rows, 0x200000, 12288)

    def test_same_name_different_inode_refused(self):
        rows = m.parse_maps('200000-201000 rw-s 00000000 00:01 7 /memfd:x\n'
                            '201000-202000 rw-s 00001000 00:01 8 /memfd:x\n')
        with self.assertRaisesRegex(ValueError, 'identity changed'):
            m.view_identity(rows, 0x200000, 8192)

    def test_private_mapping_not_shared_alias(self):
        rows = m.parse_maps('200000-202000 rw-p 00000000 00:01 7 /memfd:x\n')
        with self.assertRaisesRegex(ValueError, 'not MAP_SHARED'):
            m.view_identity(rows, 0x200000, 8192)


if __name__ == '__main__':
    unittest.main(verbosity=2)
