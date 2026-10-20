#!/usr/bin/env python3
"""One reversible bounded existing PanVK failure-only TRACE original-BC diagnostic. Offline by default.
Uses existing directory CAS, ordinary first-PID ownership and original save2 helper.
Original BC/current-identical upstream UI overlay only; no APK/module changes, old capture, measurement or GPU-import.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,shlex,signal,subprocess,sys,time,uuid,zipfile,tarfile
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport'
OLD=R/'.local/worktrees/panvk-replay-rebind-alloc-v2/scripts/dev/run-panvk-layered-offscreen-game-diagnostic.py'
spec=importlib.util.spec_from_file_location('existing_original_game_transaction',OLD);old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
require=old.require;sha=old.sha;read=old.read;journal=old.journal
READY=R/'.local/logs/panvk-compute-shader-map-v1-source-ready-agent.json'
READY_SHA='c918c3cfdd02b2796013330d77dbeccb072a781121f88cbf26da5db6635592c5'
ROOT_REVIEW=R/'.local/logs/panvk-compute-shader-map-ac88c7c-root-source-review.json'
ROOT_REVIEW_SHA='84e07c478d50423277cb5846edf9e37f77cf390326a965742aa005d92092945a'
DRIVER_SHA='4e7b29e71cb4b205b55929f58c88e3d6521b19c02e0c0a2372b0672d5648a9e0'
SOURCE=R/'.local/worktrees/readback-fixture-class-restore-v1'
APK='a0bc159c20acc85fe2178a73d3e9f889d23e9422c0bb883982136cafa0f7f4ff';ELF='38d4b04de9eafdc5ec3495a4af2ee4391579b534ceb094b77355d01ab76ed4fc'
LOCK='files/runtime/.astc6-full-controller-lock'
CHAIN='cbd8d2da0b0f';OWNER='.minegraph-game-owner';DIRS={'driver':'files/runtime/vulkan-panfrost','temp':'files/runtime/temp','cache':'cache','overlay':'files/runtime/romfs_overlay'}
RESOURCE_READY=R/'.local/build/panvk-minegraph-original-bc-control-v5/composition.json'
RESOURCE_SHA='41ce2605b2150669081aa1978094d1296168d4c95c88c2bb0b21c7e6ff970e13'
DUMP_DIR='files/runtime/temp/pandecode'
DUMP_PREFIX='/data/data/'+PKG+'/files/runtime/temp/pandecode/cs'
PROPERTIES={'debug.mesa.panvk.debug':'startup,trace,kbase_diag,trace_failure_only,shader_map','debug.mesa.pandecode.dump.file':DUMP_PREFIX}
DUMP_SOFT_LIMIT=256*1024*1024
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
 require(sha(ROOT_REVIEW)==ROOT_REVIEW_SHA,'root source review changed');review=read(ROOT_REVIEW)
 require(review['ready']['sha256']==READY_SHA and review['not_a_hang_fix']is True,'root review target mismatch')
 require(sha(READY)==READY_SHA,'driver ready changed');d=read(READY)
 require(d['upstream_commit']=='703db0ebb9afb2c97ae31f24dba5981c7c765fc1' and d['target']['parallel_jobs']==2,'wrong source/build')
 require(d['diagnostic_only'] and d['only_delta_from_parent_d149_source']==['src/panfrost/vulkan/panvk_instance.c','src/panfrost/vulkan/panvk_instance.h','src/panfrost/vulkan/panvk_vX_shader.c'],'not exact compute shader identity metadata successor')
 require('libhardware.so'not in d['needed'],'not static HAL platform candidate')
 require(d['local_source_revision']=='ac88c7ccbe9d31d46483ed8ddd3a7d9df54c1f73','wrong frozen source')
 require(d['parent_ready']['sha256']=='78dfe83e095292150ee398d39d606001bdfa714b2be3c76344ff18a2a899d00c','wrong reviewed bounded tracer parent')
 for row in [d['parent_ready'],d['source_protocol_proof'],*d['debug_token_guard_evidence'],*d['host_parser_guard_evidence'],*d['token_build_evidence'],*d['shader_map_guard_evidence'],*d['shader_map_build_evidence'],*d['shader_map_host_libraries']]:require(sha(row['path'])==row['sha256'],'TRACE provenance changed: '+row['path'])
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
 cold=R/'.local/logs/panvk-existing-progress-originalBC-original022-mali-v1/cold-original-pause-no-world-proof-agent.json';require(sha(cold)=='8d4069268ce944774f19019da244fcd2afb39706a629c6376e9ce0b15d0f83bb','exact022 cold pause proof changed');cold=read(cold)
 require(cold['exact_canonical_APK_SHA256']==APK and cold['exact_ELF']==ELF,'cold proof target mismatch')
 for n,h in cold['compiled_producer_source_hashes'].items():require(sha(SOURCE/n)==h,'original conditional-save source changed: '+n)
 ap=R/'.local/build/image-binding-022d625-canonical-source/canonical.apk';require(sha(ap)==APK,'canonical APK changed')
 with zipfile.ZipFile(ap) as z:require(hashlib.sha256(z.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()==ELF,'canonical ELF changed')
 return d,p

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
 raw=dev.app('sha256sum',*expected);got={n:h for h,n in(line.split()for line in raw.splitlines())};require(got==expected,'original35 changed')
 require(dev.app('cat','files/runtime/shaders/.chain')==CHAIN,'shader chain differs')
 return {'APK':APK,'ELF':ELF,'files35':got,'shader_chain':CHAIN,'shaders':inventory(dev,'files/runtime/shaders')}
def owned(values,d,label,partial=False):
 require(isinstance(values,dict)and values.get(OWNER)==d['owner_sha256'],'candidate ownership absent: '+label)
 if label=='overlay':
  expected={**d['candidate_overlay'],OWNER:d['owner_sha256']}
  require(all(expected.get(k)==h for k,h in values.items()) if partial else values==expected,'candidate BC overlay changed')
 if label=='driver':require((values.get('libvulkan_panfrost.so')in(None,DRIVER_SHA) if partial else values.get('libvulkan_panfrost.so')==DRIVER_SHA) and all(k in (OWNER,'libvulkan_panfrost.so')or k.startswith('shader-cache/')for k in values),'candidate driver differs')
 return True

def get_property(dev,key):
 # Preserve the exact property value; remove only adb's line terminator.
 raw=subprocess.check_output(dev.command+['shell','getprop',key],timeout=15)
 return raw.decode().removesuffix('\n').removesuffix('\r')

def publish_properties(dev,path,d):
 require(all(len(v.encode())<92 for v in PROPERTIES.values()),'Android property value too long')
 d['properties_before']={k:get_property(dev,k)for k in PROPERTIES};d['properties_touched']=[];journal(path,d)
 for key,value in PROPERTIES.items():
  # Mark intent before mutation, so a transport exception cannot skip restore.
  d['properties_touched'].append(key);journal(path,d)
  dev.adb('shell',shlex.join(['setprop',key,value]));require(get_property(dev,key)==value,'property write rejected: '+key)
 d['properties_published']=True;journal(path,d)

def restore_properties(dev,path,d):
 dev.stopped()
 for key in reversed(d.get('properties_touched',[])):
  dev.adb('shell',shlex.join(['setprop',key,d['properties_before'][key]]));require(get_property(dev,key)==d['properties_before'][key],'property restore mismatch: '+key)
 d['exact_touched_property_values_restored']=True;journal(path,d)

def dump_bytes(dev):
 raw=dev.app('sh','-c','if test -d "$1"; then find "$1" -type f -name "cs.*" -exec stat -c %s {} +; fi','sh',DUMP_DIR)
 require(all(v.isdecimal()for v in raw.splitlines()),'unexpected dump size output')
 return sum(int(v)for v in raw.splitlines())

def preserve_dump(dev,path,d,out):
 # No command stream transfer while the original game is running.
 dev.stopped();values=inventory(dev,DUMP_DIR)
 if values is None:d['dump_absent']=True;journal(path,d);return
 require(values.get(OWNER)==d['owner_sha256']and all(k==OWNER or re.fullmatch(r'cs\.[A-Za-z0-9_.:-]+\.ctx-[0-9]+\.[0-9]+',k)for k in values),'unknown diagnostic dump file')
 d['dump_remote_SHA256']=values;d['dump_file_count']=len(values)-1;journal(path,d)
 archive=out/'native-pandecode.tar'
 with archive.open('wb')as f:subprocess.run(dev.command+['exec-out','run-as',PKG,'tar','cf','-','-C',DUMP_DIR,'.'],stdout=f,stderr=subprocess.PIPE,check=True,timeout=60)
 local={}
 with tarfile.open(archive,'r')as tf:
  for member in tf.getmembers():
   if member.isdir():require(member.name in ('.','./'),'unexpected dump tar directory');continue
   name=member.name.removeprefix('./');require(member.isfile()and name in values and name not in local and '/'not in name,'unsafe/duplicate dump tar entry')
   h=hashlib.sha256()
   with tf.extractfile(member)as f:
    for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
   local[name]=h.hexdigest()
 require(local==values and inventory(dev,DUMP_DIR)==values,'dump transfer identity mismatch')
 d['dump_archive']=record(archive);d['dump_pulled_only_after_game_stopped']=True;d['dump_transport_hashed_and_verified']=True;journal(path,d)

def native_log_text(dev,*args):
 # Bounded log byte ranges may end/start inside a UTF-8 character. Preserve
 # strict Device text decoding for all identities/hashes/JSON; only diagnostic
 # native-log display and ASCII sentinel matching replace partial characters.
 data=subprocess.check_output(dev.command+['shell',shlex.join(['run-as',PKG,*args])],timeout=180)
 return data.decode('utf-8',errors='replace').strip()

def failure_only_banner(native):
 matches=re.findall(r'^\[MESA-PANVK\] panvk_debug: ([^\n]+)$',native,re.M)
 return bool(matches) and all(set(v.split('|'))=={'startup','trace','kbase_diag','trace_failure_only','shader_map'}for v in matches)

def activity_stopped(activities,pid):
 # Exact target record only; whitespace never consumes the next line/block.
 for block in re.split(r'(?=^[ \t]*\*[ \t]+Hist[ \t]+#[0-9]+:)',activities,flags=re.M):
  head=block.splitlines()[0]if block.splitlines()else''
  if not re.match(r'^[ \t]*\*[ \t]+Hist[ \t]+#[0-9]+:',head):continue
  if not re.search(re.escape(PKG)+r'/\.MainActivity(?:[ \t}]|$)',head):continue
  if re.search(r'\bapp=ProcessRecord\{[^\n]*[ \t]'+re.escape(pid)+':'+re.escape(PKG)+r'/',block) and re.search(r'^[ \t]*(?:mState|state)=STOPPED\b',block,re.M):return True
 return False

def cold_pause_proof(before,after,activities,pid):
 # Exact frozen original022 JNI: if dm_ingame_now(), its unconditional save
 # LOG precedes dm_gfx_suspend(). LOG is unbuffered stderr. Fresh positive
 # drain completion + no save LOG proves that branch was not taken on pause.
 if 'Android native start pid='+pid+'\n'not in before or not after.startswith(before):return False
 fresh=after[len(before):]
 return ('[dm] [nvn] 图形挂起：在途 GPU 工作已清空' in fresh and 'Android background save 'not in fresh and activity_stopped(activities,pid))

def launch_once(dev,path,d,out):
 own=None;client=None;ambiguous=False;native_identity=False
 def current():return dev.adb('shell','pidof '+PKG+' || true')
 def observe():
  nonlocal own,ambiguous
  now=current()
  if now:
   try:old.claim_pid(d,now,own)
   except RuntimeError:
    ambiguous=True;d['ownership_failed_raw_pidof']=now;d['ownership_failed_first_PID']=own;journal(path,d);raise
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
    native=native_log_text(dev,'sh','-c','if test -f files/runtime/native.log; then tail -c 65536 files/runtime/native.log; fi')
    d['dump_last_observed_bytes']=dump_bytes(dev)
    if d['dump_last_observed_bytes']>=DUMP_SOFT_LIMIT:d['dump_soft_bound_reached']=True;journal(path,d);break
    if own and not native_identity:
     header=native_log_text(dev,'sh','-c','if test -f files/runtime/native.log; then head -c 4096 files/runtime/native.log; fi')
     native_identity=('Android native start pid='+own+'\n'in header)
     if native_identity:d['native_log_first_PID_verified']=True;journal(path,d)
    if native_identity and not d.get('failure_only_token_actual'):
     config=native_log_text(dev,'head','-c','65536','files/runtime/native.log')
     if '[MESA-PANVK] panvk_debug:'in config:
      require(failure_only_banner(config),'actual cached failure-only TRACE token absent; stop diagnostic')
      d['failure_only_token_actual']=True;journal(path,d)
    if native_identity:
     (out/'native.log').write_text(native)
     if 'FATAL'in native or 'Vulkan PanVK ICD load:'in native:d['native_failure_seen']=True;break
    if own and not now:d['owned_process_exited_during_launch']=True;break
    require(time.monotonic()<deadline,'bounded original launcher deadline');time.sleep(.4)
   if client.poll()is None:
    client.terminate();client.wait(timeout=10)
  d['launcher_returncode']=client.returncode;journal(path,d)
  require(not d.get('dump_soft_bound_reached'),'diagnostic soft output bound reached; incomplete capture preserved')
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
  require(d.get('failure_only_token_actual'),'actual failure-only configuration never confirmed')
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
  d['raw_pidof_after_bounded_exit_wait']=now;journal(path,d)
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
  preserve_dump(dev,path,d,out)
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
 require(not d.get('launch_attempted')or d.get('dump_transport_hashed_and_verified')or d.get('dump_absent'),'unretrieved native dump must remain in owned staging after stock restore')
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
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',default='10.10.10.44:34455');p.add_argument('--output',type=Path,required=True);p.add_argument('--execute',action='store_true');a=p.parse_args();driver,promotion=proof();resources=resource_proof()
 plan={'scope':'ONE bounded existing PanVK TRACE_FAILURE_ONLY+kbase_diag+compute shader_map diagnostic original022 originalBC; no DUMP, no resources decode, incomplete conservative earlier prefix only; NOT live PC/GPU fix/performance/stability/sharing acceptance','diagnostic_properties':PROPERTIES,'dump_soft_limit_bytes':DUMP_SOFT_LIMIT,'dump_output_bound_is_soft':True,'dump_transfer_only_after_stop':True,'driver':driver['driver'],'driver_ready_sha256':READY_SHA,'root_source_review':record(ROOT_REVIEW),'serial':a.serial,'fixed_mode':{'scale_3d':.6,'output':[2772,1240],'shared':False,'legacy_ASTC_copy':False},'launch':command(a.serial,a.output),'directories_CAS_isolated':DIRS,'resource_composition_sha256':RESOURCE_SHA,'candidate_overlay':resources['deploy_files'],'resource_transfer_sha256':resources['transfer_archive']['sha256'],'original_APK':APK,'original_ELF':ELF,'tools':{str(x):sha(x)for x in [Path(__file__),OLD,old.COMMON,SOURCE/'scripts/dev/android-game-launch.py',R/'scripts/dev/android-game-background-save.py']}}
 if not a.execute:print(json.dumps(plan,indent=2));return
 a.output.mkdir(parents=True,exist_ok=False);path=a.output/'result.json';token=uuid.uuid4().hex;stage='files/runtime/.minegraph-game-'+token;transfer='/data/local/tmp/minegraph-driver-'+token+'.so'
 d={**plan,'token':token,'stage':stage,'transfer':transfer,'resource_transfer':transfer+'.resources.tar','owner_sha256':hashlib.sha256((token+'\n').encode()).hexdigest(),'cleanup_errors':[],'phase':'preflight'};journal(path,d);dev=old.common.Device(a.serial);expected=promotion['promoted_module_and_resource_hashes'];claimed=False
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
  dev.adb('push',resources['transfer_archive']['path'],d['resource_transfer']);d['resource_transfer_pushed']=True;journal(path,d);require(dev.adb('shell','sha256sum',d['resource_transfer']).split()[0]==d['resource_transfer_sha256'],'BC tar transfer corrupt')
  dev.app('tar','xf',d['resource_transfer'],'-C',stage+'/incoming/overlay');owned(inventory(dev,stage+'/incoming/overlay'),d,'overlay')
  for label,target in DIRS.items():
   d['intent']='backup-'+label;journal(path,d)
   if d['old_'+label]is not None:rename_dir(dev,target,stage+'/backup/'+label,d['old_'+label])
   v=inventory(dev,stage+'/incoming/'+label);owned(v,d,label);d['intent']='publish-'+label;journal(path,d);rename_dir(dev,stage+'/incoming/'+label,target,v)
  dev.app('mkdir',DUMP_DIR);dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',DUMP_DIR+'/'+OWNER,token)
  publish_properties(dev,path,d)
  d['phase']='published';journal(path,d);launch_once(dev,path,d,a.output);d['phase']='actual-original-HUD-pending-visual-review'
 except BaseException as e:d['phase']='failed-preserved';d['error']=d.get('primary_launch_error',repr(e));d['raised_exception_after_launch_finally']=repr(e)
 finally:
  if d.get('properties_touched'):
   try:restore_properties(dev,path,d)
   except BaseException as e:d['cleanup_errors'].append(repr(e))
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
