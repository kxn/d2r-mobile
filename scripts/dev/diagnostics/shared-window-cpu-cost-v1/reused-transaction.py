#!/usr/bin/env python3
"""Original unpatched Turnip KGSL + existing original BC/UI4 game control. Default offline.
Reuse the frozen resource Device/Hashes/journal and original launcher/save.
No APK/modules install, system driver change, profiler or driver debug override.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, shlex, signal, subprocess, sys, tarfile, time, uuid, zipfile
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport'
COMMON=R/'.local/build/deploy-astc6-full6-36a9781-v1.py'
SOURCE=R/'.local/worktrees/shared-import-windows-v2'
DRIVER_READY=R/'.local/logs/turnip-original-q735-v1/source-ready-audit-agent.json'
RESOURCE_PROOF=R/'.local/logs/panvk-original-fork-ui-only-resource-proof-v2.json'
BASELINE=R/'.local/build/turnip-original-q735-v1-frozen/baseline-readonly.json'
DRIVER=R/'.local/build/turnip-host-import-v2/driver/libvulkan_freedreno.so'
UI=R/'.local/build/android-romfs-overlay'
OVERLAY='files/runtime/romfs_overlay';DRIVER_DIR='files/runtime/vulkan-driver'
DIRS=[('driver',DRIVER_DIR),('temp','files/runtime/temp'),('cache','cache')]
LOCK='files/runtime/.astc6-full-controller-lock';OWNER='.original-fork-baseline-owner'
APK='d7f9a720480b696af0ac70acb5773a2d14842a3e4840c030fdbb049b9fe83587'
ELF='6377227d545d878caaf9636209de98814335495e9e71f4aedc0bff61fb03d34a'
SO='3c8710beb5c33016479601603d41f15508ab185c7b18c684ebe3f8e0ecfa3e61';CHAIN='9507875007c6'
spec=importlib.util.spec_from_file_location('existing_resource_journal',COMMON);common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
require=common.require;journal=common.journal
sha=lambda p:common.Hashes().file(p)
read=lambda p:json.loads(Path(p).read_text())


def proof():
 require(sha(DRIVER_READY)=='b12192fcdb50bcf86c88cd82d9ab1e27b93a8bd98b40a86af6391b265dd53f05','driver sourceReady changed')
 require(sha(BASELINE)=='310ac564e7b3ce322cf68215682e64b8d702204e7f8a2b51742074cff5f89136','Q readonly baseline changed')
 require(sha(RESOURCE_PROOF)=='76438192aedb73d5457901f4284581d1db6ff1d9db3aafbc3e80bd429d2ab1e5','UI resource proof changed')
 dr=read(DRIVER_READY);b=read(BASELINE);ui=read(RESOURCE_PROOF)
 require(dr['original_driver_candidate']==sha(DRIVER)==SO and dr['original_driver_patches']==[] and dr['unmodified_source_files_verified']==12808,'not original unpatched Turnip')
 require(ui['changed_texture_or_cache_entries']==[] and ui['texture_and_desc_cache_CFT_entries_verified_identical']==31093 and ui['original_filetable_prefix_identical'],'BC resource proof absent')
 values={x['relative']:x['sha256']for x in ui['overlay_files']}
 require(set(values)=={'ctrl_touch','touch_strings.tsv','data/.build.config','data/00-00000007.data'},'exact UI4 required')
 require(values=={n.removeprefix(OVERLAY+'/'):h for n,h in b['inventories'][OVERLAY].items()},'Q resource mismatch; do not install overlay')
 require(b['serial']=='10CG570J59001U1' and b['model']=='V2528A' and b['game_PID_absent'] and b['APK_sha256']==APK and b['chain']==CHAIN,'Q identity')
 baseline={n:h for key in ('files/runtime/modules',OVERLAY,DRIVER_DIR)for n,h in b['inventories'][key].items()};require(len(baseline)==34,'29module+4UI+1driver')
 ap=Path(dr['original_APK_local']);require(sha(ap)==APK,'exact local Q APK')
 with zipfile.ZipFile(ap)as z:require(hashlib.sha256(z.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()==ELF,'Q ELF changed')
 modules={Path(n).name:{'sha256':h}for n,h in b['inventories']['files/runtime/modules'].items()if not n.endswith('/.d2r-deploy-managed')};require(len(modules)==28,'28 original module records')
 require(b['inventories']['files/runtime/modules']['files/runtime/modules/.d2r-deploy-managed']==hashlib.sha256(b'').hexdigest(),'unknown extra module')
 deps=[Path(__file__),COMMON,DRIVER_READY,BASELINE,RESOURCE_PROOF,DRIVER,SOURCE/'scripts/dev/android-game-launch.py',R/'scripts/dev/android-game-background-save.py']
 return {'apk':APK,'elf':ELF,'chain':CHAIN,'baseline_fixed':baseline,'baseline':values,'module_artifacts':modules,'candidate':values,'frozen_inputs':{str(p):sha(p)for p in deps},'driver':SO,'old_expected_directories':{label:{n.removeprefix(target+'/'):h for n,h in b['inventories'][target].items()}for label,target in DIRS},'shared_prefs_baseline_absent':b['inventories']['shared_prefs']is None,'cache_and_temp_are_driver_owned_output':True}


WINDOW_BYTES=0
COMPLETION_TRACE=False
def launch_command(serial,output):
 return [sys.executable,str(SOURCE/'scripts/dev/android-game-launch.py'),'--serial',serial,'--external-profile','--custom-vulkan-driver','--vulkan-provider','adreno',('--shared-guest-heap' if MODE else '--no-shared-guest-heap'),'--no-legacy-astc-copy','--no-astc-decode-unorm8','--no-defer-pool-publication','--scale-3d','0.75','--fixed-3d','--pool-copy-accounting','--game-res','2750x1260','--timeout','90','--report',str(output/'launch.json')]+['--pool-window-bytes',str(WINDOW_BYTES)]+(['--expect-shared-import-windows']if WINDOW_BYTES else[])

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


def valid_new(label,values,d,partial=False):
 if not isinstance(values,dict):return False
 if partial:
  expected={OWNER:d['owner_sha256']}
  if label=='driver':expected['libvulkan_freedreno.so']=SO
  return all(expected.get(n)==h for n,h in values.items())
 if values.get(OWNER)!=d['owner_sha256']:return False
 return label!='driver' or (values.get('libvulkan_freedreno.so')==SO and all(n in (OWNER,'libvulkan_freedreno.so')or n.startswith(('shader-cache/','mesa_shader_cache/','.cache/'))for n in values))

def rename_dir(dev,src,dst,expected):
 dev.stopped();require(inventory(dev,src)==expected and inventory(dev,dst)is None,'directory rename conflict')
 # Existing private-driver atomic directory publication: old files are never
 # copied or rewritten. A stopped PID is checked again inside run-as.
 dev.app('sh','-c','set -eu; test -z "$(pidof "$3" || true)"; test -d "$1"; test ! -e "$2"; mv "$1" "$2"','sh',src,dst,PKG)
 require(inventory(dev,dst)==expected,'directory rename verification failed')


def publish(dev,path,d):
 for label,target in DIRS:
  before=d['old_'+label];incoming=d['incoming']+'/'+label;backup=d['backup']+'/'+label
  d['intent']='backup-'+label;journal(path,d);rename_dir(dev,target,backup,before)
  d['intent']='publish-'+label;journal(path,d);new=inventory(dev,incoming);require(valid_new(label,new,d),'owned incoming changed')
  rename_dir(dev,incoming,target,new);d['intent']='published-'+label;journal(path,d)
 d['status']='deployed';journal(path,d)


def restore_dirs(dev,path,d):
 dev.identity(d)
 for label,target in reversed(DIRS):
  old=d['old_'+label];backup=d['backup']+'/'+label;current=inventory(dev,target);stored=inventory(dev,backup)
  if stored is None:
   require(current==old,'missing original backup/changed target: '+label);continue
  require(stored==old,'original backup changed: '+label)
  if current is not None:
   require(valid_new(label,current,d),'unowned published directory: '+label)
   retired=d['stage']+'/after-'+label;d['intent']='retire-'+label;journal(path,d);rename_dir(dev,target,retired,current)
  d['intent']='restore-'+label;journal(path,d);rename_dir(dev,backup,target,old)
 require(all(inventory(dev,target)==d['old_'+label]for label,target in DIRS),'driver/temp/cache mismatch')
 require(snapshot(dev,d)==d['baseline_fixed'],'original34 files not restored')
 require(inventory(dev,'shared_prefs')is None,'unexpected preference directory; preserve journal')
 d['status']='restored';d['all_fixed_restored']=True;journal(path,d)

def snapshot(dev,d):
 dev.identity(d);require(inventory(dev,OVERLAY)==d['baseline'],'baseline overlay differs/extra files')
 lines=dev.app('sha256sum',*d['baseline_fixed']).splitlines();got={n:h for h,n in (line.split()for line in lines)};require(got==d['baseline_fixed'],'34-file baseline changed');return got


def cleanup(dev,path,d):
 dev.stopped();require(d.get('all_fixed_restored'),'must restore before cleanup')
 allowed=[]
 for label,target in DIRS:
  for root in (d['incoming']+'/'+label,d['stage']+'/after-'+label):
   values=inventory(dev,root)
   if values is not None:require(valid_new(label,values,d,partial=root.startswith(d['incoming']+'/')),'owned staging changed')
   allowed.append(root+'/')
 files=dev.app('find',d['stage'],'-type','f').splitlines();require(all(n.startswith(tuple(allowed))for n in files),'unknown stage files')
 for start in range(0,len(files),64):
  batch=files[start:start+64]
  # Immutable original backups have already been restored. Each remaining
  # file is under this token-owned incoming/retired directory, PID stopped.
  expected={n:dev.digest(n)for n in batch};require(all(expected.values()),'cleanup leaf changed')
  args=[]
  for n,h in expected.items():args.extend([n,h])
  dev.app('sh','-c','set -eu; pkg="$1"; shift; test -z "$(pidof "$pkg" || true)"; while test "$#" -gt 0; do test "$(sha256sum "$1" | cut -d" " -f1)" = "$2"; shift 2; done','sh',PKG,*args)
  dev.app('rm','--',*batch)
 for n in sorted(dev.app('find',d['stage'],'-type','d').splitlines(),key=lambda s:(s.count('/'),s),reverse=True):dev.app('rmdir',n)
 transfer=dev.digest(d['transfer'])
 if transfer is not None:require(transfer==d.get('tar_sha256'),'owned transfer changed');dev.adb('shell','rm',d['transfer'])
 local=Path(d['local_tar']);require(local.parent==path.parent and local.name=='owned-'+d['token']+'.tar','unsafe local transfer')
 if local.exists():require(sha(local)==d['tar_sha256'],'local transfer changed');local.unlink()
 require(dev.app('cat',LOCK+'/owner')==d['token'],'resource lock changed');dev.app('rm',LOCK+'/owner');dev.app('rmdir',LOCK)
 d['cleanup_complete']=True;journal(path,d)

def claim_pid(d,current,own):
 if not current:return own
 if not current.isdecimal() or (own is not None and current!=own):
  d['ownership_failed']=True
  raise RuntimeError('unowned/multiple/replacement game PID')
 return current

def wait_game(dev,path,d,output):
 command=launch_command(d['serial'],output);d['launch_command']=command;d['launch_attempted']=True;journal(path,d)
 own=None;ambiguous=False;launcher=None
 def pid():return dev.adb('shell','pidof '+PKG+' || true')
 def observe():
  nonlocal own,ambiguous
  current=pid();d.setdefault('PID_raw',[]).append(current);journal(path,d)
  if current:
   try:claimed=claim_pid(d,current,own)
   except RuntimeError:ambiguous=True;journal(path,d);raise
   if own is None:
    own=claimed;d['first_PID']=int(own)
    uid=dev.app('id','-u');require(uid.isdecimal() and 10000<=int(uid)<200000,'not ordinary app UID')
    status=dev.app('cat','/proc/'+own+'/status');(output/'owned-status.txt').write_text(status)
    actual=re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',status,re.M);tgid=re.search(r'^Tgid:\s+(\d+)',status,re.M)
    require(actual and tgid and tgid.group(1)==own and all(v==uid for v in actual.groups()),'ordinary PID UID proof failed');d['ordinary_UID']=int(uid);journal(path,d)
  return current
 try:
  with(output/'launch.log').open('w')as f:
   launcher=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT,cwd=R);deadline=time.monotonic()+120
   while launcher.poll()is None:
    observe();require(time.monotonic()<deadline,'one launcher deadline');time.sleep(.3)
   observe();require(launcher.returncode==0,'original HUD launcher failed')
  result=read(output/'launch.json');require(own is not None and result['pid']==int(own),'launcher PID mismatch')
  require(result['requested_shared_guest_heap'] is MODE and result['actual_guest_heap']['shared'] is MODE and result['actual_vulkan_provider']=='adreno' and result['requested_legacy_astc_copy'] is False,'fixed control mode mismatch')
  require(result['actual_defer_pool_publication'] is False and result['requested_astc_decode_unorm8'] is False,'unexpected publication/decode override')
  with(output/'official-capture-console.log').open('w')as caplog:subprocess.run([sys.executable,str(Path(__file__).parent/'capture-official-cpu.py'),d['serial'],own,'1'if MODE else'0',str(output),str(WINDOW_BYTES),'1'if COMPLETION_TRACE else'0'],check=True,stdout=caplog,stderr=subprocess.STDOUT,timeout=245)
  native=dev.app('cat','files/runtime/native.log');(output/'native.log').write_text(native)
  require('[guest]'in native and '[vulkan loader] custom='in native and '[vulkan] device=Turnip Adreno (TM) 735 'in native and 'FATAL'not in native,'native game/provider failure')
  require('[shared pool] startup probe mapping=memfd-pair-guest-RO' in native and 'backing=shared-memfd' in native and '[shared pool] immutable startup shared;' in native,'actual memfd guestRO shared required; fallback cannot pass')
  require('import_window_bytes='+str(WINDOW_BYTES)+' no online mirror fallback' in native,'actual shared window mode mismatch')
  if WINDOW_BYTES:require('cold overlapping-import bidirectional GPU byte gate: passed'in native,'missing actual overlapping-import GPU proof')
  require('[ASTC resource copy]'not in native,'unexpected ASTC adaptation in original BC control')
  current=read_json_line(dev.app('tail','-n','1','files/runtime/game-state.jsonl'))
  require(current.get('guest_heap',{}).get('shared') is MODE,'actual shared heap differs');require(current.get('pool_mirrors',{}).get('backed_bytes',0)>0,'actual backing bytes missing');require(not MODE or ('[shared pool] immutable startup shared;'in native and current.get('pool_copy_accounting',{}).get('initial_shadow_bytes',-1)==0 and current.get('pool_copy_accounting',{}).get('initial_mirror_bytes',-1)==0),'shared startup/no-shadow copy evidence missing')
  require(current['valid'] and current['world_input_ready']and current['area']==1,'original camp not ready')
  require(current.get('pool_windows',{}).get('window_bytes')==WINDOW_BYTES,'published active window differs')
  (output/'camp-state.json').write_text(json.dumps(current,indent=2)+'\n');d['original_HUD_reached']=True;d['game_modules_executed']=True;journal(path,d)
  (output/'game-state.jsonl').write_text(dev.app('cat','files/runtime/game-state.jsonl'))
  remote=d['remote_screenshot'];dev.adb('shell','test','!','-e',remote)
  dev.adb('shell','screencap','-p',remote)
  digest=dev.adb('shell','sha256sum',remote).split()[0];require(re.fullmatch('[a-f0-9]{64}',digest),'screenshot hash absent')
  d['screenshot_device_written']=True;d['screenshot_sha256']=digest;journal(path,d)
 finally:
  if launcher is not None and launcher.poll()is None:
   launcher.terminate()
   try:launcher.wait(timeout=15)
   except subprocess.TimeoutExpired:raise RuntimeError('owned launcher still active; preserve transaction')
  current=pid()
  try:
   (output/'native.log').write_text(dev.app('cat','files/runtime/native.log'))
   (output/'crash-logcat.txt').write_text(dev.adb('logcat','-b','crash','-d','-v','threadtime'))
  except Exception as e:d['log_collection_error']=repr(e)
  if current:
   require(not ambiguous and own is not None and current==own and current.isdecimal(),'unknown/replacement PID: no save/stop/restore')
   savecommand=[sys.executable,str(R/'scripts/dev/android-game-background-save.py'),'--serial',d['serial'],'--report',str(output/'fresh-save.json')]
   with(output/'fresh-save.log').open('w')as f:subprocess.run(savecommand,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
   fresh=read(output/'fresh-save.json');require(fresh['fresh_original_lifecycle_save'] is True and fresh['save_commits']==2 and fresh['pid']==int(own),'no fresh original lifecycle save')
   require(pid()==own,'PID changed while saving');dev.adb('shell','am','force-stop',PKG);require(not pid(),'owned process still alive');d['fresh_save_then_stopped']=True
  else:d['already_exited_no_save_restart']=True
  journal(path,d)

def read_json_line(text):return json.loads(text)

def pull_screenshot_after_restore(dev,path,d):
 dev.stopped();require(d.get('all_fixed_restored') and d.get('cleanup_complete'),'restore before screenshot pull')
 remote=d['remote_screenshot'];require(remote=='/data/local/tmp/d2r-original-fork-'+d['token']+'.png','unsafe screenshot path')
 require(dev.adb('shell','sha256sum',remote).split()[0]==d['screenshot_sha256'],'device screenshot changed')
 local=path.parent/'original-fork-HUD.png';require(not local.exists(),'existing screenshot must not be overwritten')
 dev.adb('pull',remote,str(local));require(sha(local)==d['screenshot_sha256'] and local.read_bytes()[:8]==b'\x89PNG\r\n\x1a\n','local screenshot hash/header mismatch')
 # This unique token path was absent before screencap, and still has the
 # exact captured digest. Pull only after fresh save/stop and full restore.
 command='set -eu; test "$(sha256sum "$1" | cut -d" " -f1)" = "$2"; rm "$1"'
 dev.adb('shell',shlex.join(['sh','-c',command,'sh',remote,d['screenshot_sha256']]))
 d['screenshot_pulled_after_game_stopped']=True;d['screenshot_temp_removed']=True;d['screenshot_local']=str(local);journal(path,d)


def transaction_identity(d,expected,path,serial):
 require(d['controller_sha256']==sha(__file__)and d['serial']==serial and d['baseline_fixed']==expected['baseline_fixed'] and d['candidate']==expected['candidate'] and d['frozen_inputs']==expected['frozen_inputs'],'journal identity changed')
 token=d['token'];require(re.fullmatch('[a-f0-9]{32}',token),'invalid token')
 require(d['stage']=='files/runtime/.original-fork-'+token and d['incoming']==d['stage']+'/incoming'and d['backup']==d['stage']+'/backup','unsafe stages')
 for label,target in DIRS:require(d['old_'+label]==expected['old_expected_directories'][label],'backup expectation changed')
 require(d['transfer']=='/data/local/tmp/d2r-original-fork-'+token+'.tar','unsafe transfer')
