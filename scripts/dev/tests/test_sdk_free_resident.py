"""Offline exact-layout decoder/offset fixtures. No LLDB target/device required."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import sys
import time

p = Path(__file__).resolve().parents[1] / 'sdk_free_resident.py'
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


class NaturalQualificationTests(unittest.TestCase):
    def tearDown(self):
        m._ACTIVE = None

    def invoke(self, raw, gated=True, error=None):
        class BP:
            disabled = False
            def GetBreakpoint(self): return self
            def GetID(self): return 5
            def SetEnabled(self, value): self.disabled = not value
        class PR:
            state, stop_id, calls = 5, 2, 0
            def GetStopID(self): return self.stop_id
            def GetState(self): return self.state
            def GetNumThreads(self): return len(self.threads)
            def GetThreadAtIndex(self, i): return self.threads[i]
            def Continue(self):
                self.calls += 1; self.state = 6
                return SimpleNamespace(Success=lambda: True)
        proc = PR()
        class TH:
            reason, data = 3, [5, 5]
            def GetProcess(self): return proc
            def GetThreadID(self): return 17
            def GetStopReason(self): return self.reason
            def GetStopReasonDataCount(self): return len(self.data)
            def GetStopReasonDataAtIndex(self, i): return self.data[i]
            def GetFrameAtIndex(self, i): return frame
        class FR:
            registers = {'x0': 0x1000, 'x30': 0x2000 + m.SDK_LR, 'pc': 0x2000 + m.SDK_ENTRY}
            def FindRegister(self, name): return self.registers[name]
            def GetThread(self): return thread
        thread, frame = TH(), FR()
        proc.threads = [thread]
        self.process, self.thread, self.frame = proc, thread, frame
        m._ACTIVE = dict(central=0x1000, sdk=0x2000, matched=False)
        if gated:
            m._ACTIVE.update(nvn_entry_gate=dict(address=0x3000, bytes=40),
                             nvn_qualification=dict(natural_entries_examined=0, nonzero_mutex_entries_declined=0),
                             nvn_entry_predicate=lambda raw: m.require(raw == bytes(40), 'not zero'))
        b = BP()
        self.breakpoint = b
        def read(*args):
            if error: raise error
            return raw
        with patch.object(m, '_unsigned', lambda v: v), patch.object(m, '_read_small', read) as _:
            result = m._on_entry(FR(), b, {})
        return result, b.disabled, m._ACTIVE

    def test_nonzero_registry_mutex_declines_natural_entry(self):
        stop, disabled, active = self.invoke(b'\x01' + bytes(39))
        self.assertTrue(stop); self.assertFalse(disabled); self.assertFalse(active['matched'])
        self.assertEqual(active['qualification_declined']['stop_id'], 2)
        self.assertEqual(active['nvn_qualification']['nonzero_mutex_entries_declined'], 1)

    def resume(self, active, deadline=None):
        fake = SimpleNamespace(eStateStopped=5, eStopReasonInvalid=0, eStopReasonNone=1, eStopReasonBreakpoint=3)
        with patch.dict(sys.modules, {'lldb': fake}), patch.object(m, '_unsigned', lambda v: v):
            return m._resume_qualification_declined(self.process, self.breakpoint, active, 1,
                                                   deadline if deadline is not None else time.monotonic() + 60)

    def test_decline_resume_then_accept_has_no_auto_restart_race(self):
        stop, disabled, active = self.invoke(b'\x01' + bytes(39))
        self.assertTrue(stop); self.assertFalse(disabled)
        self.assertEqual(self.resume(active), 2)
        self.assertEqual(self.process.calls, 1)
        self.assertNotIn('qualification_declined', active)
        self.process.state, self.process.stop_id = 5, 3
        with patch.object(m, '_unsigned', lambda v: v), patch.object(m, '_read_small', lambda *a: bytes(40)):
            self.assertTrue(m._on_entry(self.frame, self.breakpoint, {}))
        self.assertTrue(active['matched'])
        self.assertEqual(active['nvn_qualification']['declined_stops_explicitly_resumed'], 1)

    def test_declined_wrong_breakpoint_never_continues(self):
        _, _, active = self.invoke(b'\x01' + bytes(39)); self.thread.data = [99, 5]
        with self.assertRaisesRegex(ValueError, 'exact original breakpoint'): self.resume(active)
        self.assertEqual(self.process.calls, 0)

    def test_declined_unrelated_signal_never_continues(self):
        _, _, active = self.invoke(b'\x01' + bytes(39)); self.thread.reason = 5
        with self.assertRaisesRegex(ValueError, 'exact original breakpoint'): self.resume(active)
        self.assertEqual(self.process.calls, 0)

    def test_declined_changed_register_identity_never_continues(self):
        _, _, active = self.invoke(b'\x01' + bytes(39))
        self.frame.registers = dict(self.frame.registers, pc=0)
        with self.assertRaisesRegex(ValueError, 'register identity changed'): self.resume(active)
        self.assertEqual(self.process.calls, 0)

    def test_declined_expired_original_deadline_never_continues(self):
        _, _, active = self.invoke(b'\x01' + bytes(39))
        with self.assertRaisesRegex(ValueError, 'budget expired'): self.resume(active, time.monotonic() - 1)
        self.assertEqual(self.process.calls, 0)

    def test_declined_stale_stop_never_continues(self):
        _, _, active = self.invoke(b'\x01' + bytes(39)); self.process.stop_id = 3
        with self.assertRaisesRegex(ValueError, 'stale'): self.resume(active)
        self.assertEqual(self.process.calls, 0)

    def test_declined_callback_error_never_continues(self):
        _, _, active = self.invoke(b'\x01' + bytes(39)); active['callback_error'] = 'read failed'
        with self.assertRaisesRegex(ValueError, 'clean qualification decline'): self.resume(active)
        self.assertEqual(self.process.calls, 0)

    def test_zero_registry_mutex_accepts_exactly_one_stop(self):
        stop, disabled, active = self.invoke(bytes(40))
        self.assertTrue(stop); self.assertTrue(disabled); self.assertTrue(active['matched'])
        self.assertEqual(active['nvn_qualification']['accepted_mutex_hex'], '00' * 40)

    def test_read_failure_is_not_an_unlocked_zero(self):
        stop, disabled, active = self.invoke(None, error=ValueError('denied read'))
        self.assertTrue(stop); self.assertTrue(disabled); self.assertFalse(active['matched'])
        self.assertIn('denied read', active['callback_error'])

    def test_absent_optional_gate_has_no_mutex_read(self):
        stop, disabled, active = self.invoke(None, gated=False, error=ValueError('must not read'))
        self.assertTrue(stop); self.assertTrue(disabled); self.assertTrue(active['matched'])
        self.assertNotIn('callback_error', active)

    def test_bad_optional_pin_is_explicit_unavailable(self):
        class S:
            config = dict(elf=dict(sha256='0' * 64, buildid='1' * 40))
        out = m._optional_nvn_pools(None, S(), {}, 0x1000, 0x2000)
        self.assertFalse(out['complete']); self.assertTrue(out['errors'])
        self.assertEqual(out['status'], 'SAME_SDK_STOP_NVN_POOL_REGISTRY_UNAVAILABLE')


if __name__ == '__main__':
    unittest.main(verbosity=2)
