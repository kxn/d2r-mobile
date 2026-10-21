#!/usr/bin/env python3
"""Host-only optional JSON policy and original launch-command plumbing."""
import importlib.util
from contextlib import redirect_stderr
import io
from pathlib import Path
import runpy
import sys
import tempfile
import unittest
from unittest.mock import patch
D=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('span_policy_runner',D/'run-game.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
def deny(*args,**kwargs):raise AssertionError('device/process call forbidden in offline test')
class StopBeforeDevice(Exception):pass
class Policy(unittest.TestCase):
    def test_old_inputs_leave_policy_and_state_untouched(self):
        self.assertIsNone(g.driver_span_policy({},{}))
    def test_only_integer_startup_bool(self):
        for value in (0,1):self.assertIs(g.driver_span_policy({'driver_span_reference':value}),bool(value))
        for value in (False,True,None,-1,2,'0','1',[],{}):
            with self.subTest(value=value),self.assertRaisesRegex(RuntimeError,'startup-only0/1'):
                g.driver_span_policy({'driver_span_reference':value})
    def test_actual_endpoint_bool_required_at_both_boundaries(self):
        for value in (0,1):
            p={'driver_span_reference':value}
            for where in ('startup','final'):
                self.assertIs(g.driver_span_policy(p,{'driver_constants':{'span_reference':bool(value)}},where),bool(value))
                for state in ({},{'driver_constants':{}},{'driver_constants':{'span_reference':not bool(value)}},{'driver_constants':{'span_reference':value}}):
                    with self.subTest(value=value,where=where,state=state),self.assertRaisesRegex(RuntimeError,'actual '+where):g.driver_span_policy(p,state,where)
    def test_only_true_extends_original_command(self):
        for value in (None,0,1):
            captured={}
            def stop(path,data):captured.update(data);raise StopBeforeDevice()
            p={'serial':'host-only-test','pool_window_bytes':1048576}
            if value is not None:p['driver_span_reference']=value
            with tempfile.TemporaryDirectory() as tmp,patch.object(g.m,'journal',stop),patch('subprocess.run',deny),patch('subprocess.Popen',deny),patch('subprocess.check_output',deny):
                with self.assertRaises(StopBeforeDevice):g.phase_game(None,Path(tmp)/'phase',{'kind':'control'},p,'owned-test')
            cmd=captured['launch_command'];self.assertEqual(cmd.count('--driver-span-reference'),int(value==1))
            self.assertNotIn('--cache-driver-constants',cmd);self.assertNotIn('--no-cache-driver-constants',cmd)
            self.assertEqual(captured.get('driver_span_reference_requested'),None if value is None else bool(value))
    def test_fresh_final_state_after_all_official_windows(self):
        text=(D/'run-game.py').read_text();a=text.index("        if p.get('performance_tools'):",text.index('def phase_game'))
        b=text.index("final_policy_state = read_state(dev, out, 'after-span-policy')",a)
        self.assertLess(a,b);self.assertLess(b,text.index("native = dev.app('cat', 'files/runtime/native.log')",b))
        self.assertNotIn('shaders',dict(g.DIRS))
    def test_launcher_rejects_hot_switch_before_device(self):
        with patch.object(sys,'argv',['android-game-launch.py','--attach','--driver-span-reference']),patch('subprocess.run',deny),patch('subprocess.Popen',deny),patch('subprocess.check_output',deny):
            with redirect_stderr(io.StringIO()),self.assertRaises(SystemExit) as raised:runpy.run_path(str(D.parent/'android-game-launch.py'),run_name='__main__')
        self.assertEqual(raised.exception.code,2)
if __name__=='__main__':unittest.main()
