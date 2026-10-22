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
        if args == ('cat', 'files/runtime/game-state.jsonl'):
            return json.dumps({'valid': True, 'world_input_ready': True})
        if args == ('cat', 'files/runtime/native.log'):
            return 'original game test log'
        raise AssertionError(args)


class LifecycleFailureTests(unittest.TestCase):
    def exercise(self, replacement, counter_failure=False, sustained_failure=False, memory_failure=False):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder) / 'phase'
            dev = Device()
            saved = []

            class Launcher:
                returncode = 0

                def __init__(self, cmd, **kwargs):
                    report = Path(cmd[cmd.index('--report') + 1])
                    report.write_text(json.dumps({
                        'pid': 12345, 'original_hud': True,
                        'actual_guest_heap': {'shared': True},
                        'actual_vulkan_provider': 'adreno',
                        'requested_legacy_astc_copy': False}))

                def poll(self):
                    return 0

            def collect(_dev, _pid, d, report, _out, label):
                if counter_failure or sustained_failure or memory_failure:
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
                 patch.object(t.subprocess, 'run', save), \
                 patch.object(t.m, 'screenshot_collect', collect), \
                 patch.object(t.m, 'original_route'), \
                 patch.object(t.m, 'screenshot_pull_remove',
                              side_effect=RuntimeError('injected retry failure')):
                with self.assertRaises((RuntimeError, subprocess.CalledProcessError)):
                    config = {'serial': '10CG570J59001U1',
                              'pool_window_bytes': 16777216}
                    if sustained_failure:
                        config['sustained_play'] = {'enabled': True}
                    if counter_failure:
                        config['counter_snapshot'] = {
                            'helper_path': 'counter-helper.py',
                            'config': {'path': '/local/config.json', 'sha256': 'a' * 64}}
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
            if not counter_failure and not sustained_failure and not memory_failure:
                self.assertTrue(state['screenshot_collection_errors'])
            if replacement:
                self.assertFalse(saved)
                self.assertFalse(dev.stopped)
                self.assertEqual(dev.pid, '54321')
            else:
                self.assertTrue(saved)
                self.assertTrue(dev.stopped)
                self.assertEqual(dev.pid, '')
                self.assertTrue(state['fresh_save_then_stopped'])

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
