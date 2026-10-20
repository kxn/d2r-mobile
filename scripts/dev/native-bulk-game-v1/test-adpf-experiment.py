#!/usr/bin/env python3
"""Offline actual mode/cleanup control flow; no ADB, API or performance claims."""
from pathlib import Path
import copy,hashlib,importlib.util,json,subprocess,tempfile,unittest
from types import SimpleNamespace
from unittest.mock import patch
S=Path(__file__).with_name('adpf_experiment.py');spec=importlib.util.spec_from_file_location('adpf_test',S);t=importlib.util.module_from_spec(spec);spec.loader.exec_module(t)

def require(v,msg):
 if not v:raise RuntimeError(msg)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

class Flow(unittest.TestCase):
 def exercise(self,case=None):
  with tempfile.TemporaryDirectory()as tmp:
   out=Path(tmp);(out/'official-performance').mkdir();clock=[0];captures=[]
   h={'reference':True,'available':-1,'active':0,'reports':0,'failures':0,'last_error':0,'tid':222}
   class Dev:
    pid='12345';toggles=[];pso=10
    def state(self):
     clock[0]+=1
     if h['active'] and not h['reference']:h['reports']+=1
     return {'time_ns':clock[0],'valid':True,'world_input_ready':True,'area':1,'x':5225,'y':6295,
             'scale_3d':.6,'game_output':{'width':2750,'height':1260},'surface_output':{'width':2750,'height':1260},
             'frame_pacing':{'target_fps':60},'pipeline_prepare':{'pipelines':self.pso},'performance_hint':copy.deepcopy(h)}
    def adb(self,*args):
     if args[:2]==('shell','pidof com.d2rmobile.nativeport || true'):return self.pid
     if args[0]=='logcat':return 'original save and background suspend'if case=='thermal'and not h['reference']else'Android API thermal status=0'
     assert args[:4]==('shell','am','start','-W')
     reference=args[-1]=='true';self.toggles.append(reference);h['reference']=reference
     if reference:h['active']=1 if case=='OFF-stuck'else 0
     else:
      h['available']=0 if case=='unsupported'else 1;h['active']=0 if case=='unsupported'else 1
      if case=='report-error':h.update(active=0,failures=1,last_error=32)
      if case=='scope-change':self.pso=11
     return 'sameActivity'
    def app(self,*args):
     if args[0]=='tail':return json.dumps(self.state())
     assert args==('cat','/proc/12345/task/222/comm')
     return 'foreign-thread'if case=='wrong-TID'else'nvn-translate'
   dev=Dev();base=dev.state();trace=out/'official-performance/completion-scheduler.pftrace';trace.write_bytes(b'baseline')
   report={'pid':12345,'apk_sha256':'apk','trace_sha256':sha(trace),'before':base,'after':copy.deepcopy(base)}
   trace.with_suffix('.json').write_text(json.dumps(report))
   p={'serial':'Q','new_apk':'apk','baseline_data':{'chain':'chain'},'adpf_experiment':{'scheduler_helper':{'path':'existing-official.py'}}}
   m=SimpleNamespace(PKG='com.d2rmobile.nativeport',sha=sha,read=lambda p:json.loads(Path(p).read_text()),require=require,
                     journal=lambda p,d:Path(p).write_text(json.dumps(d)),screenshot_collect=lambda *args:None)
   def capture(cmd,**kwargs):
    captures.append(cmd);assert kwargs['check'];assert cmd[cmd.index('--seconds')+1]=='8'
    if case=='tool-failure':raise subprocess.CalledProcessError(1,cmd)
    path=Path(cmd[cmd.index('--output')+1]);path.write_bytes(b'official Perfetto')
    before=dev.state()
    if case=='drop-in-window':h.update(active=0,failures=1,last_error=32)
    after=dev.state()
    if case=='no-report-progress':after['performance_hint']['reports']=before['performance_hint']['reports']
    meta={'pid':12345,'apk_sha256':'apk','shader_chain':'chain','trace_sha256':sha(path),'before':before,'after':after}
    path.with_suffix('.json').write_text(json.dumps(meta))
    if case=='PID-replaced':dev.pid='99999'
   with patch.object(t.subprocess,'run',side_effect=capture),patch.object(t.time,'sleep',return_value=None):
    if case in('tool-failure','OFF-stuck','PID-replaced','thermal'):
     with self.assertRaises((RuntimeError,subprocess.CalledProcessError)):t.run(dev,'12345',p,out,out/'phase.json',{},m)
     r=json.loads((out/'optional-adpf/adpf-result.json').read_text());self.assertFalse(r['experiment_complete'])
    else:r=t.run(dev,'12345',p,out,out/'phase.json',{},m)
   if case=='PID-replaced':self.assertEqual(dev.toggles,[False]);self.assertTrue(r['cleanup_errors'])
   elif case=='OFF-stuck':self.assertTrue(r['cleanup_errors']);self.assertFalse(r.get('reference_restored_active_zero',False))
   else:self.assertTrue(r['reference_restored_active_zero']);self.assertTrue(h['reference']);self.assertEqual(h['active'],0)
   if case is None:self.assertTrue(r['experiment_complete']);self.assertEqual(len(captures),2);self.assertEqual(dev.toggles,[False,True,True])
   else:self.assertFalse(r['experiment_complete'])
   return r
 def test_normal_ABA_reuses_baseline_and_restores_OFF(self):self.exercise()
 def test_actual_unsupported_is_not_success(self):self.exercise('unsupported')
 def test_session_or_report_error_is_not_success(self):self.exercise('report-error')
 def test_wrong_actual_TID_is_not_success(self):self.exercise('wrong-TID')
 def test_scope_change_still_restores_OFF(self):self.exercise('scope-change')
 def test_report_failure_during_window_not_false_pass(self):self.exercise('drop-in-window')
 def test_report_count_must_advance(self):self.exercise('no-report-progress')
 def test_tool_error_still_restores_OFF(self):self.exercise('tool-failure')
 def test_thermal_pause_is_fatal_but_restores_OFF(self):self.exercise('thermal')
 def test_OFF_stuck_is_cleanup_failure(self):self.exercise('OFF-stuck')
 def test_replacement_PID_never_gets_intent(self):self.exercise('PID-replaced')

if __name__=='__main__':unittest.main(verbosity=2)
