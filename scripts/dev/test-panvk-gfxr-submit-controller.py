#!/usr/bin/env python3
"""Offline transaction failure checks; no ADB/GPU/game execution."""
from pathlib import Path
import importlib.util,tempfile,shlex
P=Path(__file__).with_name('run-panvk-offscreen-gfxr-submit.py');spec=importlib.util.spec_from_file_location('capture_control',P);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
checks=0
class Fake:
 def __init__(self,fail=False):self.settings={};self.props={};self.fail=fail;self.changed=0
 def stopped(self):pass
 def adb(self,*a):
  global checks
  if a[:3]==('shell','pm','path'):return 'package:/official.apk'
  if a[:2]==('shell','sha256sum'):return m.GFXR_APK_SHA+'  /official.apk'
  if a[:3]==('shell','settings','get'):return self.settings.get(a[4],'null')
  if a[:3]==('shell','settings','put'):self.settings[a[4]]=a[5];return ''
  if a[:3]==('shell','settings','delete'):self.settings.pop(a[4],None);return ''
  if a[:2]==('shell','getprop'):return self.props.get(a[2],'')
  if a[0]=='shell'and a[1].startswith('setprop '):
   k,v=shlex.split(a[1])[1:];self.changed+=1
   if self.fail and self.changed==3:self.fail=False;raise RuntimeError('injected partial property update')
   self.props[k]=v;return ''
  raise AssertionError(a)
 def app(self,*a):
  if a[0]=='find':return m.DRIVER_DIR+'/shader-cache/panvk-submit-'+('a'*32)+'.gfxr'
  if a[0]=='cat':return self.maps
  raise AssertionError(a)
def check(v):
 global checks
 assert v;checks+=1
with tempfile.TemporaryDirectory()as n:
 p=Path(n)/'journal.json';d={'token':'a'*32};dev=Fake();m.capture_settings(dev,p,d);check(d['capture_settings_applied']);check(dev.props['debug.gfxrecon.capture_queue_submits']=='150-174');check(len(dev.props['debug.gfxrecon.capture_file'].encode())<92);m.restore_capture_settings(dev,p,d);check(d['capture_settings_restored']);check(not dev.settings and all(v==''for v in dev.props.values()))
 dev=Fake(True);d={'token':'a'*32}
 try:m.capture_settings(dev,p,d)
 except RuntimeError:pass
 else:raise AssertionError('partial setup not injected')
 m.restore_capture_settings(dev,p,d);check(not dev.settings and all(v==''for v in dev.props.values()))
 dev=Fake();d={'token':'a'*32};m.capture_settings(dev,p,d);dev.props['debug.gfxrecon.capture_file']='unowned-change'
 try:m.restore_capture_settings(dev,p,d)
 except RuntimeError:check(dev.props['debug.gfxrecon.capture_file']=='unowned-change')
 else:raise AssertionError('overwrote unowned tool setting')
 dev=Fake();d={'token':'a'*32};m.capture_settings(dev,p,d);d['capture_properties_requested']['unowned.property']='x'
 try:m.restore_capture_settings(dev,p,d)
 except RuntimeError:checks+=1
 else:raise AssertionError('corrupted recovery options accepted')
 dev=Fake();d={'token':'a'*32,'trace_stem':'panvk-submit-'+('a'*32)}
 try:m.collect_trace_stopped(dev,p,d,Path(n))
 except RuntimeError as e:check('not actually selected'in str(e))
 else:raise AssertionError('untrimmed capture accepted')
 dev=Fake();dev.maps='libVkLayer_gfxreconstruct.so libVkLayer_khronos_validation.so libvulkan_panfrost.so';d={};m.capture_stack(dev,p,d,Path(n),'123');check(d['capture_stack_mapped'])
 dev.maps='libVkLayer_khronos_validation.so libvulkan_panfrost.so'
 try:m.capture_stack(dev,p,{},Path(n),'123')
 except RuntimeError:checks+=1
 else:raise AssertionError('missing real capture layer accepted')
 check(m.claim_pid({},'123','123')=='123')
 for raw in ['123 456','456']:
  d={}
  try:m.claim_pid(d,raw,'123')
  except RuntimeError:check(d['ownership_failed'])
  else:raise AssertionError('ambiguous/replacement PID accepted')
print('offline capture transaction failure checks='+str(checks)+' passed; no ADB/GPU/game execution')
