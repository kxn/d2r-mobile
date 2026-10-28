"""Small real-patch provenance checks; no compilation or full tree scan."""
from pathlib import Path
import hashlib
import importlib.util
import json
import shutil
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('critical_schedule_archive', HERE / 'build.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class ArchiveTests(unittest.TestCase):
    def test_actual_combined_and_stage_patches_reconstruct_exact_ledgers(self):
        a, ledger = m.verify()
        self.assertEqual(len(a['files']), 3)
        self.assertEqual(len(ledger), 18218)
        self.assertEqual(a['stages']['v3']['driver']['sha256'][:8], '82cbd84e')

    def changed_archive(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        p = Path(tmp.name) / 'archive'
        shutil.copytree(HERE, p, ignore=shutil.ignore_patterns('__pycache__'))
        old = m.D
        m.D = p
        self.addCleanup(setattr, m, 'D', old)
        return p

    def test_changed_combined_patch_rejected(self):
        p = self.changed_archive()
        with (p / 'memory-critical-schedule.patch').open('a') as f:
            f.write('unreviewed change\n')
        with self.assertRaises(AssertionError):
            m.verify()

    def test_wrong_expected_source_rejected(self):
        p = self.changed_archive()
        a = m.read(p / 'archive.json')
        first = next(iter(a['files']))
        a['files'][first]['before'] = '0' * 64
        m.write(p / 'archive.json', a)
        with self.assertRaises(AssertionError):
            m.verify()

    def test_forged_cache_key_rejected_even_if_file_hash_is_updated(self):
        p = self.changed_archive()
        name = 'stages/v3/cache-policy-key.json'
        policy = m.read(p / name)
        policy['hash_input']['compile_policy'] += '-wrong'
        m.write(p / name, policy)
        a = m.read(p / 'archive.json')
        a['archived_files'][name] = m.sha(p / name)
        m.write(p / 'archive.json', a)
        with self.assertRaises(AssertionError):
            m.verify()


if __name__ == '__main__':
    unittest.main()
