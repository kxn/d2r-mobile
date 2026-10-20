#!/usr/bin/env python3
"""Two cold owned native-image modes; strict package/PID/provenance transaction, no game or profiling."""
import argparse, hashlib, io, json, re, subprocess, time, uuid, signal, zipfile, xml.etree.ElementTree as ET
from pathlib import Path
P=argparse.ArgumentParser()
P.add_argument('--serial',required=True)
P.add_argument('--artifact',type=Path,required=True)
P.add_argument('--driver',type=Path,required=True)
P.add_argument('--resource-input',type=Path,required=True)
P.add_argument('--original-artifact',type=Path,help='exact locally cached original APK, verified by remote package SHA; no large exec-out pull')
P.add_argument('--module-report',type=Path)
P.add_argument('--resource-report',type=Path)
P.add_argument('--expected-original-apk')
P.add_argument('--expected-original-elf')
P.add_argument('--expected-shaderchain')
P.add_argument('--output',type=Path,required=True)
P.add_argument('--execute',action='store_true')
a=P.parse_args();R=Path(__file__).resolve().parents[2]
pkg='com.d2rmobile.turniprealproducer';original='com.d2rmobile.nativeport'
B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((a.artifact/'candidate.apk.build.json').read_text())
assert m['package']==pkg and m['activity']=='com.d2rmobile.nativeport.MainActivity'
assert sha(a.artifact/'candidate.apk')==m['apk_sha256']
with zipfile.ZipFile(a.artifact/'candidate.apk') as archive:
 for leaf,digest in m['native_library_sha256'].items():
  assert hashlib.sha256(archive.read('lib/arm64-v8a/'+leaf)).hexdigest()==digest
assert len(m['native_library_sha256'])==6
assert sha(a.driver)=='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'
resource_hashes={'resource-input.bin':'d078631c7797b699200230a09533c9ad394a40fb9244fe0f75d905974ca49354','resource-source.json':'5d2e767a618a805d412f86c70be6f3c926897e5085621461390386f368e6d269'}
assert all(sha(a.resource_input/n)==h for n,h in resource_hashes.items())
baseline_bound=bool(a.original_artifact and a.module_report and a.expected_original_apk and a.expected_original_elf and a.expected_shaderchain)
if a.execute:assert baseline_bound,'Execution requires local exact APK/ELF/28modules/shaderchain bindings'
if a.expected_original_elf:assert re.fullmatch('[0-9a-f]{64}',a.expected_original_elf)
if a.expected_original_apk:assert re.fullmatch('[0-9a-f]{64}',a.expected_original_apk)
if a.expected_shaderchain:assert re.fullmatch('[0-9a-f]{12,64}',a.expected_shaderchain)
expected={}
if a.module_report:
 deployment=json.loads(a.module_report.read_text())
 assert deployment['verified_on_device'] and deployment['serial']==a.serial and deployment['package']==original
 assert len(deployment['artifacts'])==28
 for name,item in deployment['artifacts'].items():
  assert re.fullmatch('[A-Za-z0-9_.-]+',name) and name not in ('.','..')
  assert re.fullmatch('[0-9a-f]{64}',item['sha256'])
  expected['files/runtime/modules/'+name]=item['sha256']
if a.resource_report:
 resources=json.loads(a.resource_report.read_text())
 assert resources['stage']=='deployed' and resources['phone_hashes_verified'] and len(resources['expected_hashes'])>0
 for name,digest in resources['expected_hashes'].items():
  assert re.fullmatch('[A-Za-z0-9_./-]+',name) and not Path(name).is_absolute() and '..' not in Path(name).parts and re.fullmatch('[0-9a-f]{64}',digest)
  expected['files/runtime/romfs_overlay/'+name]=digest
plan={'scope':'ONE installation, ONE cold real firepit resource producer mode; HOST copy only, GPU sampling still pending, each once; game=false; original package read-only; VVL explicitly unavailable with direct ICD; no performance claim',
 'serial':a.serial,'package':pkg,'activity':m['activity'],'APK_sha256':m['apk_sha256'],
 'native_libraries':m['native_library_sha256'],'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),
 'expected_original_APK':a.expected_original_apk,'expected_original_ELF':a.expected_original_elf,'module_report_sha256':sha(a.module_report) if a.module_report else None,'module_count':28 if a.module_report else 0,'resource_count':len(resources['expected_hashes']) if a.resource_report else 0,'resource_report_sha256':sha(a.resource_report) if a.resource_report else None,
 'baseline_bound':baseline_bound,'expected_shaderchain':a.expected_shaderchain,
 'crash_natural_exit_bound_seconds':30,'functional_timeout_seconds':45,'installation_timeout_seconds':70,'launches_allowed':1,'modes_once':['resource-bake'],'GUP_unprotect_retry':False,
 'writes_only':'new independent package + unique /data/local/tmp directory; cleanup exact package only'}
if not a.execute:
 print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(exist_ok=False,parents=True)
tmp='/data/local/tmp/dm-turnip-host-image-'+uuid.uuid4().hex
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
 assert a.original_artifact and a.original_artifact.is_file(),'Missing exact local original APK'
 raw_apk=a.original_artifact.read_bytes()
 assert hashlib.sha256(raw_apk).hexdigest()==digest,'Remote original APK hash differs from exact local artifact'
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
 log=ad('exec-out','run-as',pkg,'cat','files/runtime/native-image-test.log',check=False)
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
 if finals:return rows,'oracle-'+finals[-1].get('result','unknown'),finals[-1]
 return rows,None,None

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
      (n.get('package')=='com.android.packageinstaller' and n.get('resource-id')=='android:id/button1' and (n.get('text') in ('Install','安装','继续安装') or any(c.get('resource-id')=='com.android.packageinstaller:id/vbutton_title' and c.get('text')=='继续安装' for c in n.iter('node')))) or
      (n.get('package')=='com.oplus.appdetail' and n.get('resource-id','').endswith('/btn_left') and n.get('text')=='Install')]
    # Observed Vivo installer for this exact D2R Mobile app: acknowledge
    # the pending-install result only; never enter its settings/account link.
    acknowledgements=[n for n in tree.iter('node') if n.get('package')=='com.android.packageinstaller' and n.get('resource-id')=='com.android.packageinstaller:id/deleted_file_state_cb' and n.get('text')=='已了解应用的风险检测结果' and n.get('checked')!='true']
    if safe and len(acknowledgements)==1 and len(nodes)==1:
     node=acknowledgements[0];key='ack:'+node.get('resource-id')
     if key not in approvals:
      (a.output/'installer-risk-ack.xml').write_text(xml)
      x,y,z,w=map(int,re.findall(r'\d+',node.get('bounds')))
      ad('shell','input','tap',str((x+z)//2),str((y+w)//2));approvals.add(key)
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
 ad('push',a.driver,tmp+'/libvulkan_freedreno.so')
 ad('shell','run-as',pkg,'mkdir','-p','files/runtime/vulkan-driver','files/runtime/vulkan-fixture')
 ad('shell','run-as',pkg,'cp',tmp+'/libvulkan_freedreno.so','files/runtime/vulkan-driver/libvulkan_freedreno.so')
 assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/vulkan-driver/libvulkan_freedreno.so').split()[0]==sha(a.driver)
 ad('push',a.resource_input,tmp+'/resource-input')
 ad('shell','run-as',pkg,'cp',tmp+'/resource-input/resource-input.bin','files/runtime/resource-input.bin')
 ad('shell','run-as',pkg,'cp',tmp+'/resource-input/resource-source.json','files/runtime/resource-source.json')
 raw=ad('exec-out','run-as',pkg,'sha256sum',*['files/runtime/'+n for n in resource_hashes])
 assert {line.split(None,1)[1].strip().removeprefix('files/runtime/'):line.split()[0] for line in raw.splitlines()}==resource_hashes
 result['resource_inputs']=resource_hashes
 result['runs']=[]
 for kind,test_name in [('resource-bake','native-host-image-payload')]:
  assert exact_fixture() and not process(pkg),'Each mode is a fresh owned process, never a warm reselection'
  ad('shell','run-as',pkg,'rm','-f','files/runtime/native-image-test.log')
  args=['shell','am','start','-W','-n',pkg+'/'+m['activity'],'--ez','run_game','false',
   '--ez','native_image_test','true','--es','native_image_mode','--'+kind,'--es','vulkan_driver','adreno','--ez','vulkan_test_validation','false']
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
  assert final['result']=='passed' and final['formats']==1 and final['checks']>0
  assert final['mode']=='--'+kind and final['game_modules_executed'] is False
  config=[r for r in rows if r.get('native_image_app_configuration') is True]
  assert len(config)==1 and config[0]['uid']>=10000 and config[0]['mode']=='--'+kind
  assert config[0]['validation_layer']=='disabled_explicitly'
  assert config[0]['synchronization_validation']=='disabled_explicitly'
  assert config[0]['game_modules_executed'] is False
  assert '[vulkan loader] custom=/data/user/0/'+pkg+'/files/runtime/vulkan-driver/libvulkan_freedreno.so' in log
  assert not re.search(r'VUID-|FATAL|native fault signal=|CHECK line',log)
  identity=[r for r in rows if r.get('native_device_identity') is True]
  assert len(identity)==1 and identity[0]['driverID']==18
  assert all(re.fullmatch('[0-9a-f]{32}',identity[0][k]) and int(identity[0][k],16) for k in ('layout_UUID','device_UUID','driver_UUID'))
  run['queried_identity']=identity[0]
  maps_text=ad('exec-out','run-as',pkg,'cat','/proc/'+first_pid+'/maps')
  (a.output/(kind+'-maps.txt')).write_text(maps_text)
  assert 'files/runtime/vulkan-driver/libvulkan_freedreno.so' in maps_text
  assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/vulkan-driver/libvulkan_freedreno.so').split()[0]==sha(a.driver)
  baked=[r for r in rows if r.get('native_resource_bake_complete') is True]
  assert len(baked)==1 and (baked[0]['format'],baked[0]['width'],baked[0]['height'],baked[0]['levels'])==(158,256,256,9)
  assert baked[0]['GPU_sampling_executed'] is False
  # Publish only a private owned shell copy, then ordinary adb pull. The app
  # stops before any host packing. These files remain local evidence afterward.
  result['baked_outputs']={}
  import shlex
  for name in ['native-resource.bin','native-resource-layout.bin','native-resource.json']:
   remote='files/runtime/'+name;digest=ad('exec-out','run-as',pkg,'sha256sum',remote).split()[0]
   target=tmp+'/'+name;ad('shell','test','!','-e',target)
   ad('shell','run-as '+shlex.quote(pkg)+' cat '+shlex.quote(remote)+' > '+shlex.quote(target))
   assert ad('shell','sha256sum',target).split()[0]==digest
   local=a.output/name;ad('pull',target,local);assert sha(local)==digest
   result['baked_outputs'][name]=digest
  baked_record=json.loads((a.output/'native-resource.json').read_text())
  assert baked_record['contract']=='DM native image producer v1' and baked_record['driverID']==18
  assert all(baked_record[k]==identity[0][k] for k in ('layout_UUID','device_UUID','driver_UUID'))
  result['actual_baked_record']=baked_record
  run.update(result='passed');save()
  stop_owned(run)
  assert not process(pkg),'Owned process must stop between cold modes'
  run['PID_absent_after']=True;save()
 result.update(stage='actual-turnip-real-resource-bake-owned-passed',
  no_game_acceptance=True,native_owned_only=True,performance_claim=False)

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
raise SystemExit(0 if result['stage']=='actual-turnip-real-resource-bake-owned-passed' and not result['cleanup_errors'] else 1)
