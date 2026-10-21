#!/usr/bin/env python3
"""Host-only process/ownership failure controls; not sampling or Android proof."""
import ast
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

D = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('aa', D / 'official_cpu_aa.py')
aa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(aa)


class Flow(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            phase = {'stages': []}
            property_value = ['1']
            events, tools, removed, metadata = [], {}, [], []
            sample = {'frame_pacing': {'target_fps': 60},
                      'pipeline_prepare': {'pipelines': 186},
                      'uniform_specialization': {'variants': 33}}

            class Process:
                def __init__(self, argv, **kwargs):
                    self.argv, self.pid, self.returncode = argv, 9876, None
                    self.trace = '--serial' in argv
                    self.stdout = io.BytesIO()
                    if self.trace:
                        events.append('trace-start')
                        self.path = Path(argv[argv.index('--output') + 1])
                    else:
                        events.append('record-start')
                        assert 'exec 3>&1' not in argv[-1], 'No extra inherited fd across Android exec'
                        words = shlex.split(shlex.split(argv[-1])[-1])
                        self.remote = words[words.index('-o') + 1]
                        tools[self.remote] = self
                        self.words = words
                def wait(self, timeout=None):
                    if self.trace:
                        if fault == 'trace' and 'record-1' in str(self.path):
                            self.returncode = 1
                            return 1
                        self.path.write_bytes(b'fake-host-trace')
                        self.path.with_suffix('.json').write_text(json.dumps({
                            'pid': 12345, 'seconds_requested': 8, 'shader_chain': '9507875007c6',
                            'app_markers_requested': True, 'clean_atrace_print_requested': True,
                            'frame_timeline_requested': True, 'trace_sha256': 'fake-not-device-proof'}))
                    self.returncode = 0
                    return 0
                def poll(self):
                    return self.returncode
                def communicate(self, timeout=None):
                    self.returncode = 2 if fault == 'record' else 0
                    return b'official fixture console', None
                def kill(self):
                    self.returncode = -9
                def terminate(self):
                    self.returncode = -15

            def ad(*parts):
                if parts[:2] == ('shell', 'getprop'):
                    return property_value[0]
                if parts[:2] == ('shell', 'setprop'):
                    property_value[0] = parts[-1]
                    return ''
                if parts[0] == 'logcat':
                    return 'fake-public-API-only'
                raise AssertionError(parts)

            def app(*parts):
                if parts[:2] in (('mkdir', '-p'), ('rmdir', 'files/runtime/profiles')):
                    return ''
                if parts[:2] == ('kill', '-TERM'):
                    events.append('owned-tool-stop')
                    return ''
                if parts[0] == 'sh':
                    script = parts[2]
                    if 'test -d ' in script:
                        return ''
                    if 'test -e ' in script:
                        return ''
                    if 'cat /proc/' in script:
                        return '/system/bin/simpleperf -o ' + list(tools)[-1]
                    if 'then cat ' in script:
                        return '444'
                    if 'then sha256sum' in script:
                        return 'a' * 64 + ' ownedfile'
                    if 'rm "$1"' in script:
                        removed.append(parts[-2])
                        return ''
                raise AssertionError(parts)

            def ready(process, console):
                events.append('official-STARTED')
                if fault == 'ready':
                    raise RuntimeError('injected missing official ready')
                if fault == 'cancel':
                    raise KeyboardInterrupt('injected cooperative cancellation')
                return b'STARTED'

            def run(argv, **kwargs):
                assert argv[1:4] == ['-s', 'SERIAL', 'exec-out']
                raw = b'broken' if fault == 'payload' else b'PERFILE2' + b'x' * 2000
                return SimpleNamespace(stdout=raw)

            def save():
                metadata.append(copy.deepcopy(phase))

            with patch.object(aa.subprocess, 'Popen', Process), patch.object(aa.subprocess, 'run', run), \
                 patch.object(aa, 'started', ready):
                try:
                    aa.run(folder=root, base=['adb', '-s', 'SERIAL'], package='owned.app', pid='12345',
                           stack=49152, phase=phase, endpoint=lambda name: copy.deepcopy(sample),
                           app=app, ad=ad, exact=lambda: None, snapshot=lambda name: events.append(name),
                           save=save, env={}, scheduler=D / 'scheduler-fixture.py')
                    self.assertIsNone(fault)
                except (RuntimeError, AssertionError, KeyboardInterrupt):
                    self.assertIsNotNone(fault)
                    self.assertNotIn('complete', phase)
            self.assertEqual(property_value, ['1'])
            self.assertTrue(metadata)
            self.assertEqual(len(removed), 2 * len(tools))
            self.assertTrue(all('native-AA-' in p for p in removed))
            if fault is None:
                self.assertEqual(sum(e == 'trace-start' for e in events), 5)
                self.assertEqual(sum(e == 'record-start' for e in events), 3)
                self.assertEqual(sum(e == 'official-STARTED' for e in events), 3)
                self.assertTrue(phase['complete'])
                self.assertFalse(phase['AA_overlap_accepted'])
                self.assertEqual(len(set(tools)), 3)
                for ordinal, row in enumerate(phase['AA_windows'], 1):
                    self.assertIn('--keep-failed-unwinding-debug-info', row['command'])
                    self.assertIn('dwarf,49152', row['command'])
                    self.assertEqual(row['command'][row['command'].index('--start_profiling_fd') + 1], '1')
                    self.assertTrue((root / f'AA-record-{ordinal}.metadata.json').is_file())
                    self.assertTrue(row['capture_complete'])

    def test_three_distinct_record_and_five_trace_windows(self): self.exercise()
    def test_missing_ready_restores_owned_tool_and_property(self): self.exercise('ready')
    def test_cancel_restores_owned_tool_and_property(self): self.exercise('cancel')
    def test_trace_failure_is_not_complete(self): self.exercise('trace')
    def test_record_failure_is_not_complete(self): self.exercise('record')
    def test_invalid_payload_is_not_complete(self): self.exercise('payload')

    def test_actual_official_ready_pipe(self):
        process = subprocess.Popen(['python3', '-c', 'import os;os.write(1,b"STARTED")'], stdout=subprocess.PIPE)
        self.assertIn(b'STARTED', aa.started(process, io.BytesIO()))
        self.assertEqual(process.wait(timeout=5), 0)
        process.stdout.close()

    def test_actual_stdout_ready_then_close_still_captures_stderr(self):
        # Mirrors the official readyfd1 close: fd2 remains the merged console.
        code = 'import os;os.write(1,b"STARTED");os.close(1);os.write(2,b"after-ready-stderr")'
        process = subprocess.Popen(['sh', '-c', 'exec ' + shlex.join(['python3', '-c', code])],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        console = io.BytesIO()
        aa.started(process, console)
        tail, _ = process.communicate(timeout=5)
        self.assertIn(b'after-ready-stderr', console.getvalue() + tail)
        self.assertEqual(process.returncode, 0)

    def test_actual_eof_before_ready_refused(self):
        process = subprocess.Popen(['python3', '-c', 'pass'], stdout=subprocess.PIPE)
        with self.assertRaisesRegex(RuntimeError, 'exited before'):
            aa.started(process, io.BytesIO())
        self.assertEqual(process.wait(timeout=5), 0)
        process.stdout.close()

    def test_clock_overlap_requires_official_conversion(self):
        source = ast.parse((D / 'report-official-cpu-aa.py').read_text())
        node = next(n for n in source.body if isinstance(n, ast.FunctionDef) and n.name == 'contained')
        scope = {}; exec(compile(ast.Module(body=[node], type_ignores=[]), '<actual-overlap-check>', 'exec'), scope)
        inside = scope['contained']
        # Different MONOTONIC/BOOTTIME domains deliberately cannot be compared raw.
        self.assertFalse(inside(383130000000000, 383160000000000, 586266374013380, 586274353226762))
        self.assertTrue(inside(383130000000000, 383160000000000, 383137247183634, 383145226397016))
        self.assertFalse(inside(100, 120, 110, 121))
        self.assertFalse(inside(100, 120, 90, 110))

    def test_real_mode_validation_block(self):
        tree = ast.parse((D / 'run-game.py').read_text())
        node = next(n for n in ast.walk(tree) if isinstance(n, ast.If) and
                    ast.unparse(n.test) == "'official_cpu_aa' in p")
        code = compile(ast.Module(body=[node], type_ignores=[]), '<actual-AA-mode>', 'exec')
        good = {'official_cpu_aa': {'repetitions': 3, 'record_seconds': 30, 'sample_frequency_Hz': 99,
                                  'DWARF_bytes': 49152, 'keep_failed_debug_info': True},
                'performance_tools': {'source': True}, 'native_host_experiment': {'reviewed': True}}
        require = lambda v, text: self.assertTrue(v, text)
        exec(code, {'p': good, 'require': require})
        for change in [{'DWARF_bytes': 16384}, {'repetitions': 1}, {'sample_frequency_Hz': 199},
                       {'record_seconds': 8}, {'keep_failed_debug_info': False}]:
            bad = copy.deepcopy(good); bad['official_cpu_aa'].update(change)
            with self.assertRaises(AssertionError): exec(code, {'p': bad, 'require': require})
        for option in ('official_gpu_tools', 'official_l1i_record', 'counter_snapshot'):
            bad = copy.deepcopy(good); bad[option] = True
            with self.assertRaises(AssertionError): exec(code, {'p': bad, 'require': require})


if __name__ == '__main__':
    unittest.main()
