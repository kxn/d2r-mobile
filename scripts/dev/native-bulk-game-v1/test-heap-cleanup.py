#!/usr/bin/env python3
"""Offline fault injection; no adb, target malloc, trace or game is started."""
from pathlib import Path
import importlib.util
import json
import signal
import subprocess
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('heap_bulk_under_test', HERE / 'run-game.py')
t = importlib.util.module_from_spec(spec); spec.loader.exec_module(t)
h = t.heap


class HeapTests(unittest.TestCase):
    def test_actual_prior_partial_rejected(self):
        root = Path('/home/dl/d2r-mobile/.local/logs/native-bulk-heapprofd-q-capability-v1')
        health = h.read_csv((root / 'health.sql.existing-partial.stdout').read_text().split('\n\n')[0])
        dumps = h.read_csv((root / 'snapshots.sql.existing-partial.stdout').read_text())
        text = (root / 'existing-partial-official-textproto.txt').read_text()
        result = h.assess(health, dumps, text, 2105)
        self.assertFalse(result['accepted'])
        self.assertTrue(any('INVALID_STACK_BOUNDS' in r for r in result['reasons']))

    def test_health_gate(self):
        rows = [{'name': 'heapprofd_missing_packet', 'severity': 'error', 'value': '0'}]
        dumps = [{'pid': '12345', 'heap_name': 'libc.malloc', 'ts': '1'},
                 {'pid': '12345', 'heap_name': 'libc.malloc', 'ts': '2'}]
        text = '    process_dumps {\n      pid: 12345\n      from_startup: true\n      client_error: CLIENT_ERROR_NONE\n    }\n'
        self.assertTrue(h.assess(rows, dumps, text, 12345)['accepted'])
        for broken in [text.replace('from_startup: true', 'from_startup: false'),
                       text.replace('CLIENT_ERROR_NONE', 'CLIENT_ERROR_INVALID_STACK_BOUNDS'),
                       text.replace('client_error:', 'disconnected: true\n      client_error:')]:
            self.assertFalse(h.assess(rows, dumps, broken, 12345)['accepted'])
        self.assertFalse(h.assess(rows, dumps[:1], text, 12345)['accepted'])
        self.assertFalse(h.assess(rows, dumps, text, 54321)['accepted'])

    def test_replacement_collector_never_triggers_official_forwarded_INT(self):
        with tempfile.TemporaryDirectory() as directory:
            state = []
            cap = h.Capture(None, directory, {}, state.append)
            class Process:
                def __init__(self): self.signals=[]
                def poll(self): return None
                def send_signal(self, sig): self.signals.append(sig)
            cap.process = Process(); cap.collector = {'pid': 6789, 'trace_path': '/data/misc/perfetto-traces/profile-abcdef'}
            cap.identity = {'pid':6789, 'UID':2000, 'starttime':'1'}
            with patch.object(cap, '_identity', return_value={'pid':6789, 'UID':2000, 'starttime':'2'}):
                cap.finish('12345')
            self.assertFalse(cap.process.signals)
            self.assertFalse(cap.state['cleanup_confirmed'])
            self.assertTrue(any('replacement' in s for s in cap.state['errors']))

    def test_missing_collector_receipt_never_SIGINTs_unowned_initialization(self):
        with tempfile.TemporaryDirectory() as directory:
            cap = h.Capture(None, directory, {}, lambda _: None)
            class Process:
                def poll(self): return None
                def send_signal(self, _): raise AssertionError('must not forward SIGINT')
            cap.process = Process(); cap.finish(None)
            self.assertFalse(cap.state['cleanup_confirmed'])

    def test_late_receipt_after_finished_attempt_can_complete_exact_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            class Dev:
                def adb(self,*_):return ''
            class Process:
                alive=True
                def __init__(self):self.signals=[]
                def poll(self):return None if self.alive else 0
                def send_signal(self,sig):self.signals.append(sig);self.alive=False
            cap=h.Capture(Dev(),directory,{},lambda _:None);cap.process=Process()
            cap.finish(None)
            self.assertTrue(cap.finished);self.assertFalse(cap.state['cleanup_confirmed'])
            self.assertFalse(cap.process.signals)
            def late(_timeout=0):
                if cap.collector is None:cap._line(h.PREFIX+'{"pid":6789,"trace_path":"/data/misc/perfetto-traces/profile-abcdef"}')
            identity={'pid':6789,'UID':2000,'cmdline':['exact'],'exe':'/system/bin/perfetto','starttime':'1'}
            with patch.object(cap,'_pump',side_effect=late),patch.object(cap,'_identity',side_effect=[identity,None]),patch.object(cap,'_wait_helper',return_value=True):
                cap.finish(None)
            self.assertEqual(cap.process.signals,[signal.SIGINT])
            self.assertTrue(cap.state['cleanup_confirmed'])
            self.assertTrue(cap.state['late_receipt_cleanup_only'])
            self.assertFalse(cap.state['accepted'])

    def exercise_phase(self, health=True, cleanup=True, arm=True, replacement=False):
        events=[]
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'control'
            class Dev:
                pid=''
                def adb(self, *args):
                    if args == ('shell', 'pidof ' + t.PKG + ' || true'): return self.pid
                    if args == ('shell', 'am', 'force-stop', t.PKG):
                        self.pid=''; events.append('game-stop'); return ''
                    if args[:3] == ('shell', 'dumpsys', 'meminfo'):
                        events.append('meminfo'); return 'official meminfo'
                    return ''
                def app(self, *args):
                    if args == ('id','-u'):return '10311'
                    if args == ('cat','/proc/12345/status'):return 'Uid:\t10311\t10311\t10311\t10311\nTracerPid:\t0\n'
                    if args == ('cat','files/runtime/native.log'):return '[vulkan] device=Turnip Adreno (TM) 735 '
                    raise AssertionError(args)
            dev=Dev()
            class Capture:
                def __init__(self, _dev, _out, _cfg, journal):
                    self.state={'accepted':health,'cleanup_confirmed':cleanup,'stage':'mock-official-output'};self.journal=journal;self.done=False
                def start(self):events.append('profile-ready');self.journal(self.state);return arm
                def finish(self, pid):
                    if not self.done:
                        events.append('profile-finish');self.journal(self.state);self.done=True
                    return self.state
            class Launcher:
                returncode=0
                def __init__(self, cmd, **kwargs):
                    events.append('launcher');dev.pid='12345'
                    Path(cmd[cmd.index('--report')+1]).write_text(json.dumps({'pid':12345,'original_hud':True,'actual_guest_heap':{'shared':True},'actual_vulkan_provider':'adreno','requested_legacy_astc_copy':False}))
                def poll(self):return 0
            def route(*args):
                events.append('route')
                if replacement:dev.pid='54321'
            def runner(cmd, **kwargs):
                if 'counter-helper.py' in cmd:
                    events.append('counter')
                    result=Path(cmd[3])/'debugger-result.json'
                    result.write_text(json.dumps({'counter_snapshot_accepted':True,'cleanup_errors':[],'owned_PID_TracerPid_zero_after_cleanup':True,'snapshot':{}}))
                else:
                    self.assertIn('android-game-background-save.py',' '.join(cmd));events.append('save2')
                    Path(cmd[cmd.index('--report')+1]).write_text(json.dumps({'fresh_original_lifecycle_save':True,'save_commits':2,'pid':12345}))
                return subprocess.CompletedProcess(cmd,0)
            state={'valid':True,'world_input_ready':True,'scale_3d':.60,'game_output':{'width':2750,'height':1260},'surface_output':{'width':2750,'height':1260},'pool_windows':{'window_bytes':16777216},'guest_heap':{'shared':True}}
            cfg={'serial':'10CG570J59001U1','heap_profile':{},'counter_snapshot':{'helper_path':'counter-helper.py','config':{'path':'/config','sha256':'a'*64}}}
            # Nonempty presence matches proved real config.
            cfg['heap_profile']={'mode':'control-only'}
            with patch.object(t.heap,'Capture',Capture), patch.object(t.subprocess,'Popen',Launcher), patch.object(t.subprocess,'run',runner), patch.object(t,'read_state',return_value=state), patch.object(t.m,'screenshot_collect'), patch.object(t.m,'screenshot_pull_remove'), patch.object(t.m,'original_route',side_effect=route):
                if replacement:
                    with self.assertRaises(RuntimeError):t.phase_game(dev,out,{'kind':'control'},cfg,'token')
                else:t.phase_game(dev,out,{'kind':'control'},cfg,'token')
            report=json.loads((out/'phase.json').read_text())
            if replacement:
                self.assertNotIn('save2',events);self.assertNotIn('game-stop',events);self.assertEqual(dev.pid,'54321')
            elif arm:
                self.assertLess(events.index('profile-ready'),events.index('launcher'))
                self.assertLess(events.index('route'),events.index('profile-finish'))
                self.assertLess(events.index('profile-finish'),events.index('save2'))
                self.assertLess(events.index('save2'),events.index('game-stop'))
                self.assertTrue(report['fresh_save_then_stopped'])
                self.assertTrue(report['game_cleanup_independent_of_profile_health'])
                self.assertEqual(report['heap_profile']['accepted'],health)
                if cleanup:self.assertLess(events.index('profile-finish'),events.index('counter'))
                else:self.assertNotIn('counter',events)
            else:
                self.assertNotIn('launcher',events);self.assertNotIn('save2',events);self.assertEqual(dev.pid,'')
                self.assertTrue(report['heap_profile_arm_failed_no_game_launch'])

    def test_healthy_profile_counter_then_game_cleanup(self):self.exercise_phase()
    def test_error2_profile_still_saves_owned_game_without_runtime_failure(self):self.exercise_phase(health=False)
    def test_pending_tool_cleanup_still_saves_game_and_skips_LLDB(self):self.exercise_phase(health=False,cleanup=False)
    def test_readiness_failure_does_not_launch_game(self):self.exercise_phase(health=False,arm=False)
    def test_replacement_game_never_saved_or_stopped(self):self.exercise_phase(replacement=True)


if __name__=='__main__':unittest.main()
