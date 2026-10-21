#!/usr/bin/env python3
"""Single official Android GPU-call replay, independent ordinary UID. Plan by default.
Reuses the frozen independent-fixture PID/identity/cleanup protocol. No game,
API logger, sampler, system driver replacement, or automatic retries.
"""
import argparse,hashlib,json,re,subprocess,time,uuid,zipfile,signal,shlex,xml.etree.ElementTree as ET
from pathlib import Path
R=Path('/home/dl/d2r-mobile');P=argparse.ArgumentParser()
for n in ('serial','artifact','driver','capture','capture-source-result','baseline-ready','original-apk','output'):P.add_argument('--'+n,required=True,type=str if n=='serial'else Path)
P.add_argument('--execute',action='store_true');a=P.parse_args()
pkg='com.d2rmobile.panvkgfxrfixture';original='com.d2rmobile.nativeport';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(a.artifact/'candidate.apk.build.json')=='57c8b4ca332a0a1319cb5e41ae45f1027a16fd5ecb105c0dc9850e62b492b5ac'
m=json.loads((a.artifact/'candidate.apk.build.json').read_text())
assert m['package']==pkg and m['SDK_replay_native_unchanged']is True and m['game_modules_or_renderer_included']is False
assert sha(a.artifact/'candidate.apk')==m['APK']['sha256']
for row in m['sources']+m['actual_compile_inputs']+m['actual_tools']:assert sha(row['path'])==row['sha256'],row['path']
with zipfile.ZipFile(a.artifact/'candidate.apk')as z:assert {Path(n).name:hashlib.sha256(z.read(n)).hexdigest()for n in z.namelist()if n.startswith('lib/arm64-v8a/')}==m['native_libraries8']
assert len(m['native_libraries8'])==8
# The test driver is public RC3. The capture's original c5ec identity below is independent.
PUBLIC_DRIVER='b46b9f0416f207a05482349dffb37b6f3d84c20762151c623774831ce3939445'
DRIVER_READY=R/'.local/logs/panvk-public-rc3-driver-source-ready-agent.json'
assert sha(DRIVER_READY)=='3afc572d5b8956abcfc9a4607d74217a959ffb62ab4fcdd037c1cb1a69a0caaa'
public=json.loads(DRIVER_READY.read_text())
assert public['status']=='PUBLIC_RELEASE_DRIVER_BYTES_UNMODIFIED_SOURCE_ONLY_NO_DEVICE'
assert public['driver']['sha256']==PUBLIC_DRIVER and sha(a.driver)==PUBLIC_DRIVER
for row in [public['driver'],public['associated_source_archive'],*public['public_receipts'],*public['ELF_reports']]:assert sha(row['path'])==row['sha256']
def verified_startup_capture(path,capture):
 assert sha(path)=='299d2afd5f65895ade2a0cf75cf7e71709f1e461c2ce412d477adbf35e7c3474'
 q=json.loads(path.read_text())
 assert q['controller_sha256']=='b8acf68b0befad2c7ddfc86c667c2a332227bd1bd840dd0e30223630756bb2be'
 assert q['candidate_APK']=='cc56a9849b28ec338a4a9a1941b9933cac2be689e31c116a0ea48ab175656adc'
 assert q['driver']=='c5ecf526c0113c976366818c931173f661adb7df4f519b9a2e9b6ed9456a71b2'
 assert q['apk']=='a0bc159c20acc85fe2178a73d3e9f889d23e9422c0bb883982136cafa0f7f4ff'
 assert not q['cleanup_errors'] and not q.get('capture_safety_limit')
 assert all(q[k]is True for k in('capture_stack_mapped','capture_collected','capture_settings_restored','cleanup_complete','all35_restored','all_shaders_restored','official_capture_info_valid','official_capture_convert_valid','untrimmed_prefix_verified'))
 assert q['official_info_returncode']==0 and q['official_convert_returncode']==0
 assert q['first_recorded_function']=='vkCreateInstance' and q['trim_reconstruction_commands']==[] and q['recorded_submits']
 assert q['initial_capture_bytes']>64
 assert all(q['capture_properties_effective']['debug.gfxrecon.'+k]==''for k in('capture_queue_submits','capture_frames','capture_draw_calls','capture_trigger','capture_trigger_frames','capture_trigger_boundary','capture_android_trigger'))
 raw=q['capture_raw'];assert raw['copy_verified']is True and raw['temp_removed']is True
 assert Path(raw['local_path']).resolve()==capture.resolve()and capture.stat().st_size==raw['bytes']and 64<raw['bytes']<(2<<30)
 assert sha(capture)==raw['sha256']=='cc82695545af4cdcddfb62d5c9d13ef9ef4d314167d157f76ed3bf757de69dd5'
 return q
capture_source=verified_startup_capture(a.capture_source_result,a.capture)
def verify_baseline(path,serial,apk):
 assert sha(path)=='54f8ddb73fc2392a88eb678a44acdf289d4763c1f205419224ccb64ddff4b77c'
 p=json.loads(path.read_text())
 assert p['schema']==1 and p['status']=='ROOT_ACTUAL_STOPPED_HUD55_SELECTED35_BASELINE_FOR_PUBLIC_RC3_CONTROL'
 assert p['source_revision']=='55a11129069bb0145414804eccb9787900264e4a' and p['serial']==serial
 assert p['original_APK']=='bcfdd9a495988caacdacd07627b87c237fa78cfea5798de09311d2422f44d05a'
 assert p['original_ELF']=='5ed7df9d4cd28ec0972e414ee9d584bd1dda394ad71bbe43899efbe258d64757'
 assert p['shaderchain']=='cbd8d2da0b0f' and p['original_game_process_absent']is True
 root=p['root_actual_review'];assert root['sha256']=='cc1ee119ab43cdfd694fc94617e469de7c6deb2cbc0f2bc04e5fe8c008f73852' and sha(root['path'])==root['sha256']
 q=json.loads(Path(root['path']).read_text())
 assert q['status']=='ROOT_ACTUAL_55A1112_SCALE_SELECTOR_PASS_SAVED_STOPPED' and q['runtime']==p['source_revision']
 assert q['APK_persisted']==p['original_APK'] and q['host_ELF']==p['original_ELF']
 assert all(q[k]is True for k in ('actual_original_game','original_save2','original_graphics_suspend','stopped'))
 expected=p['original_modules_resources_private_driver_hashes'];assert len(expected)==35
 for n,h in expected.items():assert n.startswith('files/runtime/')and '..'not in Path(n).parts and re.fullmatch('[a-f0-9]{64}',h)
 assert sha(apk)==p['original_APK']
 with zipfile.ZipFile(apk)as z:assert hashlib.sha256(z.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()==p['original_ELF']
 return p,expected
baseline,expected=verify_baseline(a.baseline_ready,a.serial,a.original_apk)
PROTECTED_DIRS=('files/runtime/vulkan-panfrost','files/runtime/shaders','files/runtime/romfs_overlay','files/runtime/temp','cache','files/runtime/modules')
LAYER_KEYS=('enable_gpu_debug_layers','gpu_debug_app','gpu_debug_layers','gpu_debug_layers_gles','gpu_debug_layer_app')
plan={'scope':'ONE official Arm Android GPU-call replay of frozen complete original-game startup prefix; no ARM64 game modules executed during replay; not FPS or playable-game acceptance','package':pkg,'activity':m['activity'],'APK_SHA256':m['APK']['sha256'],'manifest_SHA256':sha(a.artifact/'candidate.apk.build.json'),'controller_SHA256':sha(__file__),'capture_SHA256':sha(a.capture),'complete_startup_capture_result_SHA256':sha(a.capture_source_result),'recorded_original_submit_count':len(capture_source['recorded_submits']),'driver_SHA256':sha(a.driver),'baseline_ready_SHA256':sha(a.baseline_ready),'capture_source_driver_SHA256':capture_source['driver'],'capture_source_host_APK':capture_source['candidate_APK'],'test_driver_is_capture_origin':False,'production_host':baseline['source_revision'],'capture_VUIDs_retained':capture_source.get('VUIDs',[]),'limits':['Trace from old offscreen host, not HUD55 capture or current whole-game acceptance.','R16 VUID06479, if encountered, is retained; GFXR added extension dependency/snapshotInvalidate VUIDs are tool diagnostics, not relabeled app bugs.','Full startup prefix, not midstream trim; EOF does not prove correct pixels or shared-memory performance.'],'memory_translation':'none','official_sync_queue_submissions':True,'single_changed_variable_vs_old_sync_replay':'test_driver_c5ec_to_public_b46' ,'diagnostic_serialization_not_runtime_fix':True,'validation':'official VVL requested + synchronization env requested; actual banner/errors retained, never assume pass','timeout_seconds':90,'launches':1,'production_package_read_only':True,'full_protected_directories':list(PROTECTED_DIRS),'source_driver_ready_SHA256':sha(DRIVER_READY)}
if not a.execute:print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(exist_ok=False,parents=True);token=uuid.uuid4().hex;tmp='/data/local/tmp/panvk-gfxr-replay-'+token;run='files/run-'+token
B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial];result={**plan,'stage':'preflight','token':token,'owned_tmp':tmp,'cleanup_errors':[]};installed=False;install_requested=False;made_tmp=False;first=None;ambiguous=False;before=None;installer=None;launch=None
save=lambda:(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
def ad(*args,check=True,timeout=20):
 p=subprocess.run(B+list(map(str,args)),capture_output=True,timeout=timeout);err=p.stderr.decode(errors='replace');out=p.stdout.decode(errors='replace').strip()
 if p.returncode and(check or any(s in err.lower()for s in ('device offline','device not found','no devices','connection reset','error: closed'))):raise RuntimeError(repr(args)+': '+err+' '+out)
 return out
pid=lambda name:ad('shell','pidof',name,check=False)
def app(*args,timeout=45):
 # Same quoted run-as convention as the existing full-directory CAS helper;
 # Diablo resource paths can contain spaces. No production path is mutated.
 return ad('shell',shlex.join(['run-as',original,*map(str,args)]),timeout=timeout)
def inventory(path):
 kind=app('sh','-c','if test -L "$1"; then exit 5; fi; if test -d "$1"; then echo DIR; elif test -e "$1"; then exit 6; fi','sh',path)
 assert kind in ('','DIR')
 if not kind:return None
 assert not app('find',path,'-type','l'),'Protected directory contains symlink'
 names=app('find',path,'-type','f').splitlines()
 assert len(names)==len(set(names)) and all(n.startswith(path+'/')and '..'not in Path(n).parts for n in names)
 got={}
 for start in range(0,len(names),128):
  group=names[start:start+128];raw=app('sha256sum',*group)
  batch={line.split(None,1)[1]:line.split()[0]for line in raw.splitlines()}
  assert set(batch)==set(group)and all(re.fullmatch('[a-f0-9]{64}',v)for v in batch.values())
  got.update(batch)
 assert len(got)==len(names)
 return dict(sorted(got.items()))
def snapshot():
 assert not pid(original),'Production game running: do not alter it'
 assert not app('sh','-c','if test -e "$1"; then echo BUSY; fi','sh','files/runtime/.astc6-full-controller-lock'),'Existing production transaction'
 path=ad('shell','pm','path',original).removeprefix('package:');assert path.startswith('/')and'\n'not in path
 assert ad('shell','sha256sum',path).split()[0]==baseline['original_APK']
 assert ad('shell','sha256sum',str(Path(path).parent/'lib/arm64/libdmhost_android.so')).split()[0]==baseline['original_ELF']
 raw=app('sha256sum',*expected);got={line.split(None,1)[1]:line.split()[0]for line in raw.splitlines()};assert got==expected
 chain=app('cat','files/runtime/shaders/.chain');assert chain==baseline['shaderchain']
 dirs={path:inventory(path)for path in PROTECTED_DIRS}
 layers={key:ad('shell','settings','get','global',key)for key in LAYER_KEYS}
 return {'APK':baseline['original_APK'],'ELF':baseline['original_ELF'],'files35':got,'chain':chain,'protected_directories':dirs,'external_layers':layers,'panvk_debug_property':ad('shell','getprop','debug.mesa.panvk.debug'),'PID_absent':True}
def exact():
 p=ad('shell','pm','path',pkg,check=False).removeprefix('package:')
 if not p:return False
 assert p.startswith('/')and'\n'not in p
 assert ad('shell','sha256sum',p).split()[0]==m['APK']['sha256'],'Unclaimed independent package'
 return True
def observe(claim=True):
 global first,ambiguous
 raw=pid(pkg);history=result.setdefault('PID_raw',[])
 if not history or history[-1]!=raw:history.append(raw)
 save()
 if not raw:return raw
 if len(raw.split())!=1 or not raw.isdecimal()or(first is not None and raw!=first):ambiguous=True;result['ownership_failed']=True;save();return raw
 if first is None and claim and not ambiguous:
  status=ad('shell','cat','/proc/'+raw+'/status');uid=int(re.search(r'^Uid:\s+(\d+)',status,re.M)[1]);assert 10000<=uid<200000 and uid==int(ad('shell','run-as',pkg,'id','-u'))
  first=raw;result.update(first_PID=int(first),UID=uid);save()
 return raw
def logs():
 text=ad('exec-out','run-as',pkg,'cat',run+'/replay.log',check=False)
 sdk=ad('exec-out','run-as',pkg,'cat',run+'/gfxr.log',check=False)
 if sdk:(a.output/'official-gfxr.log').write_text(sdk)
 combined=text+('\n[unchanged official SDK log file]\n'+sdk if sdk else'')
 if combined:
  (a.output/'replay.log').write_text(combined)
  result['official_validation_diagnostics']=sorted(set(re.findall(r'(?:VUID|SYNC)-[A-Za-z0-9_-]+',combined)));save()
 return combined
def stop():
 current=observe(False)
 if current and not ambiguous and first and current==first:
  assert exact();ad('shell','am','force-stop',pkg)
 elif current:
  deadline=time.monotonic()+30
  while current and time.monotonic()<deadline:time.sleep(.2);current=observe(False)
  assert not current,'Unclaimed/crashing process left untouched'
 assert not pid(pkg);result['fixture_PID_absent']=True;save()
def install():
 global installer
 with (a.output/'install.txt').open('w')as f:
  installer=subprocess.Popen(B+['install','--no-incremental',str(a.artifact/'candidate.apk')],stdout=f,stderr=subprocess.STDOUT);deadline=time.monotonic()+70;clicked=set()
  while installer.poll()is None:
   if time.monotonic()>deadline:raise TimeoutError('Single installation pending; no retry')
   remote=tmp+'/installer.xml';ad('shell','uiautomator','dump',remote)
   text=ad('exec-out','cat',remote)
   try:tree=ET.fromstring(text)
   except ET.ParseError:tree=None
   if tree is not None and any(n.get('text')=='PanVK GFXR diagnostic'for n in tree.iter('node')):
    nodes=[n for n in tree.iter('node')if(n.get('package')=='com.android.packageinstaller'and n.get('resource-id')=='android:id/button1'and n.get('text')in('Install','安装','继续安装'))or(n.get('package')=='com.oplus.appdetail'and n.get('resource-id','').endswith('/btn_left')and n.get('text')=='Install')]
    if len(nodes)==1:
     key=nodes[0].get('package')+':'+nodes[0].get('resource-id')
     if key not in clicked:
      (a.output/('installer-'+str(len(clicked))+'.xml')).write_text(text);x,y,z,w=map(int,re.findall(r'\d+',nodes[0].get('bounds')));ad('shell','input','tap',(x+z)//2,(y+w)//2);clicked.add(key)
   time.sleep(.4)
  assert installer.returncode==0,(a.output/'install.txt').read_text()
def interrupt(sig,frame):raise KeyboardInterrupt('Owned replay cleanup')
signal.signal(signal.SIGINT,interrupt);signal.signal(signal.SIGTERM,interrupt);save()
try:
 before=snapshot();result['original_before']=before;save();assert not ad('shell','pm','path',pkg,check=False)and not pid(pkg)
 enabled=ad('shell','settings','get','global','enable_gpu_debug_layers');target=ad('shell','settings','get','global','gpu_debug_app');assert not(enabled=='1'and target==pkg)
 result['external_layers_untouched']={'enabled':enabled,'app':target};save()
 ad('shell','mkdir',tmp);made_tmp=True;install_requested=True;install();installed=True;assert exact()and not pid(pkg)
 ad('push',a.driver,tmp+'/driver.so',timeout=45);ad('push',a.capture,tmp+'/capture.gfxr',timeout=45)
 assert ad('shell','sha256sum',tmp+'/driver.so').split()[0]==sha(a.driver)and ad('shell','sha256sum',tmp+'/capture.gfxr').split()[0]==sha(a.capture)
 ad('shell','run-as',pkg,'mkdir','-p','files/driver');ad('shell','run-as',pkg,'cp',tmp+'/driver.so','files/driver/libvulkan_panfrost.so');ad('shell','run-as',pkg,'cp',tmp+'/capture.gfxr','files/capture.gfxr')
 assert ad('shell','run-as',pkg,'sha256sum','files/driver/libvulkan_panfrost.so').split()[0]==sha(a.driver)and ad('shell','run-as',pkg,'sha256sum','files/capture.gfxr').split()[0]==sha(a.capture)
 args='--sync --validate --log-file /data/user/0/'+pkg+'/'+run+'/gfxr.log --memory-translation none /data/user/0/'+pkg+'/files/capture.gfxr'
 cmd=['shell','am','start','-W','-n',pkg+'/'+m['activity'],'--es','fixture_token',token,'--es','args',shlex.quote(args)]
 result.update(stage='one-cold-replay',launch_command=B+cmd);save()
 with (a.output/'launch.txt').open('w')as f:launch=subprocess.Popen(B+cmd,stdout=f,stderr=subprocess.STDOUT)
 deadline=time.monotonic()+90;log=''
 while True:
  log=logs();current=observe();terminal='====== Exiting android_main'in log
  if terminal or ambiguous or(first and not current):break
  if time.monotonic()>deadline:raise TimeoutError('Replay bound reached, no retry')
  time.sleep(.2)
 launch.wait(timeout=5)
 # SDK prints the exit marker before Activity cleanup/SIGTERM. Prefer natural
 # exit; only an exact owned process can be stopped after this bounded grace.
 if terminal and not ambiguous:
  natural=time.monotonic()+8
  while observe(False)and time.monotonic()<natural:time.sleep(.2)
 stop();log=logs()
 assert first and not ambiguous,'Replay PID ownership failed'
 # Retain crash/SDK console messages for this exact PID without clearing logcat.
 result['replay_return_marker']=terminal
 assert '[replay bridge] configured pid='+first+' 'in log,'Startup PID must match exact observed process'
 assert 'identity verified driverID=20'in log and 'DRIVER_CUSTOM only, GPU_MAPPING_IMPORT=off'in log,'Private ICD identity/standard loader absent'
 result['official_validation_diagnostics']=sorted(set(re.findall(r'(?:VUID|SYNC)-[A-Za-z0-9_-]+',log)));save()
 assert terminal,'Official replay did not reach SDK EOF/return'
 assert 'File did not contain any frames'in log,'Expected offscreen no-Present startup capture EOF'
 assert not any(x in log for x in ('Replay failed','fatal error and cannot continue','[replay bridge] FATAL','VK_ERROR_DEVICE_LOST'))
 errors=sorted(set(re.findall(r'(?:VUID|SYNC)-[A-Za-z0-9_-]+',log)));result['official_validation_diagnostics']=errors
 result.update(stage='official-replay-EOF-with-validation-diagnostics'if errors else'official-replay-EOF',replay_only_not_game_acceptance=True,performance_claim=False)
except BaseException as e:result.update(stage='failed-preserved',error=repr(e))
finally:
 for client in(installer,launch):
  if client is not None and client.poll()is None:
   client.terminate()
   try:client.wait(timeout=3)
   except subprocess.TimeoutExpired:client.kill();client.wait(timeout=3)
 try:
  if install_requested and not installed:installed=exact()
 except BaseException as e:result['cleanup_errors'].append('installed identity: '+repr(e))
 if installed:
  for name,action in [('replay.log',logs),('PID-logcat.txt',lambda:ad('logcat','-d','--pid='+first,'-v','threadtime',timeout=30)if first else''),('mesa.log',lambda:ad('exec-out','run-as',pkg,'cat',run+'/mesa.log',check=False))]:
   try:
    text=action()
    if text:(a.output/name).write_text(text)
   except BaseException as e:result['cleanup_errors'].append(name+': '+repr(e))
  try:
   stop();assert exact();ad('uninstall',pkg);assert not ad('shell','pm','path',pkg,check=False);result['fixture_uninstalled']=True
  except BaseException as e:result['cleanup_errors'].append('owned package cleanup: '+repr(e))
 if made_tmp:
  try:ad('shell','rm','-r',tmp);ad('shell','test','!','-e',tmp);result['owned_tmp_absent']=True
  except BaseException as e:result['cleanup_errors'].append('unique tmp cleanup: '+repr(e))
 result['owned_clients_stopped']=all(p is None or p.poll()is not None for p in(installer,launch))
 if before is not None:
  try:result['original_after']=snapshot();assert result['original_after']==before;result['production_unchanged']=True
  except BaseException as e:result['cleanup_errors'].append('production read-only verification: '+repr(e))
 save()
print(json.dumps(result));raise SystemExit(0 if result['stage']=='official-replay-EOF'and not result['cleanup_errors']else 1)
