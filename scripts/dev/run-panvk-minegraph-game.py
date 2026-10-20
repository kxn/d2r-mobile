#!/usr/bin/env python3
"""One reversible driver-only original022 real-game start. Offline by default.
Uses existing directory CAS, ordinary first-PID ownership and original save2 helper.
No APK/module/resource changes, old capture, custom measurement or GPU-import mode.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,shlex,signal,subprocess,sys,time,uuid,zipfile
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport'
OLD=R/'.local/worktrees/panvk-replay-rebind-alloc-v2/scripts/dev/run-panvk-layered-offscreen-game-diagnostic.py'
spec=importlib.util.spec_from_file_location('existing_original_game_transaction',OLD);old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
require=old.require;sha=old.sha;read=old.read;journal=old.journal;inventory=old.inventory;rename_dir=old.rename_dir
READY=R/'.local/logs/panvk-minegraph-703db0e-full-driver-source-ready-agent.json'
READY_SHA='defcf3634fe46c0733ab40319251082d0a8eca5b28eb4120b70aca5c0921e1d2'
DRIVER_SHA='6acfacb8ffe69081f84ee95a8964144760a394d9ebd694f0c082e65677d643c9'
SOURCE=R/'.local/worktrees/readback-fixture-class-restore-v1'
APK='a0bc159c20acc85fe2178a73d3e9f889d23e9422c0bb883982136cafa0f7f4ff';ELF='38d4b04de9eafdc5ec3495a4af2ee4391579b534ceb094b77355d01ab76ed4fc'
LOCK='files/runtime/.astc6-full-controller-lock'
CHAIN='cbd8d2da0b0f';OWNER='.minegraph-game-owner';DIRS={'driver':'files/runtime/vulkan-panfrost','temp':'files/runtime/temp','cache':'cache'}
LAYER_KEYS=['enable_gpu_debug_layers','gpu_debug_app','gpu_debug_layers','gpu_debug_layers_gles','gpu_debug_layer_app']
# Same existing inventory/CAS checks; preserve real Diablo II paths containing spaces.
def inventory(dev,path):
 require(dev.app('sh','-c','if test -L "$1"; then exit 5; fi; if test -d "$1"; then echo DIR; elif test -e "$1"; then exit 6; fi','sh',path) in ('','DIR'),'unknown directory kind')
 kind=dev.app('sh','-c','if test -d "$1"; then echo DIR; fi','sh',path)
 if not kind:return None
 require(not dev.app('find',path,'-type','l'),'directory contains symlink')
 files=dev.app('find',path,'-type','f').splitlines();require(all(n.startswith(path+'/') and '..'not in Path(n).parts for n in files),'unsafe directory inventory')
 if not files:return {}
 lines=[]
 # Keep each ADB shell/run-as request bounded: an existing driver cache can
 # contain hundreds of paths and exceed the shell service command limit.
 for start in range(0,len(files),128):lines.extend(dev.app('sha256sum',*files[start:start+128]).splitlines())
 result={n.removeprefix(path+'/'):h for h,n in (line.split(None,1)for line in lines)}
 require(len(result)==len(files) and all(re.fullmatch('[a-f0-9]{64}',h)for h in result.values()),'incomplete directory hashes');return result

def record(p):p=Path(p).resolve();return {'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size}
def proof():
 require(sha(READY)==READY_SHA,'driver ready changed');d=read(READY)
 require(d['upstream_commit']=='703db0ebb9afb2c97ae31f24dba5981c7c765fc1' and d['target']['parallel_jobs']==2,'wrong source/build')
 for key in ['source_identity','build_only_adaptation','generated_version','host_codegen','ELF','driver']:
  r=d[key];require(sha(r['path'])==r['sha256'],'driver provenance changed: '+key)
 require(d['driver']['sha256']==DRIVER_SHA and set(d['ICD_exports_verified'])=={'HMI','vk_icdGetInstanceProcAddr','vk_icdGetPhysicalDeviceProcAddr','vk_icdNegotiateLoaderICDInterfaceVersion'},'wrong ICD')
 for r in d['actual_compiler_dependencies'].values():
  require(sha(r['path'])==r['sha256'],'dependency report changed');q=read(r['path']);require(not q['missing'],'missing build dependencies')
  for f in q['files']:require(sha(f['path'])==f['sha256'],'compiler dependency changed: '+f['path'])
 for row in read(d['source_identity']['path'])['files']:
  p=Path(d['source_root'])/row['path'];a=row['actual']
  require((p.is_symlink() and str(p.readlink())==a['target']) if a['kind']=='symlink' else (p.is_file() and not p.is_symlink() and sha(p)==a['sha256']),'driver source changed: '+row['path'])
 p=read(old.PROMOTION);require(p['production_promoted_and_stopped'] and p['source_revision']=='022d6254d9de565852fa22394b6403491db86707' and not p['rollback_errors'],'production promotion mismatch')
 require(p['canonical_APK_SHA256']==APK and p['host_ELF_SHA256']==ELF and len(p['promoted_module_and_resource_hashes'])==35,'original APK/35 set')
 ap=R/'.local/build/image-binding-022d625-canonical-source/canonical.apk';require(sha(ap)==APK,'canonical APK changed')
 with zipfile.ZipFile(ap) as z:require(hashlib.sha256(z.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()==ELF,'canonical ELF changed')
 return d,p

def command(serial,out):
 return [sys.executable,str(SOURCE/'scripts/dev/android-game-launch.py'),'--serial',serial,'--vulkan-provider','panvk','--no-shared-guest-heap','--no-legacy-astc-copy','--external-profile','--fixed-3d','--scale-3d','0.6','--game-res','2772x1240','--timeout','75','--report',str(out/'launch.json')]

def layers(dev):return {k:dev.adb('shell','settings','get','global',k)for k in LAYER_KEYS}
def no_layers(v):return v['enable_gpu_debug_layers']in('null','0') and all(v[k]in('null','')for k in LAYER_KEYS[1:])
def baseline(dev,expected):
 dev.stopped();path=dev.adb('shell','pm','path',PKG).removeprefix('package:');require(path.startswith('/')and'\n'not in path,'unknown split APK')
 require(dev.adb('shell','sha256sum',path).split()[0]==APK,'installed APK differs')
 raw=dev.app('sha256sum',*expected);got={n:h for h,n in(line.split()for line in raw.splitlines())};require(got==expected,'original35 changed')
 require(dev.app('cat','files/runtime/shaders/.chain')==CHAIN,'shader chain differs')
 return {'APK':APK,'ELF':ELF,'files35':got,'shader_chain':CHAIN,'shaders':inventory(dev,'files/runtime/shaders')}
def owned(values,d,label,partial=False):
 require(isinstance(values,dict)and values.get(OWNER)==d['owner_sha256'],'candidate ownership absent: '+label)
 if label=='driver':require((values.get('libvulkan_panfrost.so')in(None,DRIVER_SHA) if partial else values.get('libvulkan_panfrost.so')==DRIVER_SHA) and all(k in (OWNER,'libvulkan_panfrost.so')or k.startswith('shader-cache/')for k in values),'candidate driver differs')
 return True

def launch_once(dev,path,d,out):
 own=None;client=None;ambiguous=False
 def current():return dev.adb('shell','pidof '+PKG+' || true')
 def observe():
  nonlocal own,ambiguous
  now=current()
  if now:
   try:old.claim_pid(d,now,own)
   except RuntimeError:ambiguous=True;raise
   if own is None:
    own=now;d['first_PID']=int(own);uid=dev.app('id','-u');status=dev.app('cat','/proc/'+own+'/status');(out/'first-status.txt').write_text(status)
    u=re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',status,re.M)
    require(uid.isdecimal()and 10000<=int(uid)<200000 and u and all(v==uid for v in u.groups()),'ordinary UID mismatch');d['ordinary_UID']=int(uid);journal(path,d)
  return now
 try:
  d['launch_command']=command(d['serial'],out);d['launch_attempted']=True;journal(path,d)
  with(out/'launch-console.txt').open('w')as log:
   client=subprocess.Popen(d['launch_command'],stdout=log,stderr=subprocess.STDOUT,cwd=R);deadline=time.monotonic()+100
   while client.poll()is None:
    now=observe()
    native=dev.app('sh','-c','if test -f files/runtime/native.log; then cat files/runtime/native.log; fi')
    if own and 'Android native start pid='+own+'\n'in native:
     (out/'native.log').write_text(native)
     if 'FATAL'in native or 'Vulkan PanVK ICD load:'in native:d['native_failure_seen']=True;break
    if own and not now:d['owned_process_exited_during_launch']=True;break
    require(time.monotonic()<deadline,'bounded original launcher deadline');time.sleep(.4)
   if client.poll()is None:
    client.terminate();client.wait(timeout=10)
  d['launcher_returncode']=client.returncode;journal(path,d)
  require(not d.get('native_failure_seen') and client.returncode==0,'original native startup failed, preserve diagnostic')
  require(own and observe()==own,'original game PID absent/replaced')
  native=dev.app('cat','files/runtime/native.log');(out/'native.log').write_text(native)
  raw=dev.app('cat','files/runtime/game-state.jsonl');(out/'game-state.jsonl').write_text(raw)
  state=json.loads(raw.splitlines()[-1]);(out/'HUD-state.json').write_text(json.dumps(state,indent=2)+'\n')
  # Preserve actual image before later assertions, even if a mode assertion fails.
  png=subprocess.check_output(dev.command+['exec-out','screencap','-p'],timeout=12)
  require(png[:8]==b'\x89PNG\r\n\x1a\n','screenshot not PNG');(out/'HUD.png').write_bytes(png);d['screenshot']=record(out/'HUD.png');journal(path,d)
  maps=dev.app('cat','/proc/'+own+'/maps');(out/'maps.txt').write_text(maps)
  require('libvulkan_panfrost.so'in maps and not any(s in maps.lower()for s in('renderdoc','libvklayer_khronos_validation','liblwi','libvklayer_sdp')),'wrong/layered maps')
  d['libhardware_maps']=[v for v in maps.splitlines()if 'libhardware.so'in v]
  require(state['valid']and state['world_input_ready']and state['scale_3d']==.6,'HUD or .60 mode mismatch')
  require(state['game_output']==state['surface_output']=={'width':2772,'height':1240}and state['guest_heap']['shared']is False,'output/shared mode mismatch')
  require('[vulkan loader] provider=panvk direct ICD='in native and 'driverID=20'in native and 'FATAL'not in native,'provider/driver identity mismatch')
  require(read(out/'launch.json')['actual_vulkan_provider']=='panvk' and read(out/'launch.json')['requested_legacy_astc_copy']is False,'legacy/resource mode mismatch')
  d['actual_original_game_HUD_reached']=True;d['real_game_modules_executed']=True;d['namespace_load_succeeded']=True;d['visual_acceptance_pending']=True;journal(path,d)
 finally:
  if client and client.poll()is None:client.terminate();client.wait(timeout=10)
  for remote,local in [('files/runtime/native.log','native.log'),('files/runtime/game-state.jsonl','game-state.jsonl')]:
   try:(out/local).write_text(dev.app('sh','-c','if test -f "$1"; then cat "$1"; fi','sh',remote))
   except Exception as e:d.setdefault('log_errors',[]).append(repr(e))
  try:(out/'crash-logcat.txt').write_text(dev.adb('logcat','-b','crash','-d','-v','threadtime','-t','200'))
  except Exception as e:d.setdefault('log_errors',[]).append(repr(e))
  now=current()
  if d.get('native_failure_seen')or ambiguous:
   deadline=time.monotonic()+30
   while now and time.monotonic()<deadline:time.sleep(.3);now=current()
  if now:
   require(not ambiguous and own and now==own,'unowned/replacement PID: preserve transaction')
   cmd=[sys.executable,str(R/'scripts/dev/android-game-background-save.py'),'--serial',d['serial'],'--report',str(out/'fresh-save.json')]
   with(out/'fresh-save.txt').open('w')as f:subprocess.run(cmd,check=True,stdout=f,stderr=subprocess.STDOUT,timeout=45)
   f=read(out/'fresh-save.json');require(f['fresh_original_lifecycle_save']and f['save_commits']==2 and f['pid']==int(own),'no fresh original save2')
   require(current()==own,'PID changed during save');dev.adb('shell','am','force-stop',PKG);dev.stopped();d['fresh_save2_then_stopped']=True
  else:d['already_exited_no_save_restart']=True
  native=(out/'native.log').read_text()if(out/'native.log').exists()else''
  d['namespace_load_blocked']=('Vulkan PanVK ICD load:'in native);d['libhardware_load_error']=('libhardware.so'in native and d['namespace_load_blocked']);journal(path,d)

def restore(dev,path,d):
 dev.stopped()
 for label,target in reversed(list(DIRS.items())):
  before=d['old_'+label];backup=d['stage']+'/backup/'+label;stored=inventory(dev,backup);current=inventory(dev,target)
  if stored is None and before is not None:require(current==before,'original backup missing/target changed');continue
  if stored is not None:require(stored==before,'original backup changed')
  if current is not None:
   owned(current,d,label);rename_dir(dev,target,d['stage']+'/after-'+label,current)
  if before is not None:rename_dir(dev,backup,target,before)
  require(inventory(dev,target)==before,'directory restore mismatch: '+label)
 d['original_directories_restored']=True;journal(path,d)

def clean(dev,path,d):
 dev.stopped();require(d['original_directories_restored'],'restore required')
 stage=d['stage'];require(dev.app('cat',stage+'/'+OWNER)==d['token'],'stage ownership changed')
 require(not dev.app('find',stage,'-type','l'),'stage contains symlink')
 for label in DIRS:
  for prefix in ('incoming/','after-'):
   p=stage+'/'+prefix+label;v=inventory(dev,p)
   if v is not None:owned(v,d,label,partial=prefix=='incoming/')
 files=dev.app('find',stage,'-type','f').splitlines()
 require(all(n==stage+'/'+OWNER or n.startswith(tuple(stage+'/'+p+label+'/'for label in DIRS for p in('incoming/','after-')))for n in files),'unknown staging file')
 for n in files:
  h=dev.digest(n);dev.app('sh','-c','set -eu; test -z "$(pidof "$3" || true)"; test "$(sha256sum "$1" | cut -d" " -f1)" = "$2"; rm "$1"','sh',n,h,PKG)
 for n in sorted(dev.app('find',stage,'-type','d').splitlines(),key=lambda n:(n.count('/'),n),reverse=True):dev.app('rmdir',n)
 if d.get('transfer_pushed'):
  require(dev.adb('shell','sha256sum',d['transfer']).split()[0]==DRIVER_SHA,'transfer changed');dev.adb('shell','rm',d['transfer'])
 require(dev.app('cat',LOCK+'/owner')==d['token'],'controller lock owner changed');dev.app('rm',LOCK+'/owner');dev.app('rmdir',LOCK)
 d['owned_staging_and_transfer_cleaned']=True;journal(path,d)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',default='10.10.10.44:34455');p.add_argument('--output',type=Path,required=True);p.add_argument('--execute',action='store_true');a=p.parse_args();driver,promotion=proof()
 plan={'scope':'ONE original022/fullASTC4 driver-only real-game startup; fresh allocation with full703db driver; no oldcapture/APK/module/resource mutation or FPS claim','driver':driver['driver'],'driver_ready_sha256':READY_SHA,'serial':a.serial,'fixed_mode':{'scale_3d':.6,'output':[2772,1240],'shared':False,'legacy_ASTC_copy':False},'launch':command(a.serial,a.output),'directories_CAS_isolated':DIRS,'original_APK':APK,'original_ELF':ELF,'tools':{str(x):sha(x)for x in [Path(__file__),OLD,old.COMMON,SOURCE/'scripts/dev/android-game-launch.py',R/'scripts/dev/android-game-background-save.py']}}
 if not a.execute:print(json.dumps(plan,indent=2));return
 a.output.mkdir(parents=True,exist_ok=False);path=a.output/'result.json';token=uuid.uuid4().hex;stage='files/runtime/.minegraph-game-'+token;transfer='/data/local/tmp/minegraph-driver-'+token+'.so'
 d={**plan,'token':token,'stage':stage,'transfer':transfer,'owner_sha256':hashlib.sha256((token+'\n').encode()).hexdigest(),'cleanup_errors':[],'phase':'preflight'};journal(path,d);dev=old.common.Device(a.serial);expected=promotion['promoted_module_and_resource_hashes'];claimed=False
 def interrupt(sig,frame):raise RuntimeError('interrupted; owned save/stop+restore only')
 for sig in(signal.SIGINT,signal.SIGTERM):signal.signal(sig,interrupt)
 try:
  d['baseline_before']=baseline(dev,expected);d['settings_before']=layers(dev);require(no_layers(d['settings_before']),'external GPU layer active')
  dev.stopped()
  for label,target in DIRS.items():d['old_'+label]=inventory(dev,target)
  journal(path,d);require(inventory(dev,stage)is None,'transaction exists');dev.app('mkdir',LOCK);d['lock_claimed']=True;dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',LOCK+'/owner',token);dev.app('mkdir',stage);claimed=True;dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',stage+'/'+OWNER,token);dev.app('mkdir',stage+'/incoming',stage+'/backup')
  for label in DIRS:
   pth=stage+'/incoming/'+label;dev.app('mkdir',pth);dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',pth+'/'+OWNER,token)
  dev.adb('push',driver['driver']['path'],transfer);d['transfer_pushed']=True;journal(path,d);require(dev.adb('shell','sha256sum',transfer).split()[0]==DRIVER_SHA,'pushed driver corrupt')
  dev.app('cp',transfer,stage+'/incoming/driver/libvulkan_panfrost.so')
  for label,target in DIRS.items():
   d['intent']='backup-'+label;journal(path,d)
   if d['old_'+label]is not None:rename_dir(dev,target,stage+'/backup/'+label,d['old_'+label])
   v=inventory(dev,stage+'/incoming/'+label);owned(v,d,label);d['intent']='publish-'+label;journal(path,d);rename_dir(dev,stage+'/incoming/'+label,target,v)
  d['phase']='published';journal(path,d);launch_once(dev,path,d,a.output);d['phase']='actual-original-HUD-pending-visual-review'
 except BaseException as e:d['phase']='failed-preserved';d['error']=repr(e)
 finally:
  if claimed:
   try:restore(dev,path,d);clean(dev,path,d)
   except BaseException as e:d['cleanup_errors'].append(repr(e))
  elif d.get('lock_claimed'):
   try:
    require(dev.app('cat',LOCK+'/owner')==token,'controller lock owner changed');dev.app('rm',LOCK+'/owner');dev.app('rmdir',LOCK)
   except BaseException as e:d['cleanup_errors'].append(repr(e))
  try:
   d['baseline_after']=baseline(dev,expected);require(d['baseline_after']==d['baseline_before'],'original APK/modules/resources/shaders changed');require(layers(dev)==d['settings_before'],'GPU layer settings changed');dev.stopped();d['original_APK_modules_resources_shaders_preserved']=True;d['game_stopped']=True
  except BaseException as e:d['cleanup_errors'].append(repr(e))
  journal(path,d)
 print(json.dumps({k:v for k,v in d.items()if k not in('baseline_before','baseline_after')},indent=2))
 if d['phase']=='failed-preserved'or d['cleanup_errors']:raise SystemExit(1)
if __name__=='__main__':main()
