"""Offline control/ABI negative cases only; no phone or invented allocator oracle."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import struct
import hashlib
import correlate as c
import unittest
from unittest.mock import patch
D=Path(__file__).resolve().parent
sys.path.insert(0,str(D))
import sdk_startup_allocations as h
spec=importlib.util.spec_from_file_location('startup_wrapper',D/'run-debugger.py');w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
spec=importlib.util.spec_from_file_location('bulk_runner',D.parent/'native-bulk-game-v1/run-game.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)

class Control(unittest.TestCase):
    def setUp(self):
        self.p={'startup_allocation':{'mode':'f75-main-allocator-large-return-v1','minimum_bytes':134217728,'max_returns':12,'natural_wait_seconds':60,'metadata_budget_bytes':1048576},'ELF_sha256':'4a8420209e2ac6c71c161626472458ac498ee844bdce58677c8e2769b5ec4618','ELF_BuildID':'6e6f12c5c5f7a858704a84ee294b19d95aa3f894','sdk_original':{'sha256':'1c9f198a91c0bcfee5cf8f5b53fb941895e91334dfb766ce79fc68576797f017'},'sdk_flat':{'sha256':'f5682a032c0884fc7d8553adb0f7445c3f0ae92c961a9f95b6593d7f487b6ea0'}}
    def test_policy_bounded_count_not_exact_allocation_size(self):
        for n in (1,4,8,12,16):
            self.p['startup_allocation']['max_returns']=n;w.validate_startup(self.p)
        for n in (0,17,True,'12'):
            self.p['startup_allocation']['max_returns']=n
            with self.subTest(n=n),self.assertRaisesRegex(RuntimeError,'unbounded'):w.validate_startup(self.p)
    def test_no_widening_time_size_or_bytes(self):
        for key,value in [('natural_wait_seconds',61),('minimum_bytes',1),('metadata_budget_bytes',2097152),('mode','unknown')]:
            q=copy.deepcopy(self.p);q['startup_allocation'][key]=value
            with self.subTest(key=key),self.assertRaises(RuntimeError):w.validate_startup(q)
    def test_original_host_sdk_proof_is_exact(self):
        for key in ('ELF_sha256','ELF_BuildID'):
            q=copy.deepcopy(self.p);q[key]='0'*len(q[key])
            with self.assertRaises(RuntimeError):w.validate_startup(q)
        for key in ('sdk_original','sdk_flat'):
            q=copy.deepcopy(self.p);q[key]['sha256']='0'*64
            with self.assertRaises(RuntimeError):w.validate_startup(q)
    def test_register_semantics_and_null_return_kept(self):
        v=dict(x0=0x100000000,x19=4096,x20=250*1048576,x21=0x4000,x29=0x9000,sp=0x8fd0,pc=0xa000)
        self.assertEqual(h.qualifying_return(v,0x4000,134217728)['requested_bytes'],250*1048576)
        v['x0']=0;self.assertTrue(h.qualifying_return(v,0x4000,134217728)['null_result'])
        for key,value in [('x21',0),('x20',134217727),('x0',(1<<64)-1)]:
            q=dict(v);q[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):h.qualifying_return(q,0x4000,134217728)
    def test_unknown_missing_register_rejected(self):
        with self.assertRaisesRegex(ValueError,'missing'):h.qualifying_return({},1,1)
    def test_strict_owned_breakpoint_reasons(self):
        L=types.SimpleNamespace(eStopReasonNone=0,eStopReasonInvalid=1,eStopReasonBreakpoint=3)
        class Thread:
            def __init__(self,reason,data):self.reason,self.data=reason,data
            def GetStopReason(self):return self.reason
            def GetStopReasonDataCount(self):return len(self.data)
            def GetStopReasonDataAtIndex(self,i):return self.data[i]
        bp=types.SimpleNamespace(GetID=lambda:7);owned=Thread(3,[7,1])
        with patch.dict(sys.modules,{'lldb':L}):
            self.assertIs(h.breakpoint_thread([Thread(0,[]),owned],bp),owned)
            for ts in ([],[Thread(3,[8,1])],[Thread(3,[7,1,7,2])],[Thread(6,[])],[owned,owned]):
                with self.subTest(ts=ts),self.assertRaises(ValueError):h.breakpoint_thread(ts,bp)
    def test_old_runner_path_remains_noop(self):
        with patch.object(g.subprocess,'run',side_effect=AssertionError('must not invoke tools')):
            self.assertIsNone(g.startup_allocation_capture({}))
    def test_no_parallel_profile_or_missing_final_partition(self):
        p={'startup_allocation_capture':{'helper':{},'config':{}},'readonly_memory_snapshot':{},'prior_actual_game_qualification':{}}
        for k in (None,'performance_tools','official_gpu_tools','official_cpu_aa','official_l1i_record','counter_snapshot','sustained_play'):
            q=copy.deepcopy(p)
            if k:q[k]={'present':True}
            with self.subTest(key=k),self.assertRaisesRegex(RuntimeError,'without performance'):g.startup_allocation_capture(q)
    def test_success_needs_same_PID_no_calls_and_cleanup(self):
        ok={'snapshot_complete':True,'PID':123,'inferior_calls':False,'owned_PID_TracerPid_zero_after_cleanup':True,'owned_debugger_cleanup_complete':True,'cleanup_errors':[]}
        g.startup_allocation_result(ok,'123')
        for k,v in [('PID',124),('snapshot_complete',False),('inferior_calls',True),('owned_PID_TracerPid_zero_after_cleanup',False),('owned_debugger_cleanup_complete',False),('cleanup_errors',['failed'])]:
            q=dict(ok);q[k]=v
            with self.subTest(key=k),self.assertRaises(RuntimeError):g.startup_allocation_result(q,'123')
    def test_same_PID_partition_raw_and_Pool_overlap(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);base=0x100000;buf=bytearray(4096);nodes=[base+0x100+i*0x40 for i in range(3)]
            for i,node in enumerate(nodes):
                at=node-base;buf[at+0x13]=i+1;struct.pack_into('<QQ',buf,at+0x18,base+i*4096,1)
            a=d/'span-metadata-100000.bin';a.write_bytes(buf)
            b=d/'span-boundary-table.bin';b.write_bytes(struct.pack('<QQQ',*nodes))
            raw=[dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in (a,b)]
            snap=dict(PID=123,ELF_sha256='same',complete=True,partition_complete=True,central=base,central_pages=3,
                      raw_files=raw,stop_id=9,nvn_pools=dict(complete=True,pools=[dict(identity=7,guest=base,size=512)]))
            start=dict(PID=123,ELF_sha256='same',returns=[dict(pointer=base+128,requested_bytes=64,stop_id=3)])
            out=c.correlate(start,snap)
            self.assertEqual(out['decoded_partition_span_count'],3)
            self.assertEqual(out['returns'][0]['later_containing_span']['state'],1)
            self.assertEqual(out['returns'][0]['current_Pool_intersections'][0]['identity'],7)
            self.assertFalse(out['lifetime_continuity_proven'])
            for key,value in [('PID',124),('ELF_sha256','other')]:
                bad=dict(start);bad[key]=value
                with self.assertRaisesRegex(ValueError,'cross-PID'):c.correlate(bad,snap)
            a.write_bytes(b'bad')
            with self.assertRaisesRegex(ValueError,'raw bytes changed'):c.correlate(start,snap)

    def test_launcher_mutation_is_only_perlaunch_D(self):
        text=(D.parent/'android-game-launch.py').read_text()
        self.assertIn("launch.insert(3, '-D')",text)
        self.assertNotIn('set-debug-app',text);self.assertNotIn('clear-debug-app',text)
        self.assertIn('timeout=120 if args.wait_java_debugger else 15',text)
    def test_prelaunch_hook_precedes_HUD_and_postroute_snapshot(self):
        text=(D.parent/'native-bulk-game-v1/run-game.py').read_text();start=text.index('def phase_game')
        arm=text.index("if own and p.get('startup_allocation_capture')",start)
        self.assertLess(text.index('m.claim_pid(',start),arm)
        self.assertLess(arm,text.index("launch = read(out / 'launch.json')",start))
        self.assertLess(arm,text.index('m.original_route(',start))
        self.assertIn("'early_no_native_start_stopped_without_save_claim'",text)
    def test_only_official_NDK_JDWP_client_no_custom_protocol(self):
        text=(D/'run-debugger.py').read_text()
        self.assertIn('ndkgdb.pyz',text);self.assertIn('--internal-wakeup-pid-with-jdb',text)
        self.assertNotIn('JDWP-Handshake',text)
        hook=(D/'sdk_startup_allocations.py').read_text()
        self.assertNotIn('EvaluateExpression(',hook)
        self.assertIn("sdk._read_small(process,sm['base']+ENTRY,ENTRY_SIZE)",hook)
        self.assertIn("thread backtrace -c 64",hook)

if __name__=='__main__':unittest.main()
