#!/usr/bin/env python3
"""Offline failure injection for lifecycle ownership, never Android execution."""
from pathlib import Path
import importlib.util
import json
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('bulk_game_under_test', HERE / 'run-game.py')
t = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t)


class Device:
    def __init__(self):
        self.pid = '12345'
        self.stopped = False

    def adb(self, *args):
        if args == ('shell', 'pidof ' + t.PKG + ' || true'):
            return self.pid
        if args == ('shell', 'am', 'force-stop', t.PKG):
            self.stopped = True
            self.pid = ''
        return ''

    def app(self, *args):
        if args == ('id', '-u'):
            return '10311'
        if args == ('cat', '/proc/12345/status'):
            return 'Uid:\t10311\t10311\t10311\t10311\n'
        if args == ('sh', '-c', 'set -e; cat "$1"; printf %s ' + t.state_reader._END,
                    'sh', 'files/runtime/game-state.jsonl'):
            return (json.dumps({'valid': True, 'world_input_ready': True, 'time_ns': 1, 'scale_3d': .60,
                'game_output': {'width':2750,'height':1260}, 'surface_output': {'width':2750,'height':1260},
                'guest_heap': {'shared':True}, 'pool_windows': {'window_bytes':1048576},
                'private_image_cache': {'budget_bytes':536870912}, 'shared_lazy_windows': {'active':True},
                'shared_image_raw_observe': {'enabled_policy':False}, 'driver_constants': {'span_reference':True}})
                    + '\n' + t.state_reader._END).strip()
        if args == ('cat', 'files/runtime/native.log'):
            return 'original game test log'
        raise AssertionError(args)


class LifecycleFailureTests(unittest.TestCase):
    def exercise(self, replacement, counter_failure=False, sustained_failure=False, memory_failure=False, vma_failure=False, vma_save_failure=False, current=False, fixed_report=False, accounting_report=False, descriptor_failure=False):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder) / 'phase'
            dev = Device()
            saved = []

            self_test = self

            class Launcher:
                returncode = 0

                def __init__(self, cmd, **kwargs):
                    if current:
                        self_test.assertEqual(cmd[cmd.index('--scale-3d') + 1], '.75')
                        self_test.assertNotIn('--fixed-3d', cmd)
                        self_test.assertIn('--no-pool-copy-accounting', cmd)
                        self_test.assertNotIn('--pool-copy-accounting', cmd)
                    else:
                        self_test.assertEqual(cmd[cmd.index('--scale-3d') + 1], '.60')
                        self_test.assertIn('--fixed-3d', cmd)
                        self_test.assertIn('--pool-copy-accounting', cmd)
                    report = Path(cmd[cmd.index('--report') + 1])
                    report.write_text(json.dumps({
                        'pid': 12345, 'original_hud': True,
                        'actual_guest_heap': {'shared': True},
                        'actual_vulkan_provider': 'adreno',
                        'requested_legacy_astc_copy': False,
                        'requested_scale_3d': .75 if current else .60,
                        'requested_fixed_3d': fixed_report if current else True,
                        'requested_pool_copy_accounting': accounting_report if current else True,
                        'actual_pool_copy_accounting': not current}))

                def poll(self):
                    return 0

            def collect(_dev, _pid, d, report, _out, label):
                if counter_failure or sustained_failure or memory_failure or descriptor_failure:
                    return
                d['screenshots'] = [{'remote': '/data/local/tmp/owned.png',
                                     'screencap_requested': True}]
                if replacement:
                    dev.pid = '54321'
                raise RuntimeError('injected screenshot pull failure')

            def save(cmd, **kwargs):
                if (counter_failure and 'counter-helper.py' in cmd) or (memory_failure and any('memory-helper.py' in x for x in cmd)):
                    if replacement:
                        dev.pid = '54321'
                    raise subprocess.CalledProcessError(1, cmd)
                self.assertIn('android-game-background-save.py', ' '.join(cmd))
                if vma_failure:
                    self.assertIn('--vma-dump-on-suspend', cmd)
                if vma_save_failure:
                    raise subprocess.CalledProcessError(1, cmd)
                report = Path(cmd[cmd.index('--report') + 1])
                report.write_text(json.dumps({'fresh_original_lifecycle_save': True,
                                              'save_commits': 2, 'pid': 12345}))
                saved.append(True)

            def sustained_fail(*args):
                if replacement:
                    dev.pid = '54321'
                raise RuntimeError('injected sustained thermal/tool/input failure')

            with patch.object(t, 'module', return_value=SimpleNamespace(run=sustained_fail)), \
                 patch.object(t.subprocess, 'Popen', Launcher), \
                 patch.object(t, 'capture_memory_cpu_window', side_effect=sustained_fail), \
                 patch.object(t.subprocess, 'run', save), \
                 patch.object(t.m, 'screenshot_collect', collect), \
                 patch.object(t.m, 'original_route'), \
                 patch.object(t.m, 'screenshot_pull_remove',
                              side_effect=RuntimeError('injected retry failure')):
                with self.assertRaises((RuntimeError, subprocess.CalledProcessError)):
                    config = {'serial': '10CG570J59001U1',
                              'pool_window_bytes': 16777216, '_sustained_current': current}
                    if descriptor_failure:
                        config.update(runtime_commit='88620ec7c92edeb59512612cebf42906a4848509',
                            _descriptor_memory_runtime='88620ec7c92edeb59512612cebf42906a4848509',
                            native_memory_validation={'role':'parent'})
                    if sustained_failure:
                        config['sustained_play'] = {'enabled': True}
                    if counter_failure:
                        config['counter_snapshot'] = {
                            'helper_path': 'counter-helper.py',
                            'config': {'path': '/local/config.json', 'sha256': 'a' * 64}}
                    if vma_failure:
                        config['builtin_vma_on_suspend'] = True
                    if memory_failure:
                        helper=Path(folder)/'memory-helper.py';helper.write_text('# host fake only\n')
                        helper_config=Path(folder)/'memory-config.json';helper_config.write_text('{}\n')
                        config['readonly_memory_snapshot']={
                            'helper':{'path':str(helper),'sha256':t.sha(helper)},
                            'config':{'path':str(helper_config),'sha256':t.sha(helper_config)}}
                    t.phase_game(dev, out, {'kind': 'control'}, config, 'test-token')
            state = json.loads((out / 'phase.json').read_text())
            if memory_failure:
                self.assertIn('readonly_memory_snapshot_command',state)
            if not counter_failure and not sustained_failure and not memory_failure and not fixed_report and not accounting_report and not descriptor_failure:
                self.assertTrue(state['screenshot_collection_errors'])
            if vma_failure and not vma_save_failure:
                self.assertIn('builtin_vma_error', state)
            if replacement or vma_save_failure:
                self.assertFalse(saved)
                self.assertFalse(dev.stopped)
                self.assertEqual(dev.pid, '54321' if replacement else '12345')
            else:
                self.assertTrue(saved)
                self.assertTrue(dev.stopped)
                self.assertEqual(dev.pid, '')
                self.assertTrue(state['fresh_save_then_stopped'])

    def test_descriptor_trace_failure_still_saves_and_stops_owned_game(self):
        self.exercise(False, descriptor_failure=True)

    def test_descriptor_trace_PID_replacement_is_preserved(self):
        self.exercise(True, descriptor_failure=True)

    def test_current_adaptive_launch_failure_keeps_original_save2_stop(self):
        self.exercise(False, current=True, sustained_failure=True)

    def test_current_adaptive_launch_rejects_fixed_report_and_still_saves(self):
        self.exercise(False, current=True, fixed_report=True)

    def test_current_accounting_ON_report_is_rejected_and_owned_PID_saved(self):
        self.exercise(False, current=True, accounting_report=True)

    def test_current_replacement_PID_is_preserved(self):
        self.exercise(True, current=True, sustained_failure=True)

    def test_VMA_diagnostic_failure_after_save2_still_stops_owned_game(self):
        self.exercise(False, vma_failure=True)

    def test_VMA_helper_unconfirmed_save_remains_fail_closed(self):
        self.exercise(False, vma_failure=True, vma_save_failure=True)

    def test_failed_screenshot_retry_still_saves_and_stops_owned_game(self):
        self.exercise(False)

    def test_replacement_pid_is_never_saved_or_stopped(self):
        self.exercise(True)

    def test_sustained_failure_still_saves_and_stops_owned_game(self):
        self.exercise(False, sustained_failure=True)

    def test_sustained_replacement_PID_is_not_stopped(self):
        self.exercise(True, sustained_failure=True)

    def test_counter_tool_failure_still_saves_and_stops_owned_game(self):
        self.exercise(False, counter_failure=True)

    def test_counter_tool_replacement_pid_is_not_stopped(self):
        self.exercise(True, counter_failure=True)

    def test_memory_tool_failure_still_saves_and_stops_owned_game(self):
        self.exercise(False, memory_failure=True)

    def test_memory_tool_replacement_pid_is_not_stopped(self):
        self.exercise(True, memory_failure=True)


if __name__ == '__main__':
    unittest.main()
