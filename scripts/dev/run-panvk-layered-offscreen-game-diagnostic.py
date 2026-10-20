#!/usr/bin/env python3
"""ONE original-fork PanVK + official VVL real-game diagnostic. Default offline.
Reuse the frozen resource Device/Hashes/journal and original launcher/save.
Temporary exact canonical debug APK/raw-NSO modules, BC/UI-only resources; full022d rollback. No system driver, profiler or driver debug override.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, shlex, signal, subprocess, sys, tarfile, time, uuid, zipfile
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport'
COMMON=R/'.local/build/deploy-astc6-full6-36a9781-v1.py'
SOURCE=Path(__file__).resolve().parents[2]
ORIGINAL_SOURCE=R/'.local/worktrees/readback-fixture-class-restore-v1'
ORIGINAL_BUNDLE=R/'.local/build/native-touch-x18-image-binding-022d625'
ORIGINAL_APK=R/'.local/build/image-binding-022d625-canonical-source/canonical.apk'
CANDIDATE_APK=SOURCE/'.local/build/android-apk/d2r-native-tlsdesc-debug.apk'
CANDIDATE_BUNDLE=R/'.local/build/native-touch-x18-color-depth-comparison-v1'
TOOL_REVIEW=R/'.local/logs/panvk-layered-vvl-tool-v1-mali-independent-review.json'
DRIVER_READY=R/'.local/logs/panvk-original-fork-baseline-v2-source-ready.json'
RESOURCE_PROOF=R/'.local/logs/panvk-original-fork-ui-only-resource-proof-v2.json'
PROMOTION=R/'.local/logs/image-binding-022d625-default-promote-mali-v1/root-result.json'
DRIVER=R/'.local/build/panvk-original-fork-baseline-v2/frozen/libvulkan_panfrost.so'
UI=R/'.local/build/android-romfs-overlay'
OVERLAY='files/runtime/romfs_overlay';DRIVER_DIR='files/runtime/vulkan-panfrost'
LOCK='files/runtime/.astc6-full-controller-lock';OWNER='.original-fork-baseline-owner'
APK='a0bc159c20acc85fe2178a73d3e9f889d23e9422c0bb883982136cafa0f7f4ff'
ELF='38d4b04de9eafdc5ec3495a4af2ee4391579b534ceb094b77355d01ab76ed4fc'
SO='c5ecf526c0113c976366818c931173f661adb7df4f519b9a2e9b6ed9456a71b2';CHAIN='cbd8d2da0b0f'
spec=importlib.util.spec_from_file_location('existing_resource_journal',COMMON);common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
require=common.require;journal=common.journal
sha=lambda p:common.Hashes().file(p)
read=lambda p:json.loads(Path(p).read_text())

def proof():
 require(sha(DRIVER_READY)=='222ba1e9db1f57d3d654b4654b13a2d2b25a2e09431a26566609e86b97227d41','driver sourceReady changed')
 require(sha(RESOURCE_PROOF)=='76438192aedb73d5457901f4284581d1db6ff1d9db3aafbc3e80bd429d2ab1e5','UI resource proof changed')
 dr=read(DRIVER_READY);ui=read(RESOURCE_PROOF);pr=read(PROMOTION)
 require(dr['driver']['archive']['sha256']==sha(DRIVER)==SO and dr['byte_identical_to_initial_c5ec'] and not dr['local_PanVK_core_delta_from_original_fork'],'original fork driver identity')
 require(dr['upstream_fork']['commit']=='5b87b74d3e2580d5816e7f2c629f6f016a462f28' and dr['patch_count']==179,'not the declared original fork')
 require(ui['changed_texture_or_cache_entries']==[] and ui['texture_and_desc_cache_CFT_entries_verified_identical']==31093 and ui['original_filetable_prefix_identical'],'BC resource proof absent')
 values={x['relative']:x['sha256'] for x in ui['overlay_files']}
 require(set(values)=={'ctrl_touch','touch_strings.tsv','data/.build.config','data/00-00000007.data'},'exact UI-only four required')
 common.check_map(UI,values,common.Hashes(),exact=True)
 require(pr['production_promoted_and_stopped'] and pr['default_mode_verified'] and pr['rollback_errors']==[] and pr['source_revision']=='022d6254d9de565852fa22394b6403491db86707' and pr['canonical_APK_SHA256']==APK and pr['host_ELF_SHA256']==ELF,'promoted022d baseline proof')
 baseline=pr['promoted_module_and_resource_hashes'];require(len(baseline)==35,'exact35 promoted files')
 require({n.removeprefix(OVERLAY+'/'):h for n,h in baseline.items()if n.startswith(OVERLAY+'/')}==common.BASE,'full4 baseline changed')
 ap=ORIGINAL_APK;require(sha(ap)==APK,'original022d APK changed')
 with zipfile.ZipFile(ap) as z:require(hashlib.sha256(z.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()==ELF,'canonical ELF changed')
 modules={n.removeprefix('files/runtime/modules/'):{'sha256':h} for n,h in baseline.items()if n.startswith('files/runtime/modules/')};require(len(modules)==28,'exact28 modules')
 review=read(TOOL_REVIEW);require(review['positive_GPU_bytes'] and review['negative_once'] and review['invalid_call_blocked_before_ICD'] and review['cleanup_errors']==[] and review['driverID']==20,'original privateICD/VVL tool not accepted')
 build=read(CANDIDATE_APK.with_suffix('.apk.build.json'));require(sha(CANDIDATE_APK)==build['apk_sha256'] and not build['contains_game_binary'],'candidate canonical APK invalid')
 for name,h in build['source_inputs'].items():require(sha(SOURCE/name)==h,'candidate producer source changed: '+name)
 with zipfile.ZipFile(CANDIDATE_APK)as z:
  libs={Path(n).name:hashlib.sha256(z.read(n)).hexdigest()for n in z.namelist()if n.startswith('lib/arm64-v8a/')and n.endswith('.so')}
 require(len(libs)==7 and libs['libVkLayer_khronos_validation.so']==build['official_validation_layer']['sha256'],'official VVL absent from candidate')
 for name,h in build['native_library_sha256'].items():require(libs[name]==h,'candidate runtime ELF changed')
 old_modules=bundle_artifacts(ORIGINAL_SOURCE,ORIGINAL_BUNDLE)
 require({n:v['sha256']for n,v in old_modules.items()}=={n:v['sha256']for n,v in modules.items()},'restore022d raw modules differ from promotion')
 new_modules=bundle_artifacts(SOURCE,CANDIDATE_BUNDLE)
 deps=[Path(__file__),COMMON,DRIVER_READY,RESOURCE_PROOF,PROMOTION,DRIVER,SOURCE/'scripts/dev/android-game-launch.py',R/'scripts/dev/android-game-background-save.py',TOOL_REVIEW,ORIGINAL_APK,CANDIDATE_APK,CANDIDATE_APK.with_suffix('.apk.build.json'),ORIGINAL_BUNDLE/'rewrite-manifest.json',CANDIDATE_BUNDLE/'rewrite-manifest.json',SOURCE/'scripts/dev/deploy-android-modules.py',ORIGINAL_SOURCE/'scripts/dev/deploy-android-modules.py']
 deps += [CANDIDATE_BUNDLE/n for n in new_modules]+[ORIGINAL_BUNDLE/n for n in old_modules]
 return {'apk':APK,'elf':ELF,'chain':CHAIN,'baseline35':baseline,'baseline':common.BASE,'module_artifacts':modules,'candidate':values,'frozen_inputs':{str(p):sha(p)for p in deps},'driver':SO,'candidate_APK':build['apk_sha256'],'candidate_ELF':build['host_library_sha256'],'candidate_modules':new_modules}

def bundle_artifacts(source,bundle):
 b=read(bundle/'rewrite-manifest.json');opts=b['options'];raw=Path(b['modules']['main']['recipe']['input']['path']).parent
 command=[str(R/'.local/venv/bin/python'),str(source/'scripts/dev/rewrite-native.py'),'--source',str(raw),'--output',str(bundle),'--abi',opts['DM_HOST_ABI'],'--rules',opts['DM_RULESET'],'--x18-group-size',opts['DM_X18_GROUP_SIZE'],'--private-gates',opts['DM_PRIVATE_GATES'],'--verify-only']
 subprocess.run(command,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 values=dict(b['artifacts'])
 for module in b['modules'].values():values.update(module['artifacts'])
 values['rewrite-manifest.json']={'sha256':sha(bundle/'rewrite-manifest.json'),'bytes':(bundle/'rewrite-manifest.json').stat().st_size}
 require(len(values)==28,'exact28 canonical module set')
 for n,v in values.items():require(Path(n).name==n and sha(bundle/n)==v['sha256'] and (bundle/n).stat().st_size==v['bytes'],'raw-NSO artifact mismatch')
 return values

def launch_command(serial,output):
 return [sys.executable,str(SOURCE/'scripts/dev/android-game-launch.py'),'--serial',serial,'--validate','--panvk-offscreen-diagnostic','--vulkan-provider','panvk-layered-debug','--no-shared-guest-heap','--no-legacy-astc-copy','--no-astc-decode-unorm8','--no-defer-pool-publication','--scale-3d','0.75','--fixed-3d','--game-res','2772x1240','--timeout','75','--report',str(output/'launch.json')]

def installed_apk(dev):
 path=dev.adb('shell','pm','path',PKG).removeprefix('package:');require(path.startswith('/')and'\n'not in path,'unknown installed package')
 return dev.adb('shell','sha256sum',path).split()[0]

def deploy_canonical(dev,path,d,output,restore=False):
 dev.stopped();current=installed_apk(dev);require(current in (APK,d['candidate_APK']),'unknown package: do not overwrite')
 src=ORIGINAL_SOURCE if restore else SOURCE;bundle=ORIGINAL_BUNDLE if restore else CANDIDATE_BUNDLE;apk=ORIGINAL_APK if restore else CANDIDATE_APK
 label='restore022d' if restore else 'candidate'
 # Intent before installation: interrupted/failed upgrades still get exact
 # old/candidate APK classification before restoration, never uninstall/save wipe.
 if not restore:d['candidate_install_intent']=True;journal(path,d)
 with(output/(label+'-install.log')).open('w')as f:
  subprocess.run(dev.command+['install','-r','--no-incremental',str(apk)],check=True,stdout=f,stderr=subprocess.STDOUT,timeout=90)
 with(output/(label+'-modules.log')).open('w')as f:
  subprocess.run([str(R/'.local/venv/bin/python'),str(src/'scripts/dev/deploy-android-modules.py'),'--bundle',str(bundle),'--serial',d['serial'],'--report',str(output/(label+'-modules.json'))],check=True,stdout=f,stderr=subprocess.STDOUT,timeout=120)
 expected=d['module_artifacts'] if restore else d['candidate_modules']
 got=dev.app('sha256sum',*['files/runtime/modules/'+n for n in expected]).splitlines()
 require({Path(n).name:h for h,n in (line.split()for line in got)}=={n:v['sha256']for n,v in expected.items()},'deployed module identity differs')
 require(installed_apk(dev)==sha(apk),'installed canonical APK differs')
 if restore:d['original_APK_modules_restored']=True
 else:d['candidate_APK_modules_installed']=True
 journal(path,d)

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
 result={n.removeprefix(path+'/'):h for h,n in (line.split()for line in lines)}
 require(len(result)==len(files) and all(re.fullmatch('[a-f0-9]{64}',h)for h in result.values()),'incomplete directory hashes');return result

def valid_new(label,values,d,partial=False):
 if label=='overlay':return isinstance(values,dict) and (all(d['candidate'].get(k)==h for k,h in values.items()) if partial else values==d['candidate'])
 if partial:return isinstance(values,dict) and all((k=='libvulkan_panfrost.so' and h==SO) or (k==OWNER and h==d['owner_sha256']) or k.startswith('shader-cache/') for k,h in values.items())
 return isinstance(values,dict) and values.get('libvulkan_panfrost.so')==SO and values.get(OWNER)==d['owner_sha256'] and all(n in ('libvulkan_panfrost.so',OWNER) or n.startswith('shader-cache/')for n in values)

def rename_dir(dev,src,dst,expected):
 dev.stopped();require(inventory(dev,src)==expected and inventory(dev,dst)is None,'directory rename conflict')
 # Existing private-driver atomic directory publication: old files are never
 # copied or rewritten. A stopped PID is checked again inside run-as.
 dev.app('sh','-c','set -eu; test -z "$(pidof "$3" || true)"; test -d "$1"; test ! -e "$2"; mv "$1" "$2"','sh',src,dst,PKG)
 require(inventory(dev,dst)==expected,'directory rename verification failed')

def publish(dev,path,d):
 for label,target in [('overlay',OVERLAY),('driver',DRIVER_DIR)]:
  before=d['old_'+label];incoming=d['incoming']+'/'+label;backup=d['backup']+'/'+label
  d['intent']='backup-'+label;journal(path,d);rename_dir(dev,target,backup,before)
  d['intent']='publish-'+label;journal(path,d);new=inventory(dev,incoming);require(valid_new(label,new,d),'incoming content changed')
  rename_dir(dev,incoming,target,new);d['intent']='published-'+label;journal(path,d)
 d['status']='deployed';journal(path,d)

def restore_dirs(dev,path,d):
 dev.identity(d)
 for label,target in [('driver',DRIVER_DIR),('overlay',OVERLAY)]:
  old=d['old_'+label];backup=d['backup']+'/'+label;current=inventory(dev,target);stored=inventory(dev,backup)
  if stored is None:
   require(current==old,'missing original backup/changed target: '+label);continue
  require(stored==old,'original backup changed: '+label)
  if current is not None:
   require(valid_new(label,current,d),'unowned/changed published directory: '+label)
   retired=d['stage']+'/after-'+label;d['intent']='retire-'+label;journal(path,d);rename_dir(dev,target,retired,current)
  d['intent']='restore-'+label;journal(path,d);rename_dir(dev,backup,target,old)
 require(inventory(dev,DRIVER_DIR)==d['old_driver'],'original private driver/cache changed')
 require(snapshot(dev,d)==d['baseline35'],'original35 files not restored')
 d['status']='restored';d['all35_restored']=True;journal(path,d)

def snapshot(dev,d):
 dev.identity(d);require(inventory(dev,OVERLAY)==d['baseline'],'baseline overlay differs/extra files')
 lines=dev.app('sha256sum',*d['baseline35']).splitlines();got={n:h for h,n in (line.split()for line in lines)};require(got==d['baseline35'],'35-file baseline changed');return got

def cleanup(dev,path,d):
 dev.stopped();require(d.get('all35_restored'),'must restore before cleanup')
 for root,label in [(d['incoming']+'/overlay','overlay'),(d['incoming']+'/driver','driver'),(d['stage']+'/after-overlay','overlay'),(d['stage']+'/after-driver','driver')]:
  values=inventory(dev,root)
  if values is not None:require(valid_new(label,values,d,partial=root.startswith(d['incoming']+'/')),'owned staging changed')
 files=dev.app('find',d['stage'],'-type','f').splitlines()
 allowed=(d['incoming']+'/overlay/',d['incoming']+'/driver/',d['stage']+'/after-overlay/',d['stage']+'/after-driver/')
 require(all(n.startswith(allowed)for n in files),'unknown file in transaction stage')
 for n in files:
  h=dev.digest(n);dev.app('sh','-c','set -eu; test -z "$(pidof "$3" || true)"; test "$(sha256sum "$1" | cut -d" " -f1)" = "$2"; rm "$1"','sh',n,h,PKG)
 for n in sorted(dev.app('find',d['stage'],'-type','d').splitlines(),key=lambda s:(s.count('/'),s),reverse=True):dev.app('rmdir',n)
 transfer=dev.digest(d['transfer'])
 if transfer is not None:
  require(transfer==d.get('tar_sha256'),'owned transfer changed');dev.adb('shell','rm',d['transfer'])
 local=Path(d.get('local_tar',str(path.parent/('owned-'+d['token']+'.tar'))));require(local.parent==path.parent and local.name=='owned-'+d['token']+'.tar','unsafe local tar')
 if local.exists():
  require(sha(local)==d.get('tar_sha256'),'local tar incomplete/changed: retained');local.unlink()
 owner=path.parent/('owner-'+d['token'])
 if owner.exists():require(owner.read_text()==d['token']+'\n','local owner changed');owner.unlink()
 require(dev.app('cat',LOCK+'/owner')==d['token'],'controller lock changed');dev.app('rm',LOCK+'/owner');dev.app('rmdir',LOCK)
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
    observe()
    log=dev.app('cat','files/runtime/native.log')
    current_start=own is not None and ('Android native start pid='+own+'\n')in log
    if current_start:(output/'native.log').write_text(log)
    if current_start and ('FATAL' in log or 'native fault signal=' in log):
     d['native_failure_seen']=True;d['known_device_lost']='Vulkan GPU fence wait: -4' in log;journal(path,d);break
    require(time.monotonic()<deadline,'one launcher deadline');time.sleep(.3)
   if not d.get('native_failure_seen'):observe()
   require(not d.get('native_failure_seen') and launcher.returncode==0,'Original native failure retained; not a game pass')
  result=read(output/'launch.json');require(own is not None and result['pid']==int(own),'launcher PID mismatch')
  require(result['actual_guest_heap']['shared'] is False and result['actual_vulkan_provider']=='panvk-layered-debug' and result['requested_legacy_astc_copy'] is False,'fixed control mode mismatch')
  require(result['actual_defer_pool_publication'] is False and result['requested_astc_decode_unorm8'] is False,'unexpected publication/decode override')
  native=dev.app('cat','files/runtime/native.log');(output/'native.log').write_text(native)
  require('[guest]'in native and 'PanVK layered private ICD mapping verified'in native and 'driverID=20'in native and 'FATAL'not in native,'native game/provider failure')
  require('[ASTC resource copy]'not in native,'unexpected ASTC adaptation in original BC control')
  current=read_json_line(dev.app('tail','-n','1','files/runtime/game-state.jsonl'))
  require(current['valid'] and current['world_input_ready']and current['area']==1,'original camp not ready')
  (output/'camp-state.json').write_text(json.dumps(current,indent=2)+'\n');d['offscreen_world_reached']=True;d['game_modules_executed']=True;d['playable_or_presentation_acceptance']=False;journal(path,d)

 finally:
  if launcher is not None and launcher.poll()is None:
   launcher.terminate()
   try:launcher.wait(timeout=15)
   except subprocess.TimeoutExpired:raise RuntimeError('owned launcher still active; preserve transaction')
  current=pid()
  if d.get('native_failure_seen') or ambiguous:
   natural=time.monotonic()+30
   while current and time.monotonic()<natural:
    d.setdefault('crash_PID_raw',[]).append(current);journal(path,d);time.sleep(.2);current=pid()
   require(not current,'Native/crash process did not naturally exit: preserve it')
  try:
   (output/'native.log').write_text(dev.app('cat','files/runtime/native.log'))
   (output/'mesa.log').write_text(dev.app('cat',DRIVER_DIR+'/shader-cache/mesa.log'))
   (output/'crash-logcat.txt').write_text(dev.adb('logcat','-b','crash','-d','-v','threadtime'))
  except Exception as e:d['log_collection_error']=repr(e)
  if current:
   require(not ambiguous and own is not None and current==own and current.isdecimal(),'unknown/replacement PID: no save/stop/restore')
   savecommand=[sys.executable,str(R/'scripts/dev/android-game-background-save.py'),'--serial',d['serial'],'--report',str(output/'fresh-save.json')]
   with(output/'fresh-save.log').open('w')as f:subprocess.run(savecommand,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
   fresh=read(output/'fresh-save.json');require(fresh['fresh_original_lifecycle_save'] is True and fresh['save_commits']==2 and fresh['pid']==int(own),'no fresh original lifecycle save')
   require(pid()==own,'PID changed while saving');dev.adb('shell','am','force-stop',PKG);require(not pid(),'owned process still alive');d['fresh_save_then_stopped']=True
  else:d['already_exited_no_save_restart']=True
  native=(output/'native.log').read_text() if(output/'native.log').exists()else''
  d['VUIDs']=re.findall(r'\[vulkan validation\] (VUID-[^: ]+|SYNC-[^: ]+):',native)
  d['synchronization_validation_requested']='synchronization validation requested'in native
  d['official_validation_banner_lines']=[line for line in native.splitlines()if 'CURRENT-VALIDATION-ENABLED' in line or 'Synchronization' in line or 'Core Checks' in line]
  journal(path,d)

def read_json_line(text):return json.loads(text)

def pull_screenshot_after_restore(dev,path,d):
 dev.stopped();require(d.get('all35_restored') and d.get('cleanup_complete'),'restore before screenshot pull')
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
 require(d['controller_sha256']==sha(__file__) and d['serial']==serial and d['baseline35']==expected['baseline35'] and d['candidate']==expected['candidate'] and d['frozen_inputs']==expected['frozen_inputs'],'journal identity mismatch')
 token=d['token'];require(re.fullmatch('[a-f0-9]{32}',token),'invalid transaction token')
 stage='files/runtime/.original-fork-'+token
 require(d['stage']==stage and d['incoming']==stage+'/incoming' and d['backup']==stage+'/backup' and d['transfer']=='/data/local/tmp/d2r-original-fork-'+token+'.tar' and d['owner_sha256']==hashlib.sha256((token+'\n').encode()).hexdigest() and d['remote_screenshot']=='/data/local/tmp/d2r-original-fork-'+token+'.png','unsafe transaction paths')
 require(d['old_overlay']==expected['baseline'] and d['old_driver'].get('libvulkan_panfrost.so')==expected['baseline35'][DRIVER_DIR+'/libvulkan_panfrost.so'] and OWNER not in d['old_driver'],'journal original directory differs')
 if 'local_tar'in d:require(Path(d['local_tar'])==path.parent/('owned-'+token+'.tar'),'unsafe local tar')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--action',choices=['plan','run','restore'],default='plan');p.add_argument('--execute',action='store_true');p.add_argument('--serial',default='10.10.10.44:34455');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 expected=proof();d=expected.copy();path=a.report.resolve();require(path.is_relative_to(R/'.local/logs'),'report must be private .local/logs')
 if a.action=='plan':
  print(json.dumps({'offline_only':True,'driver':SO,'driver_ready':sha(DRIVER_READY),'resources':d['candidate'],'baseline35':d['baseline35'],'launch':launch_command(a.serial,path.parent),'frozen_inputs':d['frozen_inputs'],'scope':'ONE actual original ARM64 OFFSCREEN game+official VVL/sync diagnostic (not playable/presentation acceptance); c5ec/sharedOFF/legacyASTCOFF/originalBC+UI-only. Temporary source-closed debug APK+fresh rawNSO modules, full022d restore. Diagnostic cost NOT FPS comparison.'},indent=2));return
 require(a.execute,'run/restore require --execute; no device touched');require(path.parent.is_dir(),'report parent must exist')
 if a.action=='restore':
  d=read(path);transaction_identity(d,expected,path,a.serial)
 else:
  require(not path.exists() and not path.with_name(path.name+'.tmp').exists(),'existing journal: do not retry')
  require(not any((path.parent/n).exists() for n in ('launch.json','fresh-save.json','native.log','camp-state.json','owned-status.txt','original-fork-HUD.png')),'use a fresh transaction output directory')
  token=uuid.uuid4().hex;stage='files/runtime/.original-fork-'+token
  d.update(token=token,serial=a.serial,stage=stage,incoming=stage+'/incoming',backup=stage+'/backup',transfer='/data/local/tmp/d2r-original-fork-'+token+'.tar',remote_screenshot='/data/local/tmp/d2r-original-fork-'+token+'.png',controller_sha256=sha(__file__),status='prepared',cleanup_errors=[],owner_sha256=hashlib.sha256((token+'\n').encode()).hexdigest())
 dev=common.Device(a.serial)
 def interrupted(sig,frame):raise RuntimeError('interrupted: original lifecycle and stopped-app restoration only')
 signal.signal(signal.SIGINT,interrupted);signal.signal(signal.SIGTERM,interrupted)
 acquired=False;prepared=False
 try:
  dev.stopped()
  if a.action=='restore':
   require(dev.app('cat',LOCK+'/owner')==d['token'],'not this resource lock');acquired=True;prepared=True
  else:
   snapshot(dev,d)
   require(dev.adb('shell','settings','get','global','enable_gpu_debug_layers') in ('0','null'),'external GPU layer is active')
   props=dev.adb('shell','getprop');require(not any(re.match(r'\[debug\.mesa\.panvk[^]]*\]: \[.+\]',x) for x in props.splitlines()),'nondefault PanVK properties: leave them unchanged')
   for q,h in d['frozen_inputs'].items():require(sha(q)==h,'frozen input changed')
   dev.stopped();dev.app('mkdir',LOCK);acquired=True;dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',LOCK+'/owner',d['token']);journal(path,d)
   d['old_overlay']=inventory(dev,OVERLAY);d['old_driver']=inventory(dev,DRIVER_DIR)
   require(d['old_overlay']==d['baseline'] and isinstance(d['old_driver'],dict) and d['old_driver'].get('libvulkan_panfrost.so')==d['baseline35'][DRIVER_DIR+'/libvulkan_panfrost.so'] and OWNER not in d['old_driver'],'original directories differ');journal(path,d)
   dev.app('sh','-c','test ! -e "$1"','sh',d['stage']);dev.app('mkdir','-p',d['incoming'],d['backup']);prepared=True
   owner=path.parent/('owner-'+d['token']);owner.write_text(d['token']+'\n');local=path.parent/('owned-'+d['token']+'.tar');d['local_tar']=str(local);journal(path,d)
   with tarfile.open(local,'x') as tar:
    tar.add(DRIVER,arcname='driver/libvulkan_panfrost.so',recursive=False);tar.add(owner,arcname='driver/'+OWNER,recursive=False)
    for n in d['candidate']:tar.add(common.safe(UI,n),arcname='overlay/'+n,recursive=False)
   owner.unlink();d['tar_sha256']=sha(local);journal(path,d)
   dev.adb('push',str(local),d['transfer']);require(dev.digest(d['transfer'])==d['tar_sha256'],'transfer hash differs');dev.stopped();dev.app('tar','xf',d['transfer'],'-C',d['incoming'])
   publish(dev,path,d);deploy_canonical(dev,path,d,path.parent);wait_game(dev,path,d,path.parent);d['actual_game_result']='offscreen-world-diagnostic-passed';journal(path,d)
 except BaseException as e:
  d['error']=repr(e);d['status']='failed-preserved';journal(path,d)
 finally:
  if acquired:
   try:
    if prepared:
     if d.get('candidate_install_intent') and not d.get('original_APK_modules_restored'):deploy_canonical(dev,path,d,path.parent,restore=True)
     restore_dirs(dev,path,d);cleanup(dev,path,d)
     if d.get('screenshot_device_written') and not d.get('screenshot_temp_removed'):pull_screenshot_after_restore(dev,path,d)
    else:
     dev.stopped();require(dev.app('cat',LOCK+'/owner')==d['token'],'lock identity changed');dev.app('rm',LOCK+'/owner');dev.app('rmdir',LOCK);d['lock_released_before_stage']=True;journal(path,d)
   except BaseException as e:d['cleanup_errors'].append(repr(e));journal(path,d)
 print(json.dumps(d,indent=2))
 if a.action=='restore':require(d.get('cleanup_complete') and d.get('all35_restored') and not d['cleanup_errors'],'recovery incomplete')
 else:require(d.get('offscreen_world_reached') and d.get('fresh_save_then_stopped') and d.get('cleanup_complete') and d.get('all35_restored') and not d.get('error') and not d['cleanup_errors'],'original-fork game/control incomplete')

if __name__=='__main__':main()
