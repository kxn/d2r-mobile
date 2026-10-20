#!/usr/bin/env python3
"""Two cold owned native-image modes; strict package/PID/provenance transaction, no game or profiling."""
import argparse, hashlib, io, json, re, subprocess, time, uuid, signal, zipfile, xml.etree.ElementTree as ET
from pathlib import Path
P=argparse.ArgumentParser()
P.add_argument('--serial',required=True)
P.add_argument('--artifact',type=Path,required=True)
P.add_argument('--driver',type=Path,required=True)
P.add_argument('--module-report',type=Path)
P.add_argument('--resource-report',type=Path)
P.add_argument('--expected-original-apk')
P.add_argument('--expected-original-elf')
P.add_argument('--expected-shaderchain')
P.add_argument('--output',type=Path,required=True)
P.add_argument('--execute',action='store_true')
a=P.parse_args();R=Path(__file__).resolve().parents[2]
pkg='com.d2rmobile.nativeresourcefixture';original='com.d2rmobile.nativeport'
B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((a.artifact/'candidate.apk.build.json').read_text())
assert m['package']==pkg and m['activity']=='com.d2rmobile.nativeport.MainActivity'
assert sha(a.artifact/'candidate.apk')==m['apk_sha256']
with zipfile.ZipFile(a.artifact/'candidate.apk') as archive:
 for leaf,digest in m['native_library_sha256'].items():
  assert hashlib.sha256(archive.read('lib/arm64-v8a/'+leaf)).hexdigest()==digest
assert len(m['native_library_sha256'])==6
assert sha(a.driver)=='6b15cf088112720aebb5cec3342e43a2c14be67b16f0460e8d2db9a75889189f'
assert m['resource_source_ready_sha256']=='0010dbb915f51341a49fb2907094079ef59b977d8fc5a607371ef88a3a2ff6ea'
assert sha(a.artifact/'resource-source-ready.json')==m['resource_source_ready_sha256']
resource_ready=json.loads((a.artifact/'resource-source-ready.json').read_text())
assert resource_ready['source']['commit']=='86381c7c93aa0b299b75cb33c87497b6d7d6528b'
expected_inputs={('resource4' if row['label']=='ASTC4' else 'resource6')+'/'+Path(item['path']).name:item['sha256'] for row in resource_ready['real_inputs'] for item in row['private_artifacts'] if Path(item['path']).name in ('resource-input.bin','resource-source.json')}
resource_inputs=m['owned_resource_inputs']
assert resource_inputs==expected_inputs
assert m['resource_source_commit']=='86381c7c93aa0b299b75cb33c87497b6d7d6528b'
assert set(resource_inputs)=={name+'/'+leaf for name in ['resource4','resource6'] for leaf in ['resource-input.bin','resource-source.json']}
assert all(re.fullmatch('[0-9a-f]{64}',h) and sha(a.artifact/'resources'/name)==h for name,h in resource_inputs.items())
baseline_bound=bool(a.module_report and a.resource_report and a.expected_original_apk and a.expected_original_elf and a.expected_shaderchain)
if a.execute:assert baseline_bound,'Execution requires current root APK/ELF/28modules/6resources/shaderchain bindings'
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
 assert resources['stage']=='deployed' and resources['phone_hashes_verified'] and len(resources['expected_hashes'])==6
 for name,digest in resources['expected_hashes'].items():
  assert re.fullmatch('[A-Za-z0-9_./-]+',name) and not Path(name).is_absolute() and '..' not in Path(name).parts and re.fullmatch('[0-9a-f]{64}',digest)
  expected['files/runtime/romfs_overlay/'+name]=digest
plan={'scope':'ONE installation, TWO cold real ASTC4/6 resource modes: bake/import, each once; game=false; original package read-only; VVL explicitly unavailable with direct ICD; no performance claim',
 'serial':a.serial,'package':pkg,'activity':m['activity'],'APK_sha256':m['apk_sha256'],
 'native_libraries':m['native_library_sha256'],'owned_resource_inputs':resource_inputs,'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),
 'expected_original_APK':a.expected_original_apk,'expected_original_ELF':a.expected_original_elf,'module_report_sha256':sha(a.module_report) if a.module_report else None,'module_count':28 if a.module_report else 0,'resource_count':6 if a.resource_report else 0,'resource_report_sha256':sha(a.resource_report) if a.resource_report else None,
 'baseline_bound':baseline_bound,'expected_shaderchain':a.expected_shaderchain,
 'crash_natural_exit_bound_seconds':30,'functional_timeout_seconds':45,'installation_timeout_seconds':70,'launches_allowed':2,'modes_once':['resources-bake','resources-import'],'GUP_unprotect_retry':False,
 'writes_only':'new independent package + unique /data/local/tmp directory; cleanup exact package only'}
if not a.execute:
 print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(exist_ok=False,parents=True)
tmp='/data/local/tmp/dm-native-resource-'+uuid.uuid4().hex
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
 raw_apk=subprocess.run(B+['exec-out','cat',path],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15,check=True).stdout
 assert hashlib.sha256(raw_apk).hexdigest()==digest,'Original APK read/hash mismatch'
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
 if 'FATAL' in log or 'native fault signal=' in log or 'CHECK line' in log:return rows,'native-assertion-failed',None
 finals=[r for r in rows if r.get('test')==active_phase['expected_test']]
 if finals:return rows,'oracle-'+finals[-1].get('result','unknown'),finals[-1]
 terminals=[r for r in rows if r.get('native_image_app_terminal') is True]
 if terminals and terminals[-1].get('rc')!=0:return rows,'oracle-unsupported-or-terminal-error',None
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
 ad('push',a.artifact/'resources',tmp+'/resources')
 ad('shell','run-as',pkg,'cp','-R',tmp+'/resources/.','files/runtime/')
 raw=ad('exec-out','run-as',pkg,'sha256sum',*['files/runtime/'+name for name in resource_inputs])
 assert {line.split(None,1)[1].strip().removeprefix('files/runtime/'):line.split()[0] for line in raw.splitlines()}==resource_inputs
 result['runs']=[]
 for kind,test_name in [('resources-bake','native-host-image-payload'),('resources-import','native-host-image-payload')]:
  assert exact_fixture() and not process(pkg),'Each mode is a fresh owned process, never a warm reselection'
  ad('shell','run-as',pkg,'rm','-f','files/runtime/native-image-test.log')
  args=['shell','am','start','-W','-n',pkg+'/'+m['activity'],'--ez','run_game','false',
   '--ez','native_image_test','true','--es','native_image_mode','--'+kind,'--es','vulkan_driver','panvk','--ez','vulkan_test_validation','false']
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
  assert final['result']=='passed' and final['formats']==2 and final['checks']>0
  assert final['mode']=='--'+kind and final['game_modules_executed'] is False
  config=[r for r in rows if r.get('native_image_app_configuration') is True]
  assert len(config)==1 and config[0]['uid']>=10000 and config[0]['mode']=='--'+kind
  assert config[0]['validation_layer']=='disabled_explicitly'
  assert config[0]['synchronization_validation']=='disabled_explicitly'
  assert config[0]['game_modules_executed'] is False
  assert 'provider=panvk direct ICD=' in log
  assert not re.search(r'VUID-|FATAL|native fault signal=|CHECK line',log)
  inputs=[r for r in rows if r.get('native_resource_input_verified') is True]
  assert len(inputs)==4 and {r['format'] for r in inputs}=={158,166}
  assert all(r['width']==r['height']==256 and r['levels']==9 and r['game_readonly_authority'] is False for r in inputs)
  if kind=='resources-bake':
   baked=[r for r in rows if r.get('native_resource_bake_complete') is True]
   assert len(baked)==2 and {r['format'] for r in baked}=={158,166}
   assert all(r['width']==r['height']==256 and r['levels']==9 and r['physical_image_bytes']>0 and r['GPU_sampling_executed'] is False for r in baked)
   names=[name+'/'+leaf for name in ['resource4','resource6'] for leaf in ['native-resource.bin','native-resource.json','native-resource-layout.bin']]
   raw=ad('exec-out','run-as',pkg,'sha256sum',*['files/runtime/'+n for n in names])
   hashes={line.split(None,1)[1].strip():line.split()[0] for line in raw.splitlines()}
   assert set(hashes)=={'files/runtime/'+n for n in names} and all(re.fullmatch('[0-9a-f]{64}',h) for h in hashes.values())
   for name in names:
    blob=subprocess.run(B+['exec-out','run-as',pkg,'cat','files/runtime/'+name],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15,check=True).stdout
    assert hashlib.sha256(blob).hexdigest()==hashes['files/runtime/'+name]
    local=a.output/'baked'/name;local.parent.mkdir(parents=True,exist_ok=True);local.write_bytes(blob)
    if name.endswith('.json'):
     meta=json.loads(blob);source=name.split('/')[0]
     assert meta['complete'] is True and meta['game_readonly_authority'] is False and meta['GPU_sampling_executed'] is False
     assert meta['input_sha256']==resource_inputs[source+'/resource-input.bin'] and meta['declaration_sha256']==resource_inputs[source+'/resource-source.json']
     assert meta['driverID']==20 and meta['recipe']['mipLevels']==9 and meta['recipe']['width']==meta['recipe']['height']==256
     assert meta['recipe']['VkFormat']==(158 if source=='resource4' else 166)
     assert meta['original_driver_roundtrip_compressed_blocks_exact'] is True
     assert len(meta['mips'])==9
   result['native_payload_hashes']=hashes
  else:
   imported=[r for r in rows if r.get('native_resource_import_passed') is True]
   assert len(imported)==2 and {r['format'] for r in imported}=={158,166}
   for row in imported:
    assert row['width']==row['height']==256 and row['mips']==9 and row['sampled_texels']==87381
    assert row['reference_upload']=='vkCmdCopyBufferToImage' and row['all_mips_exact_GPU_reference'] is True and row['reference_varied'] is True
    assert re.fullmatch('[0-9a-f]{64}',row['sample_SHA256'])
    assert row['memory_flags']==11 and row['atom']==64 and row['image_offset']>0 and row['pool_bytes']<=16777216
    assert row['NC_Flush_offset']==row['image_offset'] and row['NC_Flush_size']>0 and row['NC_Flush_offset']%64==row['NC_Flush_size']%64==0
    assert row['NC_Flush_offset']+row['NC_Flush_size']<=row['pool_bytes']
    assert row['native_backing_extra_image_allocation'] is False and row['reference_allocation_is_test_only'] is True
    assert row['neighbors_exact'] is True and row['original_mapping_survived_Free'] is True and row['game_modules_executed'] is False
   all_owned=result['native_payload_hashes']|{'files/runtime/'+n:h for n,h in resource_inputs.items()}
   raw=ad('exec-out','run-as',pkg,'sha256sum',*all_owned)
   assert {line.split(None,1)[1].strip():line.split()[0] for line in raw.splitlines()}==all_owned
  run.update(result='passed');save()
  stop_owned(run)
  assert not process(pkg),'Owned process must stop between cold modes'
  run['PID_absent_after']=True;save()
 result.update(stage='actual-noncoherent-native-resource-owned-passed',
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
raise SystemExit(0 if result['stage']=='actual-noncoherent-native-resource-owned-passed' and not result['cleanup_errors'] else 1)
