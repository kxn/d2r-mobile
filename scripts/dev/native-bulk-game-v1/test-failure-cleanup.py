#!/usr/bin/env python3
"""Offline failure injection for lifecycle ownership, never Android execution."""
from pathlib import Path
import importlib.util
import json
import tempfile
import unittest
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
    def exercise(self, replacement):
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
                d['screenshots'] = [{'remote': '/data/local/tmp/owned.png',
                                     'screencap_requested': True}]
                if replacement:
                    dev.pid = '54321'
                raise RuntimeError('injected screenshot pull failure')

            def save(cmd, **kwargs):
                self.assertIn('android-game-background-save.py', ' '.join(cmd))
                report = Path(cmd[cmd.index('--report') + 1])
                report.write_text(json.dumps({'fresh_original_lifecycle_save': True,
                                              'save_commits': 2, 'pid': 12345}))
                saved.append(True)

            with patch.object(t.subprocess, 'Popen', Launcher), \
                 patch.object(t.subprocess, 'run', save), \
                 patch.object(t.m, 'screenshot_collect', collect), \
                 patch.object(t.m, 'screenshot_pull_remove',
                              side_effect=RuntimeError('injected retry failure')):
                with self.assertRaises(RuntimeError):
                    t.phase_game(dev, out, {'kind': 'control'},
                                 {'serial': '10CG570J59001U1'}, 'test-token')
            state = json.loads((out / 'phase.json').read_text())
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


if __name__ == '__main__':
    unittest.main()
