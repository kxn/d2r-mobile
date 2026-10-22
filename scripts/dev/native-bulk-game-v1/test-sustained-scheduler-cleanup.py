#!/usr/bin/env python3
"""Exercise official Perfetto wrapper output cleanup offline; no device calls."""
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

HELPER = Path(__file__).with_name('capture-sustained-scheduler.py')


class Cleanup(unittest.TestCase):
    def exercise(self, failure=None):
        calls = [];remote = [];pid_reads = [0];state_reads = [0]
        with tempfile.TemporaryDirectory() as folder:
            trace = Path(folder)/'test.pftrace'
            def adb(cmd, **kwargs):
                args = cmd[3:];calls.append(args)
                if args[:3] == ['shell','rm','-f']:
                    remote.append(args[3]);return ''
                if args[:2] == ['shell','pidof']:
                    pid_reads[0] += 1
                    return '99999' if failure == 'PID' and pid_reads[0] > 1 else '12345'
                if args[:3] == ['shell','pm','path']:return 'package:/app/original.apk'
                if args[0] == 'shell':return 'a'*64+'  /app/original.apk'
                if args[3] == 'tail':
                    self.assertEqual(args[3:], ['tail','-n','2','files/runtime/game-state.jsonl'])
                    state_reads[0] += 1
                    if failure == 'state' and state_reads[0] > 1:return '{malformed}\n{"partial":'
                    return json.dumps({'valid':True,'world_input_ready':True}) + '\n{"partial":'
                if args[-1].endswith('native.log'):
                    return 'Android native start pid=12345\nexternal profiling: custom CPU/GPU timers disabled'
                if args[-1].endswith('.chain'):return 'chain'
                raise AssertionError(cmd)
            def run(cmd, **kwargs):
                args = cmd[3:];calls.append(args)
                if args[:2] == ['shell','perfetto']:
                    if failure == 'perfetto':raise subprocess.TimeoutExpired(cmd,38)
                    self.assertIn('duration_ms: 8000',kwargs['input'])
                elif args[0] == 'pull':
                    if failure == 'pull':raise subprocess.CalledProcessError(1,cmd)
                    Path(args[2]).write_bytes(b'x'*2048)
                else:raise AssertionError(cmd)
            with patch.object(sys,'argv',[str(HELPER),'--serial','Q','--seconds','8','--output',str(trace)]), \
                 patch('subprocess.check_output',side_effect=adb),patch('subprocess.run',side_effect=run):
                if failure:
                    with self.assertRaises((subprocess.TimeoutExpired,subprocess.CalledProcessError,SystemExit,json.JSONDecodeError)):
                        runpy.run_path(str(HELPER),run_name='__main__')
                else:runpy.run_path(str(HELPER),run_name='__main__')
            self.assertEqual(len(remote),1)
            self.assertTrue(remote[0].startswith('/data/misc/perfetto-traces/d2r-scheduler-'))
            self.assertEqual(trace.with_suffix('.json').exists(),failure is None)
    def test_normal_cleanup(self):self.exercise()
    def test_timeout_cleanup_and_no_success(self):self.exercise('perfetto')
    def test_pull_failure_cleanup_and_no_success(self):self.exercise('pull')
    def test_PID_mismatch_cleanup_and_no_success(self):self.exercise('PID')
    def test_malformed_complete_after_trace_still_cleans_remote(self):self.exercise('state')

if __name__=='__main__':unittest.main(verbosity=2)
