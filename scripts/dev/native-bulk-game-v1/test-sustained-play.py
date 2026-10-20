#!/usr/bin/env python3
"""Offline orchestration fault injection; subprocess/device execution forbidden."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('sustained_under_test', HERE / 'sustained_play.py')
t = importlib.util.module_from_spec(spec);spec.loader.exec_module(t)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Flow(unittest.TestCase):
    def exercise(self, failure=None):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp);clock = [0.];events = [];tick = [0];walks = [0]
            p = {'serial': 'Q', 'new_apk': 'apk', 'baseline_data': {'chain': 'chain'},
                 'pool_window_bytes': 1048576,
                 'sustained_play': {'interval_seconds': 600, 'resume_step': 0,
                   'route': {'path': 'verified'}, 'scheduler_helper': {'path': 'official-scheduler.py'}},
                 '_sustained_route': {'kind': 'accepted-camp', 'scope': 'town', 'steps': t.CAMP}}
            def state():
                tick[0] += 1
                return {'valid': True, 'world_input_ready': True, 'area': 1,
                    'x': 5225, 'y': 6289, 'time_ns': tick[0], 'scale_3d': .6,
                    'game_output': {'width': 2750, 'height': 1260},
                    'surface_output': {'width': 2750, 'height': 1260},
                    'guest_heap': {'shared': True}, 'pool_windows': {'window_bytes': 1048576},
                    'pipeline_prepare': {'pipelines': tick[0]}, 'frame_pacing': {'target_fps': 30}}
            class Dev:
                def adb(self, *args):
                    events.append(args)
                    assert not any('sysfs' in str(v) or '/sys/' in str(v) or 'thermalservice' in str(v) for v in args)
                    if args[0] == 'logcat':
                        if failure == 'thermal' and walks[0] > 0:
                            return 'Persistent/severe thermal pressure: original save and background suspend'
                        return 'Android API thermal status=2'
                    if args[:3] == ('shell', 'dumpsys', 'meminfo'):
                        return 'official meminfo'
                    return '12345' if failure != 'PID' or walks[0] == 0 else 'other'
                def app(self, *args):
                    assert args == ('tail', '-n', '1', 'files/runtime/game-state.jsonl')
                    return json.dumps(state())
            dev = Dev()
            m = SimpleNamespace(PKG='com.d2rmobile.nativeport', require=require, sha=digest,
                journal=lambda path,data: Path(path).write_text(json.dumps(data)),
                screenshot_collect=lambda *args: events.append(('screenshot',)))
            def child(cmd, **kwargs):
                assert kwargs['check']
                if 'android-game-walk-to.py' in ' '.join(cmd):
                    walks[0] += 1
                    clock[0] += 200 # fake scheduling only; not a runtime measurement
                    if failure == 'walk' or (failure == 'reverse' and walks[0] == 3):
                        raise subprocess.CalledProcessError(2, cmd)
                    if failure == 'cancel':
                        p['_operator_stop_requested'] = True
                    if failure == 'request':
                        (out / 'stop-request.json').write_text(json.dumps({'token':'owned','action':'save-stop'}))
                    return
                events.append(('trace',))
                assert '--seconds' in cmd and cmd[cmd.index('--seconds') + 1] == '8'
                if failure == 'tool' and walks[0] > 0:
                    raise subprocess.CalledProcessError(1, cmd)
                trace = Path(cmd[cmd.index('--output') + 1]);trace.write_bytes(b'official trace')
                report = {'pid': 12345, 'seconds_requested': 8, 'apk_sha256': 'apk',
                          'shader_chain': 'chain', 'trace_sha256': digest(trace)}
                if failure == 'identity':
                    report['pid'] = 9999
                trace.with_suffix('.json').write_text(json.dumps(report));clock[0] += 8
            d = {'transaction_token': 'owned'}
            with patch.object(t.time, 'monotonic', side_effect=lambda: clock[0]), \
                 patch.object(t.subprocess, 'run', side_effect=child), \
                 patch.object(t.subprocess, 'Popen', side_effect=AssertionError('no real child')):
                if failure:
                    with self.assertRaises((RuntimeError, subprocess.CalledProcessError)):
                        t.run(dev, '12345', d, out/'phase.json', out, state(), p, m)
                    self.assertFalse(d['sustained_play']['continuous_pass'])
                else:
                    t.run(dev, '12345', d, out/'phase.json', out, state(), p, m)
                    self.assertTrue(d['sustained_play']['continuous_pass'])
                    self.assertEqual(len(d['sustained_play']['official_windows']), 4)
                    self.assertEqual(len(d['sustained_play']['snapshots']), 4)
                    self.assertGreaterEqual(clock[0], 1800)
                    self.assertTrue(all(v['complete'] for v in d['sustained_play']['official_windows']))
                    self.assertEqual(sum(v == ('screenshot',) for v in events), 4)
                    self.assertTrue(d['sustained_play']['resumed_segments_cannot_be_summed_as_continuous'])
            return d

    def test_normal_four_windows_and_sparse_observations(self): self.exercise()
    def test_thermal_pause_never_passes(self): self.exercise('thermal')
    def test_tool_failure_never_passes(self): self.exercise('tool')
    def test_PID_replacement_never_passes(self): self.exercise('PID')
    def test_walk_failure_never_passes(self):
        for failure in ('walk', 'reverse'):
            with self.subTest(failure=failure): self.exercise(failure)
    def test_cooperative_signal_flag_never_passes(self): self.exercise('cancel')
    def test_owned_stop_request_never_passes(self): self.exercise('request')
    def test_official_trace_identity_must_match(self): self.exercise('identity')


if __name__ == '__main__': unittest.main(verbosity=2)
