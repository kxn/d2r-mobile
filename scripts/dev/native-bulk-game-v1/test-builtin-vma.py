#!/usr/bin/env python3
"""Host-only guard checks using an existing builtin VMA metadata capture.
No device commands are allowed; the historical Mali file is parser evidence,
not Q735 acceptance or an instruction to resume that device.
"""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

D = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('builtin_vma_runner', D / 'run-game.py')
g = importlib.util.module_from_spec(spec); spec.loader.exec_module(g)
E = Path('/home/dl/d2r-mobile/.local/logs/mali-vma-fixed065-16MiB-v1')


class BuiltinVMA(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before = (E / 'before-vma-request-native.log').read_text()
        cls.after = (E / 'native.log').read_text()
        cls.fresh = json.loads((E / 'fresh-save.json').read_text())
        cls.data = (E / 'vulkan-vma.json').read_bytes()
        cls.own = str(cls.fresh['pid']); cls.serial = cls.fresh['serial']

    def validate(self, before=None, after=None, fresh=None, data=None):
        return g.builtin_vma_validate(self.before if before is None else before,
            self.after if after is None else after, self.fresh if fresh is None else fresh,
            self.own, self.serial, self.data if data is None else data)

    def test_actual_existing_metadata_is_accepted_without_rewriting(self):
        self.assertIn('complete=1', self.validate())

    def test_policy_absent_unchanged_and_only_strict_true_AA(self):
        self.assertIsNone(g.builtin_vma_policy({}))
        good = dict(builtin_vma_on_suspend=True, official_cpu_aa={'callgraph': 'fp'}, performance_tools={'pinned': True})
        g.builtin_vma_policy(good)
        for value in (False, 1, 0, None, 'true', {}):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                g.builtin_vma_policy(dict(good, builtin_vma_on_suspend=value))
        for field in ('official_cpu_aa', 'performance_tools'):
            p = good.copy(); del p[field]
            with self.subTest(field=field), self.assertRaises(RuntimeError):g.builtin_vma_policy(p)
        for field in ('startup_allocation_capture', 'readonly_memory_snapshot', 'counter_snapshot',
                      'sustained_play', 'official_gpu_tools', 'official_l1i_record'):
            with self.subTest(field=field), self.assertRaises(RuntimeError):g.builtin_vma_policy(dict(good, **{field: {}}))

    def test_stale_complete_before_request_does_not_count(self):
        complete = self.validate()
        with self.assertRaises(RuntimeError):self.validate(after=self.after.replace(complete, ''))
        with self.assertRaises(RuntimeError):self.validate(before=self.after, after=self.after)
        with self.assertRaises(RuntimeError):self.validate(after=self.before + complete + '\n' + self.fresh['vma_request_confirmation'])

    def test_wrong_PID_rejected_request_and_failed_save_rejected(self):
        for key, value in [('pid', self.fresh['pid'] + 1), ('serial', 'wrong'),
                           ('vma_dump_requested', False), ('save_commits', 1),
                           ('fresh_original_lifecycle_save', False), ('vma_request_confirmation', 'not this request')]:
            fresh = dict(self.fresh, **{key: value})
            with self.subTest(key=key), self.assertRaises(RuntimeError):self.validate(fresh=fresh)
        with self.assertRaises(RuntimeError):self.validate(after=self.after.replace('explicit request accepted=1', 'explicit request accepted=0'))
        with self.assertRaises(RuntimeError):self.validate(after=self.after + '\n' + self.fresh['vma_request_confirmation'])
        with self.assertRaises(RuntimeError):self.validate(after='reset log')

    def test_failed_multiple_wrong_path_completion_and_bad_JSON_rejected(self):
        complete = self.validate()
        for after in (self.after + '\n[vulkan VMA dump] failed: no allocator',
                      self.after + '\n' + complete,
                      self.after.replace('complete=1 errno=0', 'complete=0 errno=5'),
                      self.after.replace('temp/vulkan-vma.json', 'temp/old-vma.json')):
            with self.subTest(after=after[-150:]), self.assertRaises(RuntimeError):self.validate(after=after)
        for data in (b'{', b'[]', b'{}', b'{"General":{"API":"not Vulkan"}}'):
            with self.subTest(data=data), self.assertRaises((RuntimeError, ValueError)):self.validate(data=data)

    def test_collect_retains_exact_bytes_and_hash(self):
        test = self
        class Device:
            command = ['DENIED-ADB', '-s', 'host-only']
            reads = 0
            def adb(self, *args):
                test.assertEqual(args, ('shell', 'pidof ' + g.PKG + ' || true')); return test.own
            def app(self, *args):
                test.assertEqual(args, ('cat', 'files/runtime/native.log'))
                self.reads += 1
                return test.after[:test.after.index(test.validate())] if self.reads == 1 else test.after
            def digest(self, path):
                test.assertEqual(path, 'files/runtime/temp/vulkan-vma.json'); return g.hashlib.sha256(test.data).hexdigest()
        dev = Device()
        def read(command, **kwargs):
            if command[3] == 'shell':return dev.adb(*command[3:])
            test.assertEqual(command[3:], ['exec-out', 'run-as', g.PKG, 'cat', 'files/runtime/native.log'])
            return dev.app('cat', 'files/runtime/native.log')
        def pull(command, **kwargs):
            test.assertEqual(command, Device.command + ['exec-out', 'run-as', g.PKG, 'cat', 'files/runtime/temp/vulkan-vma.json'])
            kwargs['stdout'].write(test.data)
        with tempfile.TemporaryDirectory() as tmp, patch.object(g.subprocess, 'run', side_effect=pull), patch.object(g.time, 'sleep', return_value=None) as wait, patch.object(g.subprocess, 'check_output', side_effect=read):
            out = Path(tmp)
            row = g.builtin_vma_collect(dev, out, self.before, self.fresh, self.own, self.serial)
            wait.assert_called_once_with(.2)
            self.assertEqual((out / 'vulkan-vma.json').read_bytes(), self.data)
            self.assertEqual(row['sha256'], g.sha(E / 'vulkan-vma.json'))
        with tempfile.TemporaryDirectory() as tmp, patch.object(g.subprocess, 'run', side_effect=AssertionError('stale log must not read file')), patch.object(g.subprocess, 'check_output', side_effect=read):
            with self.assertRaises(RuntimeError):g.builtin_vma_collect(dev, Path(tmp), self.after, self.fresh, self.own, self.serial)


    def test_existing_request_wait_rechecks_PID_prefix_and_times_out(self):
        test = self
        pending = self.after[:self.after.index(self.validate())]
        class Device:
            command = ['DENIED-ADB', '-s', 'host-only']
            def adb(self, *args):return test.own
            def app(self, *args):return pending
        dev = Device()
        def read(command, **kwargs):
            if command[3] == 'shell':return dev.adb(*command[3:])
            return dev.app('cat', 'files/runtime/native.log')
        with tempfile.TemporaryDirectory() as tmp, patch.object(g.subprocess, 'check_output', side_effect=read), patch.object(g.time, 'monotonic', side_effect=[0, 0, 0, 21]), patch.object(g.time, 'sleep', side_effect=AssertionError('timeout must stop waiting')):
            with self.assertRaisesRegex(RuntimeError, 'completion timeout'):
                g.builtin_vma_collect(Device(), Path(tmp), self.before, self.fresh, self.own, self.serial)
        class Replacement(Device):
            def adb(self, *args):return 'replacement'
        dev = Replacement()
        with tempfile.TemporaryDirectory() as tmp, patch.object(g.subprocess, 'check_output', side_effect=read):
            with self.assertRaisesRegex(RuntimeError, 'PID changed'):
                g.builtin_vma_collect(Replacement(), Path(tmp), self.before, self.fresh, self.own, self.serial)
        class Reset(Device):
            logs = iter([pending, self.before])
            def app(self, *args):return next(self.logs)
        dev = Reset()
        with tempfile.TemporaryDirectory() as tmp, patch.object(g.subprocess, 'check_output', side_effect=read), patch.object(g.time, 'sleep', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'log reset'):
                g.builtin_vma_collect(Reset(), Path(tmp), self.before, self.fresh, self.own, self.serial)


if __name__ == '__main__':unittest.main(verbosity=2)
