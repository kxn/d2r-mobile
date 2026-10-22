#!/usr/bin/env python3
"""Original reader/input regression with fake ADB only; never touches a device."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import runpy
import subprocess
import sys
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('state_reader', HERE / 'game_state_json.py')
s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)


def wire(tick=1, ready=True, x=0, y=0):
    return json.dumps({'time_ns': tick, 'valid': ready, 'world_input_ready': ready,
                       'area': 1, 'x': x, 'y': y}) + '\n'


class CompleteLine(unittest.TestCase):
    def test_latest_complete_line_and_partial_final_line(self):
        self.assertEqual(s.latest_complete_state(wire(1) + wire(2))['time_ns'], 2)
        self.assertEqual(s.latest_complete_state(wire(1) + '{"pending":"' + 'x'*4096)['time_ns'], 1)
        self.assertEqual(s.latest_complete_state(wire(1) + wire(2).rstrip('\n'))['time_ns'], 1)

    def test_malformed_latest_complete_never_falls_back(self):
        for text in (wire() + '{oops}\n', wire() + '{oops}\n{"pending":', wire() + '\n'):
            with self.subTest(text=text), self.assertRaises(json.JSONDecodeError):
                s.latest_complete_state(text)

    def test_no_complete_or_non_object_record_is_rejected(self):
        for text in ('', '{"partial":', wire().rstrip('\n'), '[]\n'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                s.latest_complete_state(text)

    def test_stripping_app_preserves_commit_boundary_and_read_errors(self):
        for full in (False, True):
            def app(*args):
                self.assertEqual(args[:2], ('sh', '-c'))
                self.assertIn('set -e;', args[2])
                self.assertIn('cat "$1"' if full else 'tail -n 2 "$1"', args[2])
                self.assertEqual(args[-1], s.STATE_FILE)
                return (wire(1) + '{"pending":' + s._END).strip()
            text = s.read_state_text(app, full=full)
            self.assertEqual(s.latest_complete_state(text)['time_ns'], 1)
        with self.assertRaises(ValueError): s.read_state_text(lambda *a: 'missing terminator')
        with self.assertRaises(subprocess.CalledProcessError):
            s.read_state_text(lambda *a: (_ for _ in ()).throw(subprocess.CalledProcessError(1, ['tail'])))


class OriginalWalk(unittest.TestCase):
    def exercise(self, states, error=None, sleep_step=.2):
        clock = [0.]; calls = []; count = [0]
        def adb(cmd, **kwargs):
            calls.append(cmd)
            self.assertEqual(cmd[:3], ['adb', '-s', 'UNIT-OFFLINE'])
            if cmd[3:] == ['shell', 'wm', 'size']: return 'Physical size: 440x960\n'
            if cmd[3:6] == ['exec-out', 'run-as', 'com.d2rmobile.nativeport']:
                self.assertEqual(cmd[6:], ['tail', '-n', '2', s.STATE_FILE])
                value = states[min(count[0], len(states)-1)]; count[0] += 1
                return value
            self.assertEqual(cmd[3:6], ['shell', 'input', 'touchscreen'])
            return ''
        def sleep(seconds): clock[0] += sleep_step
        output = io.StringIO()
        argv = ['android-game-walk-to.py', '10', '10', '--timeout', '15',
                '--ready-timeout', '.5', '--serial', 'UNIT-OFFLINE']
        with patch.object(sys, 'argv', argv), patch.dict(sys.modules, {'game_state_json': s}), \
             patch.object(subprocess, 'check_output', side_effect=adb):
            # Patch only the existing walk scheduler clock, not a new timer.
            with patch('time.monotonic', side_effect=lambda: clock[0]), patch('time.sleep', side_effect=sleep), \
                 contextlib.redirect_stdout(output):
                if error:
                    with self.assertRaises(error): runpy.run_path(str(HERE / 'android-game-walk-to.py'), run_name='__main__')
                else:
                    runpy.run_path(str(HERE / 'android-game-walk-to.py'), run_name='__main__')
        touches = [c[7] for c in calls if c[3:6] == ['shell', 'input', 'touchscreen']]
        return touches, output.getvalue(), clock[0]

    def test_partial_tail_keeps_original_input_and_reaches_latest_complete_target(self):
        touches, output, _ = self.exercise([wire(1), wire(2) + '{"partial":', wire(3, x=10, y=10) + '{"partial":'])
        self.assertEqual(touches, ['DOWN', 'MOVE', 'UP'])
        self.assertTrue(json.loads(output.splitlines()[-1])['reached'])

    def test_malformed_complete_during_motion_releases_UP_and_fails(self):
        touches, _, _ = self.exercise([wire(1), wire(2), '{oops}\n{"partial":'], error=json.JSONDecodeError)
        self.assertEqual(touches[-1], 'UP')

    def test_no_complete_record_fails_before_any_input(self):
        touches, _, _ = self.exercise(['{"partial":'], error=ValueError)
        self.assertEqual(touches, [])

    def test_current_complete_not_ready_does_not_adopt_older_ready(self):
        touches, _, elapsed = self.exercise([wire(2, ready=False) + '{"partial":'], error=RuntimeError)
        self.assertEqual(touches, []); self.assertGreaterEqual(elapsed, .5)

    def test_midwalk_not_ready_still_releases_UP_and_fails(self):
        touches, _, _ = self.exercise([wire(1), wire(2), wire(3, ready=False) + '{"partial":'], error=RuntimeError)
        self.assertEqual(touches[-1], 'UP')

    def test_partial_stream_cannot_bypass_original_eight_second_staleness(self):
        touches, _, elapsed = self.exercise([wire(1) + '{"partial":'], error=RuntimeError, sleep_step=8.1)
        self.assertEqual(touches[-1], 'UP'); self.assertGreater(elapsed, 8)


if __name__ == '__main__': unittest.main(verbosity=2)
