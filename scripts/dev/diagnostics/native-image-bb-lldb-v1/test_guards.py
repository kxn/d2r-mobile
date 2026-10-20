"""Offline fixture metadata only; never launches adb or LLDB/inferior."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('native_hook', Path(__file__).with_name('run-debugger.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def values(**kw):
    return {k: {'unsigned': v} for k, v in kw.items()}


def proof():
    image = {'image': values(**{'native_pool': 40, 'handle': 50, 'memory': 60,
        'tex.mem': 0x40001000, 'tex.L.size': 135168, 'tex.serial': 123, 'native_entry': 70}),
        'native_entry': values(**{'id': 1, 'recipe.image_size': 135168, 'recipe.image_alignment': 4096})}
    return {'events': [
        {'kind': 'bind_entry', 'PID': 12000, 'arguments': {'address': 0x40001000, 'bytes': 135168, 'VkImage': 50},
            'requirements': values(size=135168, alignment=4096, memoryTypeBits=8)},
        {'kind': 'use_validated', 'PID': 12000, 'arguments': {'address': 0x40001000, 'bytes': 135168,
            'token': 40, 'owner_register': 80},
            'allocation': values(**{'owner': 80, 'identity': 90, 'address': 0x40001000,
                'bytes': 135168, 'released': 0, 'begin': 4096, 'end': 4096 + 135168,
                'storage.memory': 60, 'storage.handle': 0, 'storage.map': 0x40001000,
                'storage.size': 135168, 'storage.shared_import': 1, 'storage.shared_memory_type': 3}),
            'owner': values(guest=0x40000000, size=1024 * 1024, identity=90, shared=1,
                shared_closing=0, native_image_references=1), 'caller_images': [image]}], 'errors': []}


class Guards(unittest.TestCase):
    def test_owned_PID(self):
        self.assertTrue(m.owned_pid('12000', '12000'))
        for s in ['', '12000 12001', '12001', '12000\n']:
            self.assertFalse(m.owned_pid(s, '12000'))

    def test_maps(self):
        text = '100000-101000 r--p 00000000 01:01 1 /x/libdmhost_android.so\n' + \
               '102000-104000 r-xp 00002000 01:01 1 /x/libdmhost_android.so\n'
        self.assertEqual(m.module_slide(text), (0x100000, '/x/libdmhost_android.so'))
        for bad in ['', text + text.replace('/x/', '/y/')]:
            with self.assertRaises(RuntimeError):
                m.module_slide(bad)

    def test_complete_metadata(self):
        p = m.validate_stops(proof())
        self.assertEqual(p['status'], 'actual_native_bind_and_validated_use_proven')
        self.assertEqual(p['native_serial'], 123)

    def test_missing_stops_not_runtime_failure(self):
        for events in [[], proof()['events'][:1], proof()['events'][1:]]:
            p = m.validate_stops({'events': events, 'errors': []})
            self.assertIn('inconclusive', p['status'])
            self.assertFalse(p['runtime_failure_proven'])

    def test_no_caller_not_adoption(self):
        p = proof()
        p['events'][1]['caller_images'] = []
        self.assertIn('inconclusive', m.validate_stops(p)['status'])

    def test_different_identity_or_short_tail_rejected(self):
        for field, value in [('identity', 91), ('released', 1), ('end', 4096 + 135167),
                             ('storage.memory', 0), ('storage.handle', 1), ('storage.shared_memory_type', 4)]:
            p = proof()
            p['events'][1]['allocation'][field]['unsigned'] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                m.validate_stops(p)

    def test_PID_and_payload_rejected(self):
        for field, value in [('PID', 12001), ('arguments', {'address': 0x40001000, 'bytes': 131072, 'VkImage': 50})]:
            p = proof()
            p['events'][0][field] = value
            with self.assertRaises(RuntimeError):
                m.validate_stops(p)


if __name__ == '__main__':
    unittest.main()
