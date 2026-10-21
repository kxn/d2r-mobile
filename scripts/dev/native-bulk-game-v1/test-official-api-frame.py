#!/usr/bin/env python3
"""Offline ownership/cleanup controls; no Android or capture acceptance."""
from pathlib import Path
import ast,copy,hashlib,importlib.util,json,subprocess,tempfile,unittest
from unittest.mock import patch
D=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('api_frame',D/'official_api_frame.py');api=importlib.util.module_from_spec(spec);spec.loader.exec_module(api)
RAW=b'GFXR'+b'host-fake-not-device-proof'*10
class Device:
 def __init__(self):
  self.settings={k:('0'if k=='enable_gpu_debug_layers'else'null')for k in api.SETTINGS};self.props={k:''for k in ['debug.gfxrecon.capture_process_name','debug.gfxrecon.capture_file','debug.gfxrecon.capture_frames','debug.gfxrecon.capture_file_timestamp','debug.gfxrecon.capture_file_flush','debug.gfxrecon.capture_android_trigger','debug.gfxrecon.capture_trigger_frames','debug.gfxrecon.quit_after_capture_frames']};self.conflicts={k:''for k in api.CONFLICT};self.current='';self.triggered=False;self.prefix='';self.events=[];self.failure=None;self.closed=True
 def pid(self):return self.current
 def adb(self,*a):
  self.events.append(a)
  if a[:3]==('shell','pm','path'):return 'package:/official-layer.apk'
  if a[:3]==('shell','settings','get'):return self.settings[a[-1]]
  if a[:3]==('shell','settings','put'):self.settings[a[-2]]=a[-1];return''
  if a[:3]==('shell','settings','delete'):self.settings[a[-1]]='null';return''
  if a[:2]==('shell','getprop'):return self.props.get(a[-1],self.conflicts.get(a[-1],''))
  if a[0]=='shell'and a[1].startswith('setprop '):
   import shlex
   _,k,v=shlex.split(a[1]);
   if self.failure==k:self.failure=None;raise RuntimeError('fake single setter failure')
   self.props[k]=v
   if k=='debug.gfxrecon.capture_android_trigger'and v=='true':self.triggered=True
   return''
  if a[:2]==('shell','sha256sum'):
   return (api.LAYER_SHA if a[-1]=='/official-layer.apk'else hashlib.sha256(RAW).hexdigest())+'  '+a[-1]
  if a[0]=='logcat':return 'Finished recording graphics API capture'if self.triggered and self.closed else''
  if a[:2]==('shell','df'):return 'Filesystem 1K-blocks Used Available Use% Mounted on\n/data 9000000 100 8000000 1% /data'
  if a[0]=='shell'and a[1].startswith('set -eu; test ! -e '):return''
  if a[0]=='pull':
   assert not self.current,'bulk pull before originalstop';Path(a[-1]).write_bytes(RAW);return''
  if a[:2]==('shell','rm'):return''
  raise AssertionError(a)
 def app(self,*a):
  if a[:1]==('ls',):return self.prefix+'_trim_trigger.gfxr'if self.triggered else''
  if a[:1]==('cat',):return 'libVkLayer_gfxreconstruct.so\nlibvulkan_freedreno.so'
  if a[:1]==('stat',):return str(len(RAW))
  if a[:1]==('sha256sum',):return hashlib.sha256(RAW).hexdigest()+'  '+a[-1]
  raise AssertionError(a)
class Flow(unittest.TestCase):
 def setup_capture(self,root):
  def pin(p):return {'path':str(p),'sha256':api.sha(p)}
  p={'runtime_commit':'533820e6d3a76325f93b08e123a0118f00098c31','new_elf':'a'*64,'driver':{'sha256':'b'*64},'native_host_experiment':True,'native_candidate_validation':True,'official_api_frame':{'tool':'gfxreconstruct','frames':1,'timeout_seconds':90,'layer_apk':pin(api.LAYER_APK),'info':pin(api.INFO),'convert':pin(api.CONVERT)}}
  p['_official_api_frame']=api.validate(p,lambda r:Path(r['path']));dev=Device();cap=api.Capture(dev,root,p,'a'*32,dev.pid,lambda:None);dev.prefix=cap.prefix;return dev,cap,p
 def test_complete_closed_then_save_stop_pull_export(self):
  with tempfile.TemporaryDirectory()as t:
   d,c,p=self.setup_capture(Path(t));original=copy.deepcopy((d.settings,d.props));c.configure();d.current='12345';c.trigger('12345');c.restore();self.assertEqual((d.settings,d.props),original)
   with self.assertRaisesRegex(RuntimeError,'already stopped'):c.export_after_stop()
   d.current=''
   def official(argv,**kw):
    output=Path(argv[argv.index('--output')+1]);output.write_text(json.dumps({'detected-apis':['Vulkan'],'exe':{'name':api.PKG},'general-info':{'frames':{'actual-count':1,'total-count':1,'blank-count':0}}})if argv[0]==str(api.INFO)else'{"official_fake_API":true}\n')
   with patch.object(api.subprocess,'run',official):c.export_after_stop()
   self.assertEqual(c.r['stage'],'complete-API-structure-only-not-replay');self.assertFalse(c.r['replay_correctness_proven']);self.assertFalse(c.r['imported_host_payload_proven']);self.assertTrue(c.r['owned_raw_copy']['removed'])
 def test_partial_configuration_restores_exactly(self):
  with tempfile.TemporaryDirectory()as t:
   d,c,p=self.setup_capture(Path(t));old=copy.deepcopy((d.settings,d.props));d.failure='debug.gfxrecon.capture_file'
   with self.assertRaisesRegex(RuntimeError,'setter failure'):c.configure()
   c.restore();self.assertEqual((d.settings,d.props),old)
 def test_conflicting_range_preserved_before_mutation(self):
  with tempfile.TemporaryDirectory()as t:
   d,c,p=self.setup_capture(Path(t));old=copy.deepcopy((d.settings,d.props));d.conflicts[api.CONFLICT[0]]='1-3'
   with self.assertRaisesRegex(RuntimeError,'conflict'):c.configure()
   c.restore();self.assertFalse(c.touched);self.assertEqual((d.settings,d.props),old)
 def test_absent_marker_no_retry_and_restore(self):
  with tempfile.TemporaryDirectory()as t:
   d,c,p=self.setup_capture(Path(t));c.configure();d.current='12345';d.closed=False
   with patch.object(api.time,'monotonic',side_effect=[0,0,100]),patch.object(api.time,'sleep'),self.assertRaisesRegex(RuntimeError,'marker absent'):c.trigger('12345')
   c.restore();self.assertFalse(c.r['capture_completed']);self.assertEqual(d.current,'12345');self.assertTrue(c.r['settings_restored'])
 def test_replacement_pid_not_adopted_or_stopped(self):
  with tempfile.TemporaryDirectory()as t:
   d,c,p=self.setup_capture(Path(t));c.configure();d.current='999'
   with self.assertRaisesRegex(RuntimeError,'owned PID'):c.trigger('12345')
   c.restore();self.assertEqual(d.current,'999');self.assertFalse(any('force-stop'in x for x in d.events))
 def test_restore_error_does_not_replace_primary_failure(self):
  with tempfile.TemporaryDirectory()as t:
   d,c,p=self.setup_capture(Path(t));c.configure();d.failure='debug.gfxrecon.capture_file'
   with self.assertRaisesRegex(RuntimeError,'primary launch failure'):
    try:raise RuntimeError('primary launch failure')
    finally:c.restore()
   self.assertTrue(c.r['cleanup_errors'])
 def test_explicit_policy_and_mutual_exclusion(self):
  with tempfile.TemporaryDirectory()as t:
   d,c,p=self.setup_capture(Path(t))
   for key,value in [('performance_tools',True),('official_cpu_aa',True),('counter_snapshot',True),('sustained_play',True)]:
    q=copy.deepcopy(p);q[key]=value
    with self.assertRaisesRegex(RuntimeError,'separate'):api.validate(q,lambda r:Path(r['path']))
   q=copy.deepcopy(p);q['official_api_frame']['frames']=2
   with self.assertRaisesRegex(RuntimeError,'one bounded'):api.validate(q,lambda r:Path(r['path']))
 def test_original_finally_and_helpers_preserved(self):
  previous=subprocess.check_output(['git','-C',str(D.parents[2]),'show','df68ea7d2a01341de9b3247324af9dafbd4839f6:scripts/dev/native-bulk-game-v1/run-game.py'],text=True)
  old=ast.parse(previous);new=ast.parse((D/'run-game.py').read_text());f=lambda t,n:next(x for x in t.body if isinstance(x,ast.FunctionDef)and x.name==n)
  # Only nest the old phase finally to guarantee API settings restoration;
  # all original save2/PID/screenshot/stop statements are byte-semantic equal.
  o=next(x for x in f(old,'phase_game').body if isinstance(x,ast.Try));n=next(x for x in f(new,'phase_game').body if isinstance(x,ast.Try));self.assertEqual(ast.dump(ast.Module(o.finalbody,[])),ast.dump(ast.Module(n.finalbody[0].body,[])))
  for name in ['owned','provider_gate','native_table_gate','read_state','expected_baseline']:self.assertEqual(ast.dump(f(old,name)),ast.dump(f(new,name)))
  om=next(x for x in f(old,'main').body if isinstance(x,ast.Try));nm=next(x for x in f(new,'main').body if isinstance(x,ast.Try));self.assertEqual(ast.dump(ast.Module(om.finalbody,[])),ast.dump(ast.Module(nm.finalbody,[])))
if __name__=='__main__':unittest.main()
