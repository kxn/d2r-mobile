#!/usr/bin/env python3
"""Single official Android GPU-call replay, independent ordinary UID. Plan by default.
Reuses the frozen independent-fixture PID/identity/cleanup protocol. No game,
API logger, sampler, system driver replacement, or automatic retries.
"""
import argparse,hashlib,json,re,subprocess,time,uuid,zipfile,signal,shlex,xml.etree.ElementTree as ET
from pathlib import Path
R=Path('/home/dl/d2r-mobile');P=argparse.ArgumentParser()
for n in ('serial','artifact','driver','capture','capture-source-result','baseline-promotion','original-apk','output'):P.add_argument('--'+n,required=True,type=str if n=='serial'else Path)
P.add_argument('--execute',action='store_true');a=P.parse_args()
pkg='com.d2rmobile.panvkgfxrfixture';original='com.d2rmobile.nativeport';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((a.artifact/'candidate.apk.build.json').read_text());promotion=json.loads(a.baseline_promotion.read_text())
assert m['package']==pkg and m['SDK_replay_native_unchanged']is True and m['game_modules_or_renderer_included']is False
assert sha(a.artifact/'candidate.apk')==m['APK']['sha256']
for row in m['sources']+m['actual_compile_inputs']+m['actual_tools']:assert sha(row['path'])==row['sha256'],row['path']
with zipfile.ZipFile(a.artifact/'candidate.apk')as z:assert {Path(n).name:hashlib.sha256(z.read(n)).hexdigest()for n in z.namelist()if n.startswith('lib/arm64-v8a/')}==m['native_libraries8']
assert len(m['native_libraries8'])==8
assert sha(a.driver)=='c5ecf526c0113c976366818c931173f661adb7df4f519b9a2e9b6ed9456a71b2'
def verified_startup_capture(path,capture):
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
 assert sha(capture)==raw['sha256']
 return q
capture_source=verified_startup_capture(a.capture_source_result,a.capture)
assert promotion['production_promoted_and_stopped']and promotion['fresh_save_then_stopped']and not promotion['rollback_errors']
expected=promotion['promoted_module_and_resource_hashes'];assert len(expected)==35
assert sha(a.original_apk)==promotion['canonical_APK_SHA256']
with zipfile.ZipFile(a.original_apk)as z:assert hashlib.sha256(z.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()==promotion['host_ELF_SHA256']
for n,h in expected.items():assert not Path(n).is_absolute()and '..'not in Path(n).parts and re.fullmatch('[a-f0-9]{64}',h)
plan={'scope':'ONE official Arm Android GPU-call replay of frozen complete original-game startup prefix; no ARM64 game modules executed during replay; not FPS or playable-game acceptance','package':pkg,'activity':m['activity'],'APK_SHA256':m['APK']['sha256'],'manifest_SHA256':sha(a.artifact/'candidate.apk.build.json'),'controller_SHA256':sha(__file__),'capture_SHA256':sha(a.capture),'complete_startup_capture_result_SHA256':sha(a.capture_source_result),'recorded_original_submit_count':len(capture_source['recorded_submits']),'driver_SHA256':sha(a.driver),'baseline_promotion_SHA256':sha(a.baseline_promotion),'memory_translation':'none','official_sync_queue_submissions':True,'single_changed_replay_option':'--serialize-render-passes','official_serialize_render_passes':True,'diagnostic_serialization_not_runtime_fix':True,'validation':'official VVL requested + synchronization env requested; actual banner/errors retained, never assume pass','timeout_seconds':90,'launches':1,'production_package_read_only':True}
if not a.execute:print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(exist_ok=False,parents=True);token=uuid.uuid4().hex;tmp='/data/local/tmp/panvk-gfxr-replay-'+token;run='files/run-'+token
B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial];result={**plan,'stage':'preflight','token':token,'owned_tmp':tmp,'cleanup_errors':[]};installed=False;install_requested=False;made_tmp=False;first=None;ambiguous=False;before=None;installer=None;launch=None
save=lambda:(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
def ad(*args,check=True,timeout=20):
 p=subprocess.run(B+list(map(str,args)),capture_output=True,timeout=timeout);err=p.stderr.decode(errors='replace');out=p.stdout.decode(errors='replace').strip()
 if p.returncode and(check or any(s in err.lower()for s in ('device offline','device not found','no devices','connection reset','error: closed'))):raise RuntimeError(repr(args)+': '+err+' '+out)
 return out
pid=lambda name:ad('shell','pidof',name,check=False)
def inventory():
 # Android toybox find -exec '+' can exceed the kernel's argument ceiling for
 # a large shader cache. Preserve every filename, hash bounded128 batches.
 names=ad('shell','run-as',original,'find','files/runtime/vulkan-panfrost','files/runtime/shaders','-type','f',timeout=45).splitlines()
 assert len(names)==len(set(names)) and names
 assert all(re.fullmatch(r'[A-Za-z0-9_./+-]+',n) and '..'not in Path(n).parts for n in names)
 got={}
 for start in range(0,len(names),128):
  group=names[start:start+128];raw=ad('exec-out','run-as',original,'sha256sum',*group,timeout=45)
  batch={line.split(None,1)[1]:line.split()[0]for line in raw.splitlines()}
  assert set(batch)==set(group) and all(re.fullmatch('[a-f0-9]{64}',v)for v in batch.values())
  got.update(batch)
 assert len(got)==len(names)
 return dict(sorted(got.items()))
def snapshot():
 assert not pid(original),'Production game running: do not alter it'
 path=ad('shell','pm','path',original).removeprefix('package:');assert path.startswith('/')and'\n'not in path
 assert ad('shell','sha256sum',path).split()[0]==promotion['canonical_APK_SHA256']
 raw=ad('exec-out','run-as',original,'sha256sum',*expected);got={line.split(None,1)[1]:line.split()[0]for line in raw.splitlines()};assert got==expected
 chain=ad('exec-out','run-as',original,'cat','files/runtime/shaders/.chain');assert chain=='cbd8d2da0b0f'
 inv=inventory()
 return {'APK':promotion['canonical_APK_SHA256'],'ELF':promotion['host_ELF_SHA256'],'files35':got,'chain':chain,'driver_shader_inventory':inv,'PID_absent':True}
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
 if combined:(a.output/'replay.log').write_text(combined)
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
 args='--sync --serialize-render-passes --validate --log-file /data/user/0/'+pkg+'/'+run+'/gfxr.log --memory-translation none /data/user/0/'+pkg+'/files/capture.gfxr'
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
