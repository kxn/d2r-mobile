#!/usr/bin/env python3
"""One ordinary-UID official VVL/private PanVK entry check; no game, profiler or retries."""
import argparse, hashlib, io, json, re, subprocess, time, uuid, signal, zipfile, xml.etree.ElementTree as ET
from pathlib import Path
P=argparse.ArgumentParser()
P.add_argument('--serial',required=True)
P.add_argument('--artifact',type=Path,required=True)
P.add_argument('--driver',type=Path,required=True)
P.add_argument('--baseline-promotion',type=Path,required=True)
P.add_argument('--original-apk',type=Path,required=True)
P.add_argument('--expected-shaderchain')
P.add_argument('--output',type=Path,required=True)
# Only one official-layer entry workload; no mode or driver flag sweep.
P.add_argument('--execute',action='store_true')
a=P.parse_args();R=Path(__file__).resolve().parents[2]
pkg='com.d2rmobile.panvklayervvlfixture';original='com.d2rmobile.nativeport'
B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((a.artifact/'candidate.apk.build.json').read_text())
assert m['package']==pkg and m['activity']=='com.d2rmobile.nativeport.MainActivity'
assert sha(a.artifact/'candidate.apk')==m['apk_sha256']
with zipfile.ZipFile(a.artifact/'candidate.apk') as archive:
 for leaf,digest in m['native_library_sha256'].items():
  assert hashlib.sha256(archive.read('lib/arm64-v8a/'+leaf)).hexdigest()==digest
assert len(m['native_library_sha256'])==7 and 'libVkLayer_khronos_validation.so' in m['native_library_sha256']
assert m['fixture_kind']=='official-VVL-private-PanVK-entry-v1'
assert sha(a.artifact/'probe-source-ready.json')==m['probe_source_ready_sha256']
assert sha(a.artifact/'fixture-hashes.json')==m['fixture_hashes_sha256']
fixture=json.loads((a.artifact/'fixture-hashes.json').read_text())
assert fixture and all(re.fullmatch('[A-Za-z0-9_.-]+',n) and n not in ('.','..') and re.fullmatch('[0-9a-f]{64}',v) for n,v in fixture.items())
assert {p.name for p in (a.artifact/'vulkan-fixture').iterdir()}==set(fixture)
for name,digest in fixture.items():
 assert (a.artifact/'vulkan-fixture'/name).is_file() and not (a.artifact/'vulkan-fixture'/name).is_symlink()
 assert sha(a.artifact/'vulkan-fixture'/name)==digest
assert sha(a.driver)=='c5ecf526c0113c976366818c931173f661adb7df4f519b9a2e9b6ed9456a71b2'
promotion=json.loads(a.baseline_promotion.read_text())
assert promotion['production_promoted_and_stopped'] is True and promotion['default_mode_verified'] is True and promotion['rollback_errors']==[]
assert promotion['fresh_save_then_stopped'] is True
a.expected_original_apk=promotion['canonical_APK_SHA256'];a.expected_original_elf=promotion['host_ELF_SHA256']
assert sha(a.original_apk)==a.expected_original_apk
with zipfile.ZipFile(a.original_apk) as original_archive:
 assert hashlib.sha256(original_archive.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()==a.expected_original_elf
expected=promotion['promoted_module_and_resource_hashes'];assert len(expected)==35
assert sum(n.startswith('files/runtime/modules/') for n in expected)==28
assert sum(n.startswith('files/runtime/romfs_overlay/') for n in expected)==6
assert expected['files/runtime/vulkan-panfrost/libvulkan_panfrost.so']=='1739bcaf02c306988e591ac056eb41dfcf59d412460b9f8a26a737176919018a'
for name,digest in expected.items():assert not Path(name).is_absolute() and '..' not in Path(name).parts and re.fullmatch('[a-f0-9]{64}',digest)
a.expected_shaderchain=a.expected_shaderchain or 'cbd8d2da0b0f'
assert re.fullmatch('[a-f0-9]{12,64}',a.expected_shaderchain)
baseline_bound=True
plan={'scope':'ONE installation, ONE official VVL correct GPU byte + deliberately invalid API check; game=false; no Surface; original package read-only; original private c5ec via Android loader; no performance claim',
 'serial':a.serial,'package':pkg,'activity':m['activity'],'APK_sha256':m['apk_sha256'],
 'native_libraries':m['native_library_sha256'],
 'artifact_manifest_sha256':sha(a.artifact/'candidate.apk.build.json'),
 'probe_source_revision':m['probe_source_revision'],'probe_source_ready_sha256':m['probe_source_ready_sha256'],
 'fixture_hashes_sha256':m['fixture_hashes_sha256'],'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),
 'expected_original_APK':a.expected_original_apk,'expected_original_ELF':a.expected_original_elf,'baseline_promotion_sha256':sha(a.baseline_promotion),'original_local_apk_sha256':sha(a.original_apk),'module_count':28,'resource_count':6,
 'baseline_bound':baseline_bound,'expected_shaderchain':a.expected_shaderchain,
 'crash_natural_exit_bound_seconds':30,'functional_timeout_seconds':45,'installation_timeout_seconds':70,'launches_allowed':1,'modes_once':['official-vvl'],'GUP_unprotect_retry':False,
 'writes_only':'new independent package + unique /data/local/tmp directory; cleanup exact package only'}
if not a.execute:
 print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(exist_ok=False,parents=True)
tmp='/data/local/tmp/dm-panvk-vvl-entry-'+uuid.uuid4().hex
result={**plan,'stage':'preflight','cleanup_errors':[]};made_tmp=False;installation_requested=False;installed=False;launched=False;installer=None;launch=None;original_before=None;first_pid=None;active_phase=None
result['owned_tmp']=tmp

def save():(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
def ad(*args,check=True):
 p=subprocess.run(B+list(map(str,args)),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15)
 error=p.stderr.decode(errors='replace')
 if p.returncode and (check or any(x in error.lower() for x in ('device offline','no devices','device not found','connection reset','error: closed'))):
  raise RuntimeError(repr(args)+': '+error)
 return p.stdout.decode(errors='replace').strip()
def process(name):return ad('shell','pidof',name,check=False)
def snapshot():
 assert not process(original),'Original game is running; leave it untouched'
 path=ad('shell','pm','path',original).removeprefix('package:')
 assert path.startswith('/') and '\n' not in path
 digest=ad('shell','sha256sum',path).split()[0]
 assert digest==a.expected_original_apk,'Original APK changed since lease: do not install'
 # Read-only original APK extraction on the host; no original private write.
 raw_apk=a.original_apk.read_bytes();assert hashlib.sha256(raw_apk).hexdigest()==digest,'Original local APK hash mismatch'
 with zipfile.ZipFile(io.BytesIO(raw_apk)) as archive:
  elf=hashlib.sha256(archive.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()
 assert elf==a.expected_original_elf,'Original native ELF changed since lease' 
 raw=ad('exec-out','run-as',original,'sha256sum',*expected)
 got={line.split(None,1)[1].strip():line.split(None,1)[0] for line in raw.splitlines()}
 assert got==expected,'Original module baseline mismatch'
 chain=ad('exec-out','run-as',original,'cat','files/runtime/shaders/.chain')
 assert chain==a.expected_shaderchain,'Original shaderchain changed since lease'
 return {'APK':digest,'ELF':elf,'modules_and_resources':got,'shaderchain':chain,'PID_absent':True}
def exact_fixture():
 path=ad('shell','pm','path',pkg,check=False).removeprefix('package:')
 if not path:return False
 assert path.startswith('/') and '\n' not in path
 assert ad('shell','sha256sum',path).split()[0]==m['apk_sha256'],'Unclaimed package; leave untouched'
 return True
# PID/crash ownership helpers adapted from frozen5c185ca matrix controller.
# Final JSON has no PID and cannot rescue ambiguous/replacement ownership.
def read_oracle_log(phase):
 log=ad('exec-out','run-as',pkg,'cat','files/runtime/vulkan-test.log',check=False)
 if log:(a.output/(phase['name']+'-vulkan-test.log')).write_text(log)
 return log

def parse_oracle_log(log):
 rows=[]
 for line in log.splitlines():
  if line.startswith('{'):
   try:rows.append(json.loads(line))
   except json.JSONDecodeError:pass
 if 'FATAL' in log or 'native fault signal=' in log:return rows,'native-assertion-failed',None
 finals=[r for r in rows if r.get('test')==active_phase['expected_test']]
 if finals:
  final=finals[-1]
  if final.get('result')!='passed' or any(r.get('native_vulkan_app_terminal') is True for r in rows):
   return rows,'oracle-'+final.get('result','unknown'),final
 return rows,None,None

def validate_result(log,rows,final,first_pid):
 configurations=[r for r in rows if r.get('native_vulkan_app_configuration') is True]
 assert len(configurations)==1
 configuration=configurations[0]
 assert 10000<=configuration['uid']<200000 and configuration['pid']==int(first_pid)
 assert configuration['validation_layer']=='requested' and configuration['synchronization_validation']=='requested'
 terminals=[r for r in rows if r.get('native_vulkan_app_terminal') is True]
 assert len(terminals)==1 and terminals[0]['rc']==0 and terminals[0]['pid']==int(first_pid)
 assert 'PanVK layered private ICD mapping verified' in log and 'driverID=20' in log
 assert 'GPU_MAPPING_IMPORT=off' in log
 assert final['driverID']==20 and final['private_ICD_verified'] is True
 assert final['positive_GPU_bytes'] is True and final['positive_VUID_count']==0
 assert final['negative_VUID']=='VUID-VkBufferCreateInfo-size-00912' and final['negative_count']==1
 assert final['invalid_call_blocked'] is True
 errors=re.findall(r'\[vulkan validation\] (VUID-[^: ]+):',log)
 assert errors==['VUID-VkBufferCreateInfo-size-00912'],'Unexpected/missing official VVL diagnostics'
 return configuration

def observe_pid(phase,allow_claim=True):
 global first_pid
 current=process(pkg)
 # Persist raw text before interpreting or rejecting anything. Crash dump/fork
 # transients must never erase the failed oracle or become another owned PID.
 raw=phase.setdefault('pid_raw',[])
 if not raw or raw[-1]!=current:raw.append(current)
 phase['last_pid_raw']=current;save()
 if not current:return current,'absent'
 tokens=current.split()
 if len(tokens)!=1 or not current.isdecimal():
  phase['ownership_ambiguous']=True;phase['pid_anomaly']='multiple-or-invalid';save()
  return current,'ambiguous'
 if phase.get('ownership_ambiguous'):return current,'ambiguous'
 if first_pid is None:
  if not allow_claim:return current,'unclaimed'
  first_pid=current;phase['first_pid']=int(current);save()
 elif current!=first_pid:
  phase['ownership_ambiguous']=True;phase['pid_anomaly']='replacement';save()
  return current,'replacement'
 return current,'owned'

def wait_natural_exit(phase):
 # Functional crash cleanup only; no process is claimed or force-stopped here.
 wait=phase.setdefault('natural_exit',{'bound_seconds':30,'all_PIDs_absent':False})
 if wait.get('timed_out'):raise TimeoutError('Unclaimed/crashing PIDs remain; no force-stop or repeated wait')
 deadline=time.monotonic()+30
 while True:
  log=read_oracle_log(phase)
  rows,stage,final=parse_oracle_log(log);phase['rows']=rows
  if stage=='native-assertion-failed':phase['native_terminal_seen']=True
  current,kind=observe_pid(phase,allow_claim=False)
  if not current:
   log=read_oracle_log(phase);rows,stage,final=parse_oracle_log(log);phase['rows']=rows
   if stage=='native-assertion-failed':phase['native_terminal_seen']=True
   wait['all_PIDs_absent']=True;save();return log
  if time.monotonic()>=deadline:
   wait['timed_out']=True;save()
   raise TimeoutError('Crash/ambiguous PIDs did not naturally exit; leave them untouched')
  time.sleep(.2)

def stop_owned(phase=None):
 phase=phase if phase is not None else active_phase
 if phase is None:
  current=process(pkg);result['cleanup_pid_raw']=current;save()
  assert not current,'No owned launch phase; preserve unclaimed process'
  return
 current,kind=observe_pid(phase,allow_claim=False)
 if current and (phase.get('native_terminal_seen') or kind!='owned'):
  wait_natural_exit(phase);return
 if current:
  assert first_pid is not None and current==first_pid
  assert exact_fixture();ad('shell','am','force-stop',pkg)
 assert not process(pkg)

def install():
 global installer
 with (a.output/'install.txt').open('w') as log:
  installer=subprocess.Popen(B+['install','--no-incremental',str(a.artifact/'candidate.apk')],stdout=log,stderr=subprocess.STDOUT)
  deadline=time.monotonic()+70;approvals=set()
  while installer.poll() is None:
   if time.monotonic()>deadline:raise TimeoutError('single install pending; no retry')
   # Existing Android/Oplus package-installer approval, only this pending new
   # app install. No packageinstaller kill/service/permission changes.
   remote=tmp+'/installer.xml'
   ad('shell','uiautomator','dump',remote)
   xml=ad('exec-out','cat',remote)
   try:tree=ET.fromstring(xml)
   except ET.ParseError:tree=None
   if tree is not None:
    safe=any(n.get('text')=='D2R Mobile' for n in tree.iter('node'))
    nodes=[n for n in tree.iter('node') if
      (n.get('package')=='com.android.packageinstaller' and n.get('resource-id')=='android:id/button1' and n.get('text') in ('Install','安装','继续安装')) or
      (n.get('package')=='com.oplus.appdetail' and n.get('resource-id','').endswith('/btn_left') and n.get('text')=='Install')]
    if safe and len(nodes)==1:
     key=nodes[0].get('package')+':'+nodes[0].get('resource-id')
     if key not in approvals:
      (a.output/('installer-'+str(len(approvals))+'.xml')).write_text(xml)
      x,y,z,w=map(int,re.findall(r'\d+',nodes[0].get('bounds')))
      ad('shell','input','tap',str((x+z)//2),str((y+w)//2));approvals.add(key)
   time.sleep(.5)
  assert installer.returncode==0,(a.output/'install.txt').read_text()
def interrupted(sig,frame):raise KeyboardInterrupt('fixture interrupted; owned cleanup')
signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
save() # Persist the precise unique tmp before any device-side mutation.
try:
 original_before=snapshot();result['original_before']=original_before;save()
 assert not ad('shell','pm','path',pkg,check=False),'Existing independent fixture: do not replace'
 assert not process(pkg)
 layers=ad('shell','settings','get','global','enable_gpu_debug_layers')
 layer_app=ad('shell','settings','get','global','gpu_debug_app')
 assert not (layers=='1' and layer_app==pkg),'Fixture targeted by external GPU layers; leave settings untouched'
 result['external_layer_preflight']={'enabled':layers,'app':layer_app};save()
 ad('shell','mkdir','-p',tmp);made_tmp=True;installation_requested=True
 install();installed=True;assert exact_fixture()
 assert not process(pkg),'Installer unexpectedly opened fixture; no test launch'
 ad('push',a.driver,tmp+'/libvulkan_panfrost.so')
 ad('shell','run-as',pkg,'mkdir','-p','files/runtime/vulkan-panfrost','files/runtime/vulkan-fixture')
 ad('shell','run-as',pkg,'cp',tmp+'/libvulkan_panfrost.so','files/runtime/vulkan-panfrost/libvulkan_panfrost.so')
 assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/vulkan-panfrost/libvulkan_panfrost.so').split()[0]==sha(a.driver)
 # Complete owned fixture inventory, exact local/remote hashes; no game SPVs.
 ad('push',a.artifact/'vulkan-fixture',tmp+'/vulkan-fixture')
 ad('shell','run-as',pkg,'cp','-R',tmp+'/vulkan-fixture/.','files/runtime/vulkan-fixture/')
 raw=ad('exec-out','run-as',pkg,'sha256sum',*['files/runtime/vulkan-fixture/'+n for n in fixture])
 got={line.split(None,1)[1].strip().removeprefix('files/runtime/vulkan-fixture/'):line.split()[0] for line in raw.splitlines()}
 assert got==fixture,'Fixture bytes mismatch'
 result['runs']=[]
 for kind,test_name in [('official-vvl','official-VVL-private-ICD-entry')]:
  assert exact_fixture() and not process(pkg),'Each mode is a fresh owned process, never a warm reselection'
  ad('shell','run-as',pkg,'rm','-f','files/runtime/vulkan-test.log')
  args=['shell','am','start','-W','-n',pkg+'/'+m['activity'],'--ez','run_game','false',
   '--ez','run_vulkan_tests','false','--ez','vulkan_layer_probe','true',
   '--es','vulkan_provider','panvk-layered-debug']
  first_pid=None;run={'name':kind,'kind':kind,'expected_test':test_name,'launch_command':B+args,'launches':1};active_phase=run
  result['runs'].append(run);result.update(stage='one-cold-'+kind,launches=len(result['runs']));save();launched=True
  with (a.output/(kind+'-launch.txt')).open('w') as launch_log:launch=subprocess.Popen(B+args,stdout=launch_log,stderr=subprocess.STDOUT)
  deadline=time.monotonic()+45;log='';rows=[];phase=run
  while True:
   # Retain oracle bytes/FATAL before examining transient crash PID lists.
   log=read_oracle_log(phase);rows,stage,final=parse_oracle_log(log);phase['rows']=rows
   if stage=='native-assertion-failed':phase['native_terminal_seen']=True;phase['stage']=stage
   current,pid_kind=observe_pid(phase,allow_claim=not phase.get('native_terminal_seen',False))
   if stage=='native-assertion-failed' or pid_kind in ('ambiguous','replacement','unclaimed'):
    log=wait_natural_exit(phase);rows,stage,final=parse_oracle_log(log)
    # Final JSON has no PID: after lost ownership a pass cannot be
    # attributed to this launch. Only preserve an explicit native failure.
    phase['stage']='native-assertion-failed' if stage=='native-assertion-failed' else 'ownership-failed'
    phase['terminal_after_unowned_wait']=stage
    if final is not None:phase['untrusted_final']=final
    break
   if stage is not None:
    if final is not None and first_pid is None:
     phase['stage']='ownership-failed';phase['untrusted_final']=final
    else:
     phase['stage']=stage
     if final is not None:phase['final']=final
    break
   if first_pid is not None and not current:
    # Re-read after exit so the last flushed FATAL/final row is not discarded.
    log=read_oracle_log(phase);rows,stage,final=parse_oracle_log(log)
    phase['stage']=stage or 'exited-without-final'
    if stage=='native-assertion-failed':phase['native_terminal_seen']=True
    if final is not None:phase['final']=final
    break
   if time.monotonic()>=deadline:phase['stage']='functional-bound-reached';break
   time.sleep(.2)
  launch.wait(timeout=5);run['results']=rows;save()
  assert run.get('stage')=='oracle-passed','Original failure or lost PID ownership; no later mode/retry'
  final=run['final']
  assert final['result']=='passed' and isinstance(final.get('checks'),int) and final['checks']>0
  assert final['game_modules_executed'] is False
  configuration=validate_result(log,rows,final,first_pid)
  run.update(actual_configuration=configuration,result='passed');save()
  stop_owned(run)
  assert not process(pkg),'Owned process must stop between cold modes'
  run['PID_absent_after']=True;save()
 result.update(stage='actual-official-VVL-private-PanVK-entry-passed',
  no_game_acceptance=True,performance_claim=False)

except BaseException as e:result.update(stage='failed-preserved',error=repr(e))
finally:
 try:
  for owned_client in (installer,launch):
   if owned_client is not None and owned_client.poll() is None:
    owned_client.terminate()
    try:owned_client.wait(timeout=3)
    except subprocess.TimeoutExpired:owned_client.kill();owned_client.wait(timeout=3)
  if installation_requested and not installed:installed=exact_fixture()
  if installed:
   try:(a.output/'app-events.txt').write_text(ad('logcat','-d','-v','threadtime','-s','AndroidRuntime:E','DEBUG:E','libc:E','MESA:I'))
   except BaseException as capture_error:result['cleanup_errors'].append('capture: '+repr(capture_error))
   stop_owned();assert not process(pkg);result['fixture_PID_absent']=True
   assert exact_fixture();ad('uninstall',pkg)
   assert not ad('shell','pm','path',pkg,check=False);result['fixture_uninstalled']=True
  if made_tmp:
   ad('shell','rm','-r',tmp);ad('shell','test','!','-e',tmp);result['fixture_tmp_absent']=True
  result['owned_clients_stopped']=all(client is None or client.poll() is not None for client in (installer,launch))
  if original_before is not None:
   result['original_after']=snapshot();assert result['original_after']==original_before
   result['original_unchanged']=True
 except BaseException as e:result['cleanup_errors'].append(repr(e))
 save()
print(json.dumps(result))
raise SystemExit(0 if result['stage']=='actual-official-VVL-private-PanVK-entry-passed' and not result['cleanup_errors'] else 1)
