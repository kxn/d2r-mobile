#!/usr/bin/env python3
"""Offline official-tool/transaction guards; never adb or target execution."""
from pathlib import Path
import hashlib, importlib.util, json, subprocess, tempfile, unittest
from unittest.mock import patch
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('malloc_transaction_test',HERE/'run-game.py')
t=importlib.util.module_from_spec(spec);spec.loader.exec_module(t);h=t.malloc
VIEWER=HERE/'official/native_heapdump_viewer.compat.py'
DUMP='''Android Native Heap Dump v1.2
Total memory: 340
Allocation records: 3
Backtrace size: 16
z 1 sz 100 num 2 bt
z 1 sz 50 num 1 bt 1001
z 1 sz 30 num 3 bt 1002 1003 1004 1005
MAPS
1000-2000 r-xp 00000000 00:00 1 /data/app/owned/lib/arm64/libdmhost_android.so
END
'''

class MallocToolTests(unittest.TestCase):
    def test_official_zero_frames_gap_and_compat_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            f=Path(directory)/'dump';f.write_text(DUMP)
            original=h.load(HERE/'official/native_heapdump_viewer.upstream.py')
            bt,_=original.ParseNativeHeap(str(f),False,True,'')
            self.assertEqual(sum(b.size*b.num_allocs for b in bt),140)
            stats,_=h.coverage(f,VIEWER)
            self.assertEqual(stats['total_requested_live_bytes'],340)
            self.assertEqual(stats['empty_stack_bytes'],200)
            self.assertEqual(stats['one_frame_stack_bytes'],50)
            self.assertEqual(stats['short_stack_under4_bytes'],250)
            self.assertEqual(stats['host_or_driver_stack_bytes'],140)
            self.assertEqual(stats['no_host_or_driver_stack_bytes'],200)

    def test_incomplete_or_inconsistent_official_dump_never_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            f=Path(directory)/'dump'
            for text in [DUMP.replace('END\n',''),DUMP.replace('Total memory: 340','Total memory: 339'),DUMP.replace('Allocation records: 3','Allocation records: 2'),DUMP.replace('Backtrace size: 16','Backtrace size: 64'),DUMP.replace('1000-2000 r-xp','missing-map')]:
                f.write_text(text)
                with self.assertRaises((RuntimeError,TypeError)):h.coverage(f,VIEWER)

    def collect_case(self,failure=None):
        class Dev:
            command=['not-adb']
            signalled=False
            def adb(self,*args):
                if args==('shell','pidof '+t.PKG+' || true'):
                    return '54321' if failure=='replacement' else '12345'
                return '' if failure=='receipt' else "malloc debug enabled\nRun: 'kill -47 12345' to dump the backtrace."+("\nunknown option backtrace_full" if failure=='unknownoption' else "")+("\n+++ ALLOCATION 0x123 UNKNOWN POINTER (free)" if failure=='unknownfree' or (failure=='unknownlate' and self.signalled) else "")
            def app(self,*args):
                if args==('cat','/proc/12345/status'):
                    return 'Uid:\t'+('\t'.join(['10312' if failure=='uid' else '10311']*4))+'\nTracerPid:\t'+('7' if failure=='tracer' else '0')+'\nSigCgt:\t'+('0' if failure=='handler' else format(1<<46,'x'))+'\n'
                if args==('cat','/proc/12345/maps'):return '' if failure=='shim' else 'libc_malloc_debug.so'
                if args[0]=='sha256sum':return 'd'*64+' file' if args[1].startswith('files/') else hashlib.sha256(DUMP.encode()).hexdigest()+' file'
                if args==('cat','/proc/12345/environ'):return '' if failure=='environment' else 'LIBC_DEBUG_MALLOC_OPTIONS=exact\x00'
                if args[:2]==('sh','-c'):return 'PRESENT' if self.signalled or failure=='preexisting' else ''
                if args[:2]==('kill','-47'):self.signalled=True;return ''
                if args[:3]==('tail','-n','1'):return 'NOT-END' if failure=='deadline' else 'END'
                raise AssertionError(args)
        cfg={'dump_prefix':'/private/owned','driver_symbols':{'sha256':'d'*64},'malloc_options':'exact','viewer':str(VIEWER)}
        with tempfile.TemporaryDirectory() as directory:
            dev=Dev();events=[]
            def export(command,**kwargs):kwargs['stdout'].write(DUMP.encode());return subprocess.CompletedProcess(command,0)
            with patch.object(h.subprocess,'run',side_effect=export),patch.object(h.time,'monotonic',side_effect=[0,26]),patch.object(h.time,'sleep'):
                result=h.collect(dev,Path(directory)/'official',cfg,'12345',10311,events.append)
            return result,dev.signalled

    def test_single_signal_complete_dump_defers_viewer(self):
        result,sent=self.collect_case()
        self.assertTrue(sent);self.assertTrue(result['accepted']);self.assertTrue(result['viewer_pending_after_game_stop'])
        self.assertEqual(result['coverage']['empty_stack_bytes'],200)

    def test_missing_shim_handler_receipt_environment_or_owner_never_signalled(self):
        for failure in ['shim','handler','receipt','environment','uid','tracer','replacement','preexisting','unknownoption','unknownfree']:
            with self.subTest(failure=failure):
                result,sent=self.collect_case(failure)
                self.assertFalse(sent);self.assertFalse(result['accepted']);self.assertTrue(result['errors'])

    def test_late_unknown_pointer_invalidates_already_exported_dump(self):
        result,sent=self.collect_case('unknownlate')
        self.assertTrue(sent);self.assertFalse(result['accepted']);self.assertIn('dump_sha256',result)

    def test_timeout_no_repeat_signal_and_untrusted_evidence(self):
        result,sent=self.collect_case('deadline')
        self.assertTrue(sent);self.assertFalse(result['accepted'])
        self.assertIn('incomplete_dump_sha256',result)

    def exercise_phase(self,valid=True,replacement=False,tracer=False,viewer_error=False,unknown=False,unknown_after_stop=False):
        events=[]
        class Dev:
            pid='';stopped=False
            def adb(self,*args):
                if args==('shell','pidof '+t.PKG+' || true'):return self.pid
                if args==('shell','am','force-stop',t.PKG):self.pid='';self.stopped=True;events.append('stop');return ''
                if args[:2]==('shell','logcat') and (unknown or (unknown_after_stop and not self.pid)):return 'UNKNOWN POINTER (free)'
                return ''
            def app(self,*args):
                if args==('id','-u'):return '10311'
                if args==('cat','/proc/12345/status'):return 'Uid:\t10311\t10311\t10311\t10311\nTracerPid:\t'+('9' if tracer else '0')+'\n'
                if args==('cat','files/runtime/native.log'):return '[vulkan] device=Turnip Adreno (TM) 735 '
                raise AssertionError(args)
        dev=Dev()
        with tempfile.TemporaryDirectory() as directory:
            out=Path(directory)/'phase'
            class Launcher:
                returncode=0
                def __init__(self,cmd,**kwargs):
                    dev.pid='12345';events.append('launch')
                    Path(cmd[cmd.index('--report')+1]).write_text(json.dumps({'pid':12345,'original_hud':True,'actual_guest_heap':{'shared':True},'actual_vulkan_provider':'adreno','requested_legacy_astc_copy':False}))
                def poll(self):return 0
            def collect(_dev,_out,_cfg,_pid,_uid,journal):
                events.append('dump');result={'accepted':valid,'dump_complete':valid,'errors':[]};journal(result)
                if replacement:dev.pid='54321'
                return result
            def viewer(_out,_cfg,state,journal):
                self.assertFalse(dev.pid);events.append('viewer');state['accepted']=not viewer_error;journal(state)
            def save(cmd,**kwargs):
                self.assertIn('android-game-background-save.py',' '.join(cmd));events.append('save2')
                Path(cmd[cmd.index('--report')+1]).write_text(json.dumps({'fresh_original_lifecycle_save':True,'save_commits':2,'pid':12345}))
                return subprocess.CompletedProcess(cmd,0)
            state={'valid':True,'world_input_ready':True,'scale_3d':.60,'game_output':{'width':2750,'height':1260},'surface_output':{'width':2750,'height':1260},'pool_windows':{'window_bytes':16777216},'guest_heap':{'shared':True}}
            cfg={'serial':'10CG570J59001U1','malloc_debug':{'mode':'control-only'}}
            with patch.object(t.subprocess,'Popen',Launcher),patch.object(t.subprocess,'run',side_effect=save),patch.object(t,'read_state',return_value=state),patch.object(t.m,'screenshot_collect'),patch.object(t.m,'screenshot_pull_remove'),patch.object(t.m,'original_route'),patch.object(h,'collect',side_effect=collect),patch.object(h,'analyze_after_game_stop',side_effect=viewer):
                if replacement or tracer:
                    with self.assertRaises(RuntimeError):t.phase_game(dev,out,{'kind':'control'},cfg,'owned')
                else:t.phase_game(dev,out,{'kind':'control'},cfg,'owned')
            report=json.loads((out/'phase.json').read_text())
        if replacement or tracer:
            self.assertNotIn('save2',events);self.assertNotIn('stop',events);self.assertNotIn('viewer',events)
        else:
            self.assertLess(events.index('dump'),events.index('save2'));self.assertLess(events.index('save2'),events.index('stop'))
            if valid and not (unknown or unknown_after_stop):self.assertLess(events.index('stop'),events.index('viewer'))
            else:self.assertNotIn('viewer',events)
            self.assertTrue(report['fresh_save_then_stopped'])
            self.assertEqual(report['malloc_debug']['accepted'],valid and not viewer_error and not (unknown or unknown_after_stop))

    def test_healthy_dump_analyzed_after_owned_game_save_and_stop(self):self.exercise_phase()
    def test_invalid_tool_does_not_bypass_owned_game_save_stop(self):self.exercise_phase(valid=False)
    def test_replacement_game_never_saved_stopped_or_analyzed(self):self.exercise_phase(replacement=True)
    def test_active_tracer_prevents_lifecycle_actions(self):self.exercise_phase(tracer=True)
    def test_official_viewer_failure_keeps_cleanup_and_distinct_failure(self):self.exercise_phase(viewer_error=True)
    def test_late_unknown_pointer_cannot_be_overridden_by_viewer(self):self.exercise_phase(unknown=True)
    def test_unknown_pointer_during_save_stop_cannot_be_overridden_by_viewer(self):self.exercise_phase(unknown_after_stop=True)

class FailedLaunchLifecycleTests(unittest.TestCase):
    def exercise(self,proven):
        events=[]
        class Dev:
            pid='12345'
            def adb(self,*args):
                if args==('shell','pidof '+t.PKG+' || true'):return self.pid
                if args==('shell','am','force-stop',t.PKG):events.append('stop');self.pid=''
                return ''
            def app(self,*args):
                if args==('id','-u'):return '10311'
                if args==('cat','/proc/12345/status'):return 'Uid:\t10311\t10311\t10311\t10311\nTracerPid:\t0\n'
                return ''
        class Launcher:
            returncode=1
            def __init__(self,*args,**kwargs):pass
            def poll(self):return 1
        def pause(dev,out,pid,uid,journal,cfg):
            events.append('pause')
            if not proven:raise RuntimeError('unknown world remains paused')
            result={'accepted':True,'never_world_confirmed':True,'no_save_required':True};journal(result);return result
        dev=Dev()
        with tempfile.TemporaryDirectory()as directory,patch.object(t.subprocess,'Popen',Launcher),patch.object(h,'pause_failed_cold_start',side_effect=pause):
            out=Path(directory)/'phase'
            with self.assertRaises(RuntimeError):t.phase_game(dev,out,{'kind':'control'},{'serial':'10CG570J59001U1','malloc_debug':{'mode':'control-only'}},'owned')
            report=json.loads((out/'phase.json').read_text())
        self.assertEqual('stop'in events,proven)
        if proven:
            self.assertTrue(report['never_world_paused_then_stopped']);self.assertNotIn('fresh_save_then_stopped',report)
        else:self.assertEqual(dev.pid,'12345')
    def test_never_world_cold_failure_stops_without_false_save2(self):self.exercise(True)
    def test_unknown_world_cold_failure_remains_paused_no_force_stop(self):self.exercise(False)

class ColdCleanupTests(unittest.TestCase):
    def run_case(self,kind):
        events=[]
        class Dev:
            paused=False
            def adb(self,*args):
                if args==('shell','pidof '+t.PKG+' || true'):return '54321' if kind=='replacement' and self.paused else '12345'
                if args==('shell','input','keyevent','HOME'):self.paused=True;events.append('HOME');return ''
                if args[:3]==('shell','dumpsys','activity'):
                    pkg='other.package' if kind=='wrongactivity' else t.PKG
                    return '* Hist #0: ActivityRecord{abc u0 '+pkg+'/.MainActivity t3}\n  state=STOPPED\n'
                if args[:3]==('shell','am','start'):events.append('foreground');return ''
                raise AssertionError(args)
            def app(self,*args):
                if args==('cat','/proc/12345/status'):return 'Uid:\t10311\t10311\t10311\t10311\nTracerPid:\t0\n'
                if args==('cat','/proc/12345/maps'):return '0000-1000 r-xp 0000 00:00 1 /data/app/owned/lib/arm64/libdmhost_android.so'
                if args[0]=='sha256sum':return ('e' if kind=='foreignELF' else 'd')*64+' file'
                if args==('cat','files/runtime/native.log'):
                    base='Android native start pid=12345\n[dm] [长帧] 面板 MainMenuLinks，区域 0（未读到玩家）\n'
                    if kind in ('worldunknown','savedworld'):base+='PanelManager OpenPanel HUDPanel\n'
                    if kind=='previoussave':base+='Android background save asked=1 commits=2\n'
                    if self.paused:
                        if kind!='nofocus':base+='oe 焦点状态 → 3\n'
                        if kind=='savedworld':base+='Android background save asked=1 commits=2 froze=1\n'
                        if kind!='noquiescence':base+='[nvn] 图形挂起：在途 GPU 工作已清空\n'
                    return base
                raise AssertionError(args)
        cfg={'lifecycle_proof_checked':kind!='noproof','runtime_symbols':{'sha256':'d'*64}}
        with tempfile.TemporaryDirectory()as directory,patch.object(h.time,'monotonic',side_effect=[0,20,40]),patch.object(h.time,'sleep'):
            dev=Dev();journal=[]
            if kind not in ('menu','savedworld'):
                with self.assertRaises(RuntimeError):h.pause_failed_cold_start(dev,Path(directory),'12345',10311,journal.append,cfg)
                self.assertEqual('HOME'in events,kind not in ('noproof','foreignELF'))
            else:
                result=h.pause_failed_cold_start(dev,Path(directory),'12345',10311,journal.append,cfg)
                self.assertTrue(result['accepted'])
                self.assertEqual(result.get('never_world_confirmed',False),kind=='menu')
                self.assertEqual(result.get('save_commits'),2 if kind=='savedworld' else None)
        return events
    def test_exact_target_positive_pause_menu_no_fake_save(self):self.run_case('menu')
    def test_real_world_requires_original_fresh_save2(self):self.run_case('savedworld')
    def test_world_without_save_is_paused_not_accepted(self):self.run_case('worldunknown')
    def test_no_reviewed_proof_never_assumes_contract(self):self.run_case('noproof')
    def test_foreign_loaded_ELF_never_assumes_contract(self):self.run_case('foreignELF')
    def test_other_activity_STOPPED_is_not_our_pause(self):self.run_case('wrongactivity')
    def test_replacement_after_HOME_never_accepted(self):self.run_case('replacement')
    def test_no_new_quiescence_never_assumes_completion(self):self.run_case('noquiescence')
    def test_no_new_focus_does_not_use_surface_only_suspend(self):self.run_case('nofocus')
    def test_previous_world_save_history_blocks_menu_assumption(self):self.run_case('previoussave')

if __name__=='__main__':unittest.main()
