#!/usr/bin/env python3
"""One reversible unmodified PUBLIC RC3 original-BC HUD55 compatibility start. Offline by default.
Uses existing directory CAS, ordinary first-PID ownership and original save2 helper.
Original BC/current-identical upstream UI overlay only; HUD55 APK/modules remain unchanged, old capture, measurement or GPU-import.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,shlex,signal,subprocess,sys,time,uuid,zipfile,tarfile
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport'
OLD=R/'.local/worktrees/panvk-replay-rebind-alloc-v2/scripts/dev/run-panvk-layered-offscreen-game-diagnostic.py'
spec=importlib.util.spec_from_file_location('existing_original_game_transaction',OLD);old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
require=old.require;sha=old.sha;read=old.read;journal=old.journal
READY=R/'.local/logs/panvk-public-rc3-driver-source-ready-agent.json'
READY_SHA='3afc572d5b8956abcfc9a4607d74217a959ffb62ab4fcdd037c1cb1a69a0caaa'
DRIVER_SHA='b46b9f0416f207a05482349dffb37b6f3d84c20762151c623774831ce3939445'
SOURCE=R/'.local/worktrees/debug-overlay-panel-v1'
SOURCE_COMMIT='55a11129069bb0145414804eccb9787900264e4a'
APK='bcfdd9a495988caacdacd07627b87c237fa78cfea5798de09311d2422f44d05a';ELF='5ed7df9d4cd28ec0972e414ee9d584bd1dda394ad71bbe43899efbe258d64757'
BASELINE_ROOT_SHA='cc1ee119ab43cdfd694fc94617e469de7c6deb2cbc0f2bc04e5fe8c008f73852'
BASELINE_READY=R/'.local/logs/mali-HUD55-public-RC3-selected35-baseline-root.json'
BASELINE_READY_SHA='54f8ddb73fc2392a88eb678a44acdf289d4763c1f205419224ccb64ddff4b77c'
CANONICAL_APK=R/'.local/build/debug-overlay-scale-55a1112-canonical-source-final/canonical.apk'
LIFECYCLE_SOURCE_HASHES={'dm/host/android/jni.c': '149b7543f47ecd9c74b08014816f1841e72ca3a14636205d6b5f7432e3c0a825', 'dm/host/diag.c': '1f3bec4bfb2380e77024500f5d6259e5850d44fa70afba721e234b1b90b7a4e4', 'dm/host/dm.h': 'e71013417b320fc1714c55266cd8fe24dee8d3f95e00d5d99ff1c40c6a8c8a42', 'dm/gfx/nvn/hle_nvn.c': '8d378bce01788b270e60354143bdde16e3b6997e765e74619a7cf077d558b9e3', 'scripts/dev/android-game-launch.py': 'f424840a0b4138a624ffc16919b9218b7ee6547d7c319fbbc6815dcad626458d'}
LOCK='files/runtime/.astc6-full-controller-lock'
CHAIN='cbd8d2da0b0f';OWNER='.minegraph-game-owner';DIRS={'driver':'files/runtime/vulkan-panfrost','temp':'files/runtime/temp','cache':'cache','overlay':'files/runtime/romfs_overlay'}
RESOURCE_READY=R/'.local/build/panvk-minegraph-original-bc-control-v5/composition.json'
RESOURCE_SHA='41ce2605b2150669081aa1978094d1296168d4c95c88c2bb0b21c7e6ff970e13'
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

# Original CAS function body resolves the corrected local inventory.
def rename_dir(dev,src,dst,expected):
 dev.stopped();require(inventory(dev,src)==expected and inventory(dev,dst)is None,'directory rename conflict')
 # Existing private-driver atomic directory publication: old files are never
 # copied or rewritten. A stopped PID is checked again inside run-as.
 dev.app('sh','-c','set -eu; test -z "$(pidof "$3" || true)"; test -d "$1"; test ! -e "$2"; mv "$1" "$2"','sh',src,dst,PKG)
 require(inventory(dev,dst)==expected,'directory rename verification failed')

def record(p):p=Path(p).resolve();return {'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size}
def proof():
 require(sha(READY)==READY_SHA,'driver ready changed');d=read(READY)
 require(d['status']=='PUBLIC_RELEASE_DRIVER_BYTES_UNMODIFIED_SOURCE_ONLY_NO_DEVICE' and d['associated_source_commit']=='fd6ed91eff1c8d3a83307a2348b959bb93797011','wrong published RC3 source')
 require(d['ELF_BuildID']=='4ec75e25deca57c27fb1e0872fdbf58473d41527' and d['driver']['sha256']==DRIVER_SHA,'wrong public ELF')
 require(d['needed']==['liblog.so','libnativewindow.so','libsync.so','libm.so','libz.so','libdl.so','libc.so'],'unexpected namespace dependency')
 require(set(d['ICD_exports_verified'])=={'vk_icdGetInstanceProcAddr','vk_icdGetPhysicalDeviceProcAddr','vk_icdNegotiateLoaderICDInterfaceVersion'},'wrong ICD exports')
 for row in [d['driver'],d['associated_source_archive'],*d['public_receipts'],*d['ELF_reports']]:require(sha(row['path'])==row['sha256'],'published identity changed: '+row['path'])
 sums=Path(next(x['path']for x in d['public_receipts']if Path(x['path']).name=='SHA256SUMS')).read_text()
 require(DRIVER_SHA+'  libvulkan_panfrost-android-aarch64.so' in sums,'published sum missing')
 download=read(next(x['path']for x in d['public_receipts']if Path(x['path']).name=='download-receipt.json'))
 require(download['published_SHA256SUMS_verified'] and any(x['sha256']==DRIVER_SHA and x['github_digest_verified']for x in download['assets']),'GitHub public digest mismatch')
 # The old022 cold observation is historical only. HUD55 uses its own pinned
 # compiled source contract; the live no-world predicate remains unchanged.
 require(subprocess.check_output(['git','rev-parse','HEAD'],cwd=SOURCE,text=True).strip()==SOURCE_COMMIT,'HUD55 source revision changed')
 for n,h in LIFECYCLE_SOURCE_HASHES.items():require(sha(SOURCE/n)==h,'HUD55 source changed: '+n)
 require(sha(CANONICAL_APK)==APK,'HUD55 canonical APK changed')
 with zipfile.ZipFile(CANONICAL_APK) as z:require(hashlib.sha256(z.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()==ELF,'HUD55 canonical ELF changed')
 return d

def baseline_proof(row,execute=False):
 require(row is not None,'HUD55 actual receipt and selected baseline required')
 require(row['sha256']==BASELINE_READY_SHA and sha(row['path'])==BASELINE_READY_SHA,'HUD55 baseline Ready changed');p=read(row['path'])
 require(p['schema']==1 and p['status']=='ROOT_ACTUAL_STOPPED_HUD55_SELECTED35_BASELINE_FOR_PUBLIC_RC3_CONTROL','wrong HUD55 root baseline')
 require(p['source_revision']==SOURCE_COMMIT and p['original_APK']==APK and p['original_ELF']==ELF,'HUD55 baseline identity mismatch')
 expected=p['original_modules_resources_private_driver_hashes']
 require(isinstance(expected,dict) and len(expected)==35 and all(re.fullmatch('[a-f0-9]{64}',h) and n.startswith('files/runtime/') and '..'not in Path(n).parts for n,h in expected.items()),'HUD55 selected35 invalid')
 require(p['shaderchain']==CHAIN and p['original_game_process_absent']is True,'HUD55 baseline shader/process mismatch')
 actual=p['root_actual_review'];require(actual['sha256']==BASELINE_ROOT_SHA and sha(actual['path'])==BASELINE_ROOT_SHA,'HUD55 actual receipt changed');q=read(actual['path'])
 require(q['status']=='ROOT_ACTUAL_55A1112_SCALE_SELECTOR_PASS_SAVED_STOPPED' and q['runtime']==SOURCE_COMMIT and q['APK_persisted']==APK and q['host_ELF']==ELF and q['actual_original_game']is True and q['original_save2']is True and q['original_graphics_suspend']is True and q['stopped']is True,'HUD55 actual baseline not approved')
 return expected

def resource_proof():
 require(sha(RESOURCE_READY)==RESOURCE_SHA,'BC resource composition changed');c=read(RESOURCE_READY)
 require(c['complete'] and not c['deploy_allowed'] and not c['game_acceptance'],'not offline BC validation composition')
 require(c['proofs']['original_texture_and_cache_CFT_entries_identical']==31093 and c['proofs']['other_including_all_UI_CFT_entries_current_full4_identical']==102107 and c['proofs']['ctrl_touch_and_strings_current_Mali_identical'],'BC/UI proof incomplete')
 require(set(c['deploy_files'])=={'ctrl_touch','touch_strings.tsv','data/.build.config','data/00-00000007.data'},'not exact four BC overlay files')
 for row in [c['old_resource_proof'],c['preparer'],c['original_manifest'],*c['source_configs'],*c['source_tools'],c['transfer_archive']]:require(sha(row['path'])==row['sha256'],'BC provenance changed: '+row['path'])
 old.common.check_map(Path(c['deploy_overlay']),c['deploy_files'],old.common.Hashes(),exact=True)
 with tarfile.open(c['transfer_archive']['path'],'r')as archive:
  members=archive.getmembers();require(len(members)==4 and {m.name for m in members}==set(c['deploy_files']),'BC tar member set changed')
  for member in members:require(member.isfile() and not Path(member.name).is_absolute() and '..'not in Path(member.name).parts and hashlib.sha256(archive.extractfile(member).read()).hexdigest()==c['deploy_files'][member.name],'unsafe/corrupt BC tar member')
 return c

def command(serial,out):
 return [sys.executable,str(SOURCE/'scripts/dev/android-game-launch.py'),'--serial',serial,'--vulkan-provider','panvk','--no-shared-guest-heap','--no-legacy-astc-copy','--external-profile','--fixed-3d','--scale-3d','0.6','--game-res','2772x1240','--timeout','75','--report',str(out/'launch.json')]

def layers(dev):return {k:dev.adb('shell','settings','get','global',k)for k in LAYER_KEYS}
def no_layers(v):return v['enable_gpu_debug_layers']in('null','0') and all(v[k]in('null','')for k in LAYER_KEYS[1:])
def baseline(dev,expected):
 dev.stopped();path=dev.adb('shell','pm','path',PKG).removeprefix('package:');require(path.startswith('/')and'\n'not in path,'unknown split APK')
 require(dev.adb('shell','sha256sum',path).split()[0]==APK,'installed APK differs')
 raw=dev.app('sha256sum',*expected);got={n:h for h,n in(line.split()for line in raw.splitlines())};require(got==expected,'HUD55 selected35 changed')
 require(dev.app('cat','files/runtime/shaders/.chain')==CHAIN,'shader chain differs')
 return {'APK':APK,'ELF':ELF,'files35':got,'shader_chain':CHAIN,'shaders':inventory(dev,'files/runtime/shaders')}
def owned(values,d,label,partial=False):
 require(isinstance(values,dict)and values.get(OWNER)==d['owner_sha256'],'candidate ownership absent: '+label)
 if label=='overlay':
  expected={**d['candidate_overlay'],OWNER:d['owner_sha256']}
  require(all(expected.get(k)==h for k,h in values.items()) if partial else values==expected,'candidate BC overlay changed')
 if label=='driver':require((values.get('libvulkan_panfrost.so')in(None,DRIVER_SHA) if partial else values.get('libvulkan_panfrost.so')==DRIVER_SHA) and all(k in (OWNER,'libvulkan_panfrost.so')or k.startswith('shader-cache/')for k in values),'candidate driver differs')
 return True

def native_log_text(dev,*args):
 # Bounded log byte ranges may end/start inside a UTF-8 character. Preserve
 # strict Device text decoding for all identities/hashes/JSON; only diagnostic
 # native-log display and ASCII sentinel matching replace partial characters.
 data=subprocess.check_output(dev.command+['shell',shlex.join(['run-as',PKG,*args])],timeout=180)
 return data.decode('utf-8',errors='replace').strip()

def activity_stopped(activities,pid):
 # Exact target record only; whitespace never consumes the next line/block.
 for block in re.split(r'(?=^[ \t]*\*[ \t]+Hist[ \t]+#[0-9]+:)',activities,flags=re.M):
  head=block.splitlines()[0]if block.splitlines()else''
  if not re.match(r'^[ \t]*\*[ \t]+Hist[ \t]+#[0-9]+:',head):continue
  if not re.search(re.escape(PKG)+r'/\.MainActivity(?:[ \t}]|$)',head):continue
  if re.search(r'\bapp=ProcessRecord\{[^\n]*[ \t]'+re.escape(pid)+':'+re.escape(PKG)+r'/',block) and re.search(r'^[ \t]*(?:mState|state)=STOPPED\b',block,re.M):return True
 return False

def cold_pause_proof(before,after,activities,pid):
 # Exact pinned HUD55 JNI: if dm_ingame_now(), its unconditional save
 # LOG precedes dm_gfx_suspend(). LOG is unbuffered stderr. Fresh positive
 # drain completion + no save LOG proves that branch was not taken on pause.
 if 'Android native start pid='+pid+'\n'not in before or not after.startswith(before):return False
 fresh=after[len(before):]
 return ('[dm] [nvn] 图形挂起：在途 GPU 工作已清空' in fresh and 'Android background save 'not in fresh and activity_stopped(activities,pid))

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
    native=native_log_text(dev,'sh','-c','if test -f files/runtime/native.log; then cat files/runtime/native.log; fi')
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
  native=native_log_text(dev,'cat','files/runtime/native.log');(out/'native.log').write_text(native)
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
  require('PanVK-kbase RC3' in native,'actual driverInfo is not RC3')
  d['actual_original_game_HUD_reached']=True;d['real_game_modules_executed']=True;d['namespace_load_succeeded']=True;d['visual_acceptance_pending']=True;journal(path,d)
 except BaseException as e:
  d['primary_launch_error']=repr(e);journal(path,d);raise
 finally:
  if client and client.poll()is None:client.terminate();client.wait(timeout=10)
  for remote,local in [('files/runtime/native.log','native.log'),('files/runtime/game-state.jsonl','game-state.jsonl')]:
   try:(out/local).write_text((native_log_text if remote.endswith('native.log') else lambda d,*a:d.app(*a))(dev,'sh','-c','if test -f "$1"; then cat "$1"; fi','sh',remote))
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
   before_save=native_log_text(dev,'cat','files/runtime/native.log')
   require('Android native start pid='+own+'\n'in before_save,'same-PID save log absent')
   try:
    with(out/'fresh-save.txt').open('w')as f:save_result=subprocess.run(cmd,check=False,stdout=f,stderr=subprocess.STDOUT,timeout=45)
    save_rc=save_result.returncode
   except subprocess.TimeoutExpired:
    d['original_save_helper_timed_out']=True;save_rc=None
   after_pid=current()
   if not after_pid:d['already_exited_during_pause_no_save_restart']=True
   else:
    require(after_pid==own,'PID changed after original pause attempt')
    if save_rc==0:
     f=read(out/'fresh-save.json');require(f['fresh_original_lifecycle_save']and f['save_commits']==2 and f['pid']==int(own),'no fresh original save2')
     d['fresh_save2_then_stopped']=True
    else:
     after_save=native_log_text(dev,'cat','files/runtime/native.log');activities=dev.adb('shell','dumpsys','activity','activities');(out/'pause-activities.txt').write_text(activities);(out/'pause-native.log').write_text(after_save)
     require(cold_pause_proof(before_save,after_save,activities,own),'original save failed without exact same-PID positive no-world suspend proof')
     d['cold_original_pause_no_world_save_required']=True
    require(current()==own,'PID changed during save');dev.adb('shell','am','force-stop',PKG);dev.stopped()
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
 if d.get('resource_transfer_pushed'):
  require(dev.adb('shell','sha256sum',d['resource_transfer']).split()[0]==d['resource_transfer_sha256'],'BC resource transfer changed');dev.adb('shell','rm',d['resource_transfer'])
 d['owned_staging_and_transfer_cleaned']=True;journal(path,d)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline-ready',type=Path);p.add_argument('--baseline-ready-sha256');p.add_argument('--execute',action='store_true');a=p.parse_args();driver=proof();resources=resource_proof()
 require(bool(a.baseline_ready)==bool(a.baseline_ready_sha256),'baseline path/hash must be supplied together')
 baseline_row={'path':str(a.baseline_ready.resolve()),'sha256':a.baseline_ready_sha256} if a.baseline_ready else None
 expected=baseline_proof(baseline_row,a.execute) if baseline_row else None
 if baseline_row:require(read(baseline_row['path'])['serial']==a.serial,'HUD55 baseline serial mismatch')
 if a.execute:require(expected is not None,'DRAFT: HUD55 actual receipt/selected35 missing')
 plan={'scope':'ONE unmodified PUBLIC RC3 HUD55 original-BC/current-identical-upstreamUI compatibility baseline; no TRACE/DUMP/debug properties, oldcapture/APK/module mutation, ASTC performance or sharing claim','driver':driver['driver'],'driver_ready_sha256':READY_SHA,'serial':a.serial,'fixed_mode':{'scale_3d':.6,'output':[2772,1240],'shared':False,'legacy_ASTC_copy':False},'launch':command(a.serial,a.output),'directories_CAS_isolated':DIRS,'resource_composition_sha256':RESOURCE_SHA,'candidate_overlay':resources['deploy_files'],'resource_transfer_sha256':resources['transfer_archive']['sha256'],'original_APK':APK,'original_ELF':ELF,'baseline_ready':baseline_row,'execution_ready':expected is not None,'missing_inputs':[]if expected else['HUD55 root actual receipt + selected35 baseline Ready'],'tools':{str(x):sha(x)for x in [Path(__file__),OLD,old.COMMON,SOURCE/'scripts/dev/android-game-launch.py',R/'scripts/dev/android-game-background-save.py']}}
 if not a.execute:print(json.dumps(plan,indent=2));return
 a.output.mkdir(parents=True,exist_ok=False);path=a.output/'result.json';token=uuid.uuid4().hex;stage='files/runtime/.minegraph-game-'+token;transfer='/data/local/tmp/minegraph-driver-'+token+'.so'
 d={**plan,'token':token,'stage':stage,'transfer':transfer,'resource_transfer':transfer+'.resources.tar','owner_sha256':hashlib.sha256((token+'\n').encode()).hexdigest(),'cleanup_errors':[],'phase':'preflight'};journal(path,d);dev=old.common.Device(a.serial);claimed=False
 def interrupt(sig,frame):raise RuntimeError('interrupted; owned save/stop+restore only')
 for sig in(signal.SIGINT,signal.SIGTERM):signal.signal(sig,interrupt)
 try:
  d['baseline_before']=baseline(dev,expected);d['settings_before']=layers(dev);require(no_layers(d['settings_before']),'external GPU layer active')
  sdk=dev.adb('shell','getprop','ro.build.version.sdk');require(sdk.isdecimal()and int(sdk)>=35,'public RC3 needs Android API35')
  d['debug_property_before']=dev.adb('shell','getprop','debug.mesa.panvk.debug');require(not d['debug_property_before'],'external PanVK debug flags active')
  dev.stopped()
  for label,target in DIRS.items():d['old_'+label]=inventory(dev,target)
  journal(path,d);require(inventory(dev,stage)is None,'transaction exists');dev.app('mkdir',LOCK);d['lock_claimed']=True;dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',LOCK+'/owner',token);dev.app('mkdir',stage);claimed=True;dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',stage+'/'+OWNER,token);dev.app('mkdir',stage+'/incoming',stage+'/backup')
  for label in DIRS:
   pth=stage+'/incoming/'+label;dev.app('mkdir',pth);dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',pth+'/'+OWNER,token)
  dev.adb('push',driver['driver']['path'],transfer);d['transfer_pushed']=True;journal(path,d);require(dev.adb('shell','sha256sum',transfer).split()[0]==DRIVER_SHA,'pushed driver corrupt')
  dev.app('cp',transfer,stage+'/incoming/driver/libvulkan_panfrost.so')
  dev.adb('push',resources['transfer_archive']['path'],d['resource_transfer']);d['resource_transfer_pushed']=True;journal(path,d);require(dev.adb('shell','sha256sum',d['resource_transfer']).split()[0]==d['resource_transfer_sha256'],'BC tar transfer corrupt')
  dev.app('tar','xf',d['resource_transfer'],'-C',stage+'/incoming/overlay');owned(inventory(dev,stage+'/incoming/overlay'),d,'overlay')
  for label,target in DIRS.items():
   d['intent']='backup-'+label;journal(path,d)
   if d['old_'+label]is not None:rename_dir(dev,target,stage+'/backup/'+label,d['old_'+label])
   v=inventory(dev,stage+'/incoming/'+label);owned(v,d,label);d['intent']='publish-'+label;journal(path,d);rename_dir(dev,stage+'/incoming/'+label,target,v)
  d['phase']='published';journal(path,d);launch_once(dev,path,d,a.output);d['phase']='actual-unmodified-public-RC3-original-HUD-pending-visual-review'
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
   d['baseline_after']=baseline(dev,expected);require(d['baseline_after']==d['baseline_before'],'original APK/modules/resources/shaders changed');require(layers(dev)==d['settings_before'],'GPU layer settings changed');require('debug_property_before'not in d or dev.adb('shell','getprop','debug.mesa.panvk.debug')==d['debug_property_before'],'PanVK debug property changed');dev.stopped();d['original_APK_modules_resources_shaders_preserved']=True;d['game_stopped']=True
  except BaseException as e:d['cleanup_errors'].append(repr(e))
  journal(path,d)
 print(json.dumps({k:v for k,v in d.items()if k not in('baseline_before','baseline_after')},indent=2))
 if d['phase']=='failed-preserved'or d['cleanup_errors']:raise SystemExit(1)
if __name__=='__main__':main()
