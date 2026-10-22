"""Only optional frozen-source config checks; never invokes main/ADB."""
import copy
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

p = Path(__file__).resolve().parents[1] / 'sdk-free-resident-pool-v1/run-debugger.py'
s = importlib.util.spec_from_file_location('optional_sdk_pool_config', p)
m = importlib.util.module_from_spec(s)
s.loader.exec_module(m)


class ConfigTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.old = m.HERE
        m.HERE = Path(self.tmp.name)
        f = m.HERE / 'sdk_nvn_pool_registry.py'; f.write_text('# pinned fixture\n')
        sha = hashlib.sha256(f.read_bytes()).hexdigest()
        self.inputs = dict(files={'sdk_nvn_pool_registry.py': dict(sha256=sha)})
        self.c = dict(ELF_sha256='4a8420209e2ac6c71c161626472458ac498ee844bdce58677c8e2769b5ec4618',
                      ELF_BuildID='6e6f12c5c5f7a858704a84ee294b19d95aa3f894')
        self.c['nvn_pools'] = dict(enabled=True, mode='f75-bionic-normal-resources-zero-v1',
                                   ELF_sha256=self.c['ELF_sha256'], ELF_BuildID=self.c['ELF_BuildID'],
                                   qualify_unlocked=True, module=dict(path=str(f), sha256=sha))

    def tearDown(self):
        m.HERE = self.old
        self.tmp.cleanup()

    def test_absent_optional_retains_original_contract(self):
        m.validate_optional_nvn({}, {})

    def test_matching_frozen_optional_pin(self):
        m.validate_optional_nvn(self.c, self.inputs)

    def test_extra_optional_field_rejected(self):
        self.c['nvn_pools']['guess'] = True
        with self.assertRaisesRegex(RuntimeError, 'config fields'):
            m.validate_optional_nvn(self.c, self.inputs)

    def test_bad_companion_hash_rejected(self):
        self.c['nvn_pools']['module']['sha256'] = '0' * 64
        with self.assertRaisesRegex(RuntimeError, 'frozen companion'):
            m.validate_optional_nvn(self.c, self.inputs)

    def test_unfrozen_live_path_rejected_even_same_content(self):
        other = m.HERE / 'other.py'; other.write_text('# pinned fixture\n')
        self.c['nvn_pools']['module']['path'] = str(other)
        with self.assertRaisesRegex(RuntimeError, 'frozen companion'):
            m.validate_optional_nvn(self.c, self.inputs)

    def test_wrong_ELF_rejected(self):
        self.c['nvn_pools']['ELF_sha256'] = '0' * 64
        with self.assertRaisesRegex(RuntimeError, 'ELF identity'):
            m.validate_optional_nvn(self.c, self.inputs)

    def test_exact_Q735_libc_pin_accepted(self):
        self.c['nvn_pools'].update(mode=m.Q735_MUTEX_MODE, libc_sha256=m.Q735_LIBC_SHA)
        m.validate_optional_nvn(self.c, self.inputs)

    def test_Q735_missing_or_other_libc_pin_rejected(self):
        self.c['nvn_pools']['mode'] = m.Q735_MUTEX_MODE
        with self.assertRaisesRegex(RuntimeError, 'libc ABI pin'): m.validate_optional_nvn(self.c, self.inputs)
        self.c['nvn_pools']['libc_sha256'] = '0' * 64
        with self.assertRaisesRegex(RuntimeError, 'libc ABI pin'): m.validate_optional_nvn(self.c, self.inputs)

    def test_old_mode_does_not_implicitly_accept_vendor_libc_pin(self):
        self.c['nvn_pools']['libc_sha256'] = m.Q735_LIBC_SHA
        with self.assertRaisesRegex(RuntimeError, 'old mutex mode'): m.validate_optional_nvn(self.c, self.inputs)

    def test_wrong_mode_rejected(self):
        self.c['nvn_pools']['mode'] = 'unreviewed'
        with self.assertRaisesRegex(RuntimeError, 'mode/enable'):
            m.validate_optional_nvn(self.c, self.inputs)

    def test_implicit_string_bool_rejected(self):
        self.c['nvn_pools']['qualify_unlocked'] = 'true'
        with self.assertRaisesRegex(RuntimeError, 'not bool'):
            m.validate_optional_nvn(self.c, self.inputs)

    def test_disabled_section_is_not_requested_capture(self):
        self.c['nvn_pools']['enabled'] = False
        with self.assertRaisesRegex(RuntimeError, 'mode/enable'):
            m.validate_optional_nvn(self.c, self.inputs)


if __name__ == '__main__':
    unittest.main(verbosity=2)
