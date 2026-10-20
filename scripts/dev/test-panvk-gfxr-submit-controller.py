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
  if a[0]=='find':return m.DRIVER_DIR+'/shader-cache/panvk-submit-'+('a'*32)+'_queue_submits_150_through_174.gfxr'
  if a[0]=='cat':return self.maps
  raise AssertionError(a)
def check(v):
 global checks
 assert v;checks+=1
with tempfile.TemporaryDirectory()as n:
 p=Path(n)/'journal.json';d={'token':'a'*32};dev=Fake();m.capture_settings(dev,p,d);check(d['capture_settings_applied']);check(dev.props['debug.gfxrecon.capture_queue_submits']=='' and dev.props['debug.gfxrecon.capture_trigger_boundary']=='');check(len(dev.props['debug.gfxrecon.capture_file'].encode())<92);m.restore_capture_settings(dev,p,d);check(d['capture_settings_restored']);check(not dev.settings and all(v==''for v in dev.props.values()))
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
 else:raise AssertionError('nonplain capture accepted')
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
# Actual boundedstorage/tracehelper behavior, not a new capture implementation.
class Space:
 def adb(self,*args):return 'Filesystem 1K-blocks Used Available Use% Mounted\n/data 50000000 1 '+str((m.MIN_FREE+4096)//1024)+' 1% /data'
 def app(self,*args):return str(self.size)
space=Space();space.size=0;old_usage=m.shutil.disk_usage
m.shutil.disk_usage=lambda path:type('Disk',(),{'free':m.MIN_FREE+4096})()
check(m.check_storage(space,Path('/unused'))['device_free_bytes']>=m.MIN_FREE)
m.shutil.disk_usage=lambda path:type('Disk',(),{'free':m.MIN_FREE-1})()
try:m.check_storage(space,Path('/unused'))
except RuntimeError:checks+=1
else:raise AssertionError('lowfree accepted')
m.shutil.disk_usage=old_usage
d={'trace_stem':'g'};m.capture_bound(space,d);check(d['last_trace_bytes']==0)
space.size=m.TRACE_STOP_BYTES
try:m.capture_bound(space,d)
except RuntimeError:check(d['capture_safety_limit']=='2GiB size stop trigger')
else:raise AssertionError('2GiB bound ignored')
# Shaderdirectory admission shares original journal, never accepts unowned
# arbitrarypaths. Entireoldcache may be renamed back without relying on newdata.
import hashlib,subprocess
H=lambda data:hashlib.sha256(data).hexdigest();old_SO=m.SO;original_snapshot=m.snapshot
class Files:
 def __init__(self,root):self.root=root;self.running=False
 def stopped(self):assert not self.running
 def identity(self,d):self.stopped()
 def app(self,*args):return subprocess.check_output(list(args),cwd=self.root,text=True).strip()
 def digest(self,n):
  p=self.root/n
  return H(p.read_bytes())if p.is_file()else None
with tempfile.TemporaryDirectory()as n:
 root=Path(n);dev=Files(root);stage='files/runtime/.original-fork-'+('a'*32);d={'token':'a'*32,'serial':'fake','stage':stage,'incoming':stage+'/incoming','backup':stage+'/backup','transfer':str(root/'unused-transfer'),'owner_sha256':H(('a'*32+'\n').encode()),'baseline35':{},'baseline':{},'candidate':{},'old_shaders':{}}
 for label,target in [('overlay',m.OVERLAY),('driver',m.DRIVER_DIR),('shaders',m.SHADERS)]:
  p=root/target;p.mkdir(parents=True);files={'old.txt':b'original'}if label=='overlay'else({'libvulkan_panfrost.so':b'old-driver','shader-cache/pso':b'old-cache'}if label=='driver'else{'.chain':b'original-chain','0123456789abcdef.spv':b'original-shader'})
  for leaf,data in files.items():q=p/leaf;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(data)
  d['old_'+label]=m.inventory(dev,target)
  inc=root/(d['incoming']+'/'+label);inc.mkdir(parents=True)
  nf={'new.txt':b'new-overlay'}if label=='overlay'else({'libvulkan_panfrost.so':b'new-driver',m.OWNER:('a'*32+'\n').encode()}if label=='driver'else{**files,m.OWNER:('a'*32+'\n').encode()})
  for leaf,data in nf.items():q=inc/leaf;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(data)
  if label=='overlay':d['candidate']=m.inventory(dev,d['incoming']+'/'+label)
  if label=='driver':m.SO=H(b'new-driver')
 (root/d['backup']).mkdir();journal=root/'journal.json';m.snapshot=lambda dev,d:d['baseline35']
 m.publish(dev,journal,d);check((root/m.SHADERS/'0123456789abcdef.spv').read_bytes()==b'original-shader')
 (root/m.SHADERS/'0123456789abcdef.spv').write_bytes(b'gamechanged');(root/m.SHADERS/'fedcba9876543210.spv').write_bytes(b'gamenew')
 check(m.valid_new('shaders',m.inventory(dev,m.SHADERS),d))
 bad=dict(m.inventory(dev,m.SHADERS));bad['unowned-path']=H(b'evil');check(not m.valid_new('shaders',bad,d));bad.pop('unowned-path');bad[m.OWNER]=H(b'wrong');check(not m.valid_new('shaders',bad,d))
 m.restore_dirs(dev,journal,d);check(d['all_shaders_restored'] and m.inventory(dev,m.SHADERS)==d['old_shaders']);check(not(root/m.SHADERS/'fedcba9876543210.spv').exists())
 # Validate actual selectivecleanup shell removes only newlyowned shadercopy.
 # Original restoredtree remains bitexact, original OWNER never introduced.
 retired=stage+'/after-shaders';before=m.inventory(dev,m.SHADERS)
 cmd='set -eu; test -z "$(pidof "$3" || true)"; test "$(sha256sum "$1/"'+m.OWNER+' | cut -d" " -f1)" = "$2"; rm -r "$1"'
 (root/retired/m.OWNER).write_bytes(b'unowned')
 try:dev.app('sh','-c',cmd,'sh',retired,d['owner_sha256'],m.PKG)
 except subprocess.CalledProcessError:check((root/retired).exists()and m.inventory(dev,m.SHADERS)==before)
 else:raise AssertionError('wrongowner directory removed')
 (root/retired/m.OWNER).write_text('a'*32+'\n')
 dev.app('sh','-c',cmd,'sh',retired,d['owner_sha256'],m.PKG)
 check(not(root/retired).exists()and m.inventory(dev,m.SHADERS)==before)
m.SO=old_SO;m.snapshot=original_snapshot
print('offline capture transaction failure checks='+str(checks)+' passed; no ADB/GPU/game execution')
