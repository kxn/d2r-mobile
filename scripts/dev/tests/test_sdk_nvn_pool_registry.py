"""Offline malformed-list/guard tests; no device or target function calls."""
from pathlib import Path
import copy
import importlib.util
import unittest

p = Path(__file__).resolve().parents[1] / 'sdk_nvn_pool_registry.py'
s = importlib.util.spec_from_file_location('registry_test_module', p)
m = importlib.util.module_from_spec(s)
s.loader.exec_module(m)


def fixture():
    b = dict(metadata=0x8000, next=0, begin=0, end=4096, generation=1,
             buffer=dict(handle=0x33, memory=0x44, size=4096, map=0x100000, shared_import=1))
    p = dict(metadata=0x7000, next=0, identity=3, guest=0x100000, size=8192,
             shared=1, shared_closing=0, shared_lazy_eligible=1, backing_generation=1,
             backings=[b], shared_covers=[], shared_import_nodes=1,
             shared_cover_nodes=0, shared_import_bytes=4096, native_allocations=0)
    return [p]


def valid(p):
    return m.validate_registry(p, 0x7000, 7, 4, 0x7000, 7, 4)


class RegistryTests(unittest.TestCase):
    def test_registered_list_and_unbacked_hole(self):
        r = valid(fixture())
        self.assertEqual(r['pool_count'], 1)
        self.assertEqual(r['logical_pool_bytes'], 8192)
        self.assertEqual(r['buffer_import_interval_bytes'], 4096)

    def test_zero_initializer_accepted(self):
        m.normal_resources_unlocked(bytes(40))

    def test_held_normal_mutex_not_followed(self):
        for state in (1, 2):
            with self.assertRaisesRegex(ValueError, 'held or not a qualified'):
                m.normal_resources_unlocked(bytes([state]) + bytes(39))

    def test_other_type_unknown_or_short_mutex_rejected(self):
        for raw in (bytes(39), bytes(2) + b'\x01' + bytes(37)):
            with self.assertRaises(ValueError):
                m.normal_resources_unlocked(raw)

    def test_identity_midpublication_zero_rejected(self):
        p = fixture(); p[0]['identity'] = 0
        with self.assertRaisesRegex(ValueError, 'unpublished'):
            valid(p)

    def test_epoch_or_head_change_rejected(self):
        for after in ((0x7001, 7, 4), (0x7000, 8, 4), (0x7000, 7, 5)):
            with self.assertRaisesRegex(ValueError, 'changed'):
                m.validate_registry(fixture(), 0x7000, 7, 4, *after)

    def test_next_link_cycle_rejected(self):
        p = fixture(); p[0]['next'] = p[0]['metadata']
        with self.assertRaisesRegex(ValueError, 'next/list mismatch'):
            valid(p)

    def test_logical_pool_overlap_rejected(self):
        p = fixture(); q = copy.deepcopy(p[0]); q.update(metadata=0x7100, identity=4, next=0)
        q['backings'] = []; q['shared_import_nodes'] = 0; q['shared_import_bytes'] = 0
        p[0]['next'] = q['metadata']; p.append(q)
        with self.assertRaisesRegex(ValueError, 'logical ranges overlap'):
            valid(p)

    def test_buffer_range_overflow_rejected(self):
        p = fixture(); p[0]['backings'][0]['end'] = 8193
        with self.assertRaisesRegex(ValueError, 'outside Pool'):
            valid(p)

    def test_buffer_size_mismatch_rejected(self):
        p = fixture(); p[0]['backings'][0]['buffer']['size'] = 4095
        with self.assertRaisesRegex(ValueError, 'size/origin mismatch'):
            valid(p)

    def test_buffer_generation_midpublication_rejected(self):
        p = fixture(); p[0]['backings'][0]['generation'] = 2
        with self.assertRaisesRegex(ValueError, 'generation not published'):
            valid(p)

    def test_buffer_cycle_or_shared_node_duplicate_rejected(self):
        p = fixture(); p[0]['shared_covers'] = [copy.deepcopy(p[0]['backings'][0])]
        with self.assertRaisesRegex(ValueError, 'cycle/duplicate'):
            valid(p)

    def test_overlapping_cover_is_legal_not_canonical_overlap(self):
        p = fixture(); b = copy.deepcopy(p[0]['backings'][0]); b.update(metadata=0x8100, generation=2)
        p[0].update(shared_covers=[b], backing_generation=2, shared_cover_nodes=1,
                    shared_import_nodes=2, shared_import_bytes=8192)
        self.assertEqual(valid(p)['buffer_node_count'], 2)

    def test_lazy_counter_midpublication_rejected(self):
        p = fixture(); p[0]['shared_import_nodes'] = 2
        with self.assertRaisesRegex(ValueError, 'publication middle-state'):
            valid(p)

    def test_shared_map_origin_not_guessed(self):
        p = fixture(); p[0]['backings'][0]['buffer']['map'] = 0x200000
        with self.assertRaisesRegex(ValueError, 'CPU origin mismatch'):
            valid(p)

    def test_empty_current_registry_is_not_missing_data(self):
        self.assertEqual(m.validate_registry([], 0, 11, 2, 0, 11, 2)['pool_count'], 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
