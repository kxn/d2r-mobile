#!/usr/bin/env python3
"""ONE ordinary-UID original NVN native family/window transaction, with proven v3 alias identity guards.

Adapted from run-turnip-real-native-family-v2 ownership/installer transaction.
Default is an offline plan. No game launch, system setting changes or profiler.
"""
import argparse,hashlib,io,json,re,subprocess,time,uuid,signal,zipfile,os,xml.etree.ElementTree as ET
from pathlib import Path
P=argparse.ArgumentParser()
P.add_argument('--serial',required=True)
P.add_argument('--artifact',type=Path,required=True)
P.add_argument('--driver',type=Path,required=True)
P.add_argument('--original-artifact',type=Path,required=True)
P.add_argument('--module-report',type=Path,required=True)
P.add_argument('--resource-report',type=Path)
P.add_argument('--expected-original-apk',required=True)
P.add_argument('--expected-original-elf',required=True)
P.add_argument('--expected-shaderchain',required=True)
P.add_argument('--output',type=Path,required=True)
P.add_argument('--execute',action='store_true')
a=P.parse_args();R=Path(__file__).resolve().parents[2]
pkg='com.d2rmobile.nativewindowfamilyfixture';original='com.d2rmobile.nativeport'
B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((a.artifact/'candidate.apk.build.json').read_text())
assert m['package']==pkg and m['activity']==pkg+'.MainActivity'
assert m['contains_game_binary'] is False and m['original_game_classes_assets_packaged'] is False
assert m['requires_actual_ASTC_GPU'] is True and m['previous_writer_ASTC_fixture'] is True
assert sha(a.artifact/'candidate.apk')==m['apk_sha256']
assert sha(a.original_artifact)==a.expected_original_apk
with zipfile.ZipFile(a.artifact/'candidate.apk') as z:
 for name,digest in m['native_library_sha256'].items():assert hashlib.sha256(z.read('lib/arm64-v8a/'+name)).hexdigest()==digest
assert len(m['native_library_sha256'])==6
assert sha(a.driver)==m['driver_sha256']=='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'
resource_inputs=m['owned_resource_inputs']
assert set(resource_inputs)=={'native-layouts.bin','native-resource.bin','resource-input.bin'}
for name,digest in resource_inputs.items():assert sha(a.artifact/'resource4'/name)==digest
assert resource_inputs['native-layouts.bin']=='d61c38b5bcbac469f9a111e6b239fada4b7fb2c5b86f33aec3810661215b8e55'
assert resource_inputs['native-resource.bin']=='de206d467fc9dd74de9e957e432cf2470512dc6825caceff40d89a7260e5e418'
assert resource_inputs['resource-input.bin']=='d078631c7797b699200230a09533c9ad394a40fb9244fe0f75d905974ca49354'
assert m['native_payload_bytes']==135168 and (a.artifact/'resource4/native-resource.bin').stat().st_size==135168
assert m['native_resource_pack_sha256']=='9fb5a9d015b60a9857620419ae4fd70a4704786398a2ebaaf0f4f60f8cd72144'
assert m['native_resource_producer_sha256']=='34ec895c3699da37f59b5af0a057ae0555934692917edd9bc62a775a9847a29f'
shader_files=m['shader_files'];assert shader_files
assert all(Path(n).name==n and sha(a.artifact/'vulkan-fixture'/n)==h for n,h in shader_files.items())
assert sha(a.artifact/'fixture-hashes.json')==m['fixture_hashes_sha256']
assert json.loads((a.artifact/'fixture-hashes.json').read_text())==shader_files
for digest in (a.expected_original_apk,a.expected_original_elf):assert re.fullmatch('[0-9a-f]{64}',digest)
assert re.fullmatch('[0-9a-f]{12,64}',a.expected_shaderchain)
expected={};deployment=json.loads(a.module_report.read_text())
assert deployment['serial']==a.serial and deployment['package']==original
if 'artifacts' in deployment:
 assert deployment['verified_on_device'] and len(deployment['artifacts']) in (28,29)
 module_items=deployment['artifacts']
 for name,item in module_items.items():
  assert re.fullmatch('[A-Za-z0-9_.-]+',name) and name not in ('.','..')
  assert re.fullmatch('[0-9a-f]{64}',item['sha256'])
  expected['files/runtime/modules/'+name]=item['sha256']
else:
 # Existing root baseline-readonly format, not an invented deployment proof.
 # Recheck actual original files before and after this independent package.
 assert deployment['no_device_mutation'] and deployment['game_PID_absent']
 assert deployment['APK_sha256']==a.expected_original_apk and deployment['chain']==a.expected_shaderchain
 inventory=deployment['inventories'];module_items=inventory['files/runtime/modules']
 assert len(module_items) in (28,29)
 for folder in ('files/runtime/modules','files/runtime/romfs_overlay','files/runtime/vulkan-driver'):
  for name,digest in inventory[folder].items():
   assert name.startswith(folder+'/') and re.fullmatch('[A-Za-z0-9_./-]+',name) and '..' not in Path(name).parts
   assert re.fullmatch('[0-9a-f]{64}',digest)
   expected[name]=digest
if a.resource_report:
 resources=json.loads(a.resource_report.read_text())
 assert resources['stage']=='deployed' and resources['phone_hashes_verified'] and resources['expected_hashes']
 for name,digest in resources['expected_hashes'].items():
  assert re.fullmatch('[A-Za-z0-9_./-]+',name) and not Path(name).is_absolute() and '..' not in Path(name).parts and re.fullmatch('[0-9a-f]{64}',digest)
  expected['files/runtime/romfs_overlay/'+name]=digest
plan={'scope':'ONE cold original-NVN native image family + window unions; actual memfd, real AA185135168B firepit all9mips; no game/performance claim',
 'serial':a.serial,'package':pkg,'activity':m['activity'],'APK_sha256':m['apk_sha256'],
 'owned_resource_inputs':resource_inputs,'native_libraries':m['native_library_sha256'],'shader_files':shader_files,'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),
 'expected_original_APK':a.expected_original_apk,'expected_original_ELF':a.expected_original_elf,
 'module_report_sha256':sha(a.module_report),'module_count':len(module_items),
 'resource_report_sha256':sha(a.resource_report) if a.resource_report else None,'expected_shaderchain':a.expected_shaderchain,
 'controller_revision':'v3-exact-private-alias-device-inode-size-sha',
 'functional_timeout_seconds':90,'installation_timeout_seconds':70,'launches_allowed':1,
 'writes_only':'new independent package + unique tmp directory, original package read-only','settings_changes':False}
if not a.execute:
 print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(exist_ok=False,parents=True)
tmp='/data/local/tmp/dm-native-window-family-'+uuid.uuid4().hex
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
 # Exact local artifact avoids a large exec-out transfer; device SHA binds it.
 raw_apk=a.original_artifact.read_bytes()
 assert hashlib.sha256(raw_apk).hexdigest()==digest
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
 log=ad('exec-out','run-as',pkg,'cat','files/runtime/native-family-test.log',check=False)
 if log:(a.output/(phase['name']+'-vulkan-test.log')).write_text(log)
 return log

def parse_oracle_log(log):
 rows=[]
 for line in log.splitlines():
  if line.startswith('{'):
   try:rows.append(json.loads(line))
   except json.JSONDecodeError:pass
 if 'FATAL' in log or 'native fault signal=' in log or 'CHECK line' in log:return rows,'native-assertion-failed',None
 terminals=[r for r in rows if r.get('native_window_family_app_terminal') is True]
 if terminals:
  terminal=terminals[-1]
  return rows,'oracle-passed' if terminal.get('rc')==0 else 'oracle-unsupported-or-terminal-error',terminal
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
    safe=any(n.get('text')=='D2R Native Window Family Fixture' for n in tree.iter('node'))
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
def interrupted(sig,frame):raise KeyboardInterrupt('owned fixture interrupted; cleanup')
signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
save()
try:
 original_before=snapshot();result['original_before']=original_before;save()
 assert not ad('shell','pm','path',pkg,check=False),'Existing fixture package: do not replace or adopt'
 assert not process(pkg)
 layers=ad('shell','settings','get','global','enable_gpu_debug_layers')
 layer_app=ad('shell','settings','get','global','gpu_debug_app')
 assert not (layers=='1' and layer_app==pkg),'External layers target this package; no settings changes'
 result['external_layer_preflight']={'enabled':layers,'app':layer_app};save()
 ad('shell','mkdir',tmp);made_tmp=True;installation_requested=True
 install();installed=True;assert exact_fixture()
 assert not process(pkg),'Installer opened fixture unexpectedly; no second launch'
 ad('push',a.driver,tmp+'/libvulkan_freedreno.so')
 ad('shell','run-as',pkg,'mkdir','-p','files/runtime/driver','files/runtime/vulkan-fixture','files/runtime/shader-cache')
 ad('shell','run-as',pkg,'cp',tmp+'/libvulkan_freedreno.so','files/runtime/driver/libvulkan_freedreno.so')
 assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/driver/libvulkan_freedreno.so').split()[0]==sha(a.driver)
 allowed_driver_paths={'/data/user/0/'+pkg+'/files/runtime/driver/libvulkan_freedreno.so',
                       '/data/data/'+pkg+'/files/runtime/driver/libvulkan_freedreno.so'}
 driver_alias_stat={}
 for path in sorted(allowed_driver_paths):
  raw_stat=ad('exec-out','run-as',pkg,'stat','-c','%d:%i:%s',path)
  assert re.fullmatch(r'[0-9]+:[0-9]+:[0-9]+',raw_stat),'Unexpected stat schema'
  dev,inode,size=map(int,raw_stat.split(':'))
  assert inode>0 and size==a.driver.stat().st_size
  digest=ad('exec-out','run-as',pkg,'sha256sum',path).split()[0]
  assert digest==sha(a.driver),'Private alias does not contain the frozen driver'
  driver_alias_stat[path]={'device':dev,'major':os.major(dev),'minor':os.minor(dev),'inode':inode,'size':size,'sha256':digest,'raw_stat':raw_stat}
 assert len({(v['device'],v['inode'],v['size']) for v in driver_alias_stat.values()})==1,'Private aliases must be the same file'
 result['driver_alias_stat']=driver_alias_stat;save()
 ad('push',a.artifact/'resource4',tmp+'/resource4')
 ad('shell','run-as',pkg,'cp','-R',tmp+'/resource4','files/runtime/resource4')
 for name,digest in resource_inputs.items():assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/resource4/'+name).split()[0]==digest
 ad('push',a.artifact/'vulkan-fixture',tmp+'/vulkan-fixture')
 ad('shell','run-as',pkg,'cp','-R',tmp+'/vulkan-fixture/.','files/runtime/vulkan-fixture/')
 raw=ad('exec-out','run-as',pkg,'sha256sum',*['files/runtime/vulkan-fixture/'+n for n in shader_files])
 assert {line.split(None,1)[1].strip().removeprefix('files/runtime/vulkan-fixture/'):line.split()[0] for line in raw.splitlines()}==shader_files
 result['shader_files_on_device_verified']=True
 args=['shell','am','start','-W','-n',pkg+'/'+m['activity'],'--es','fixture_directory','/data/user/0/'+pkg+'/files/runtime','--es','native_resource_pack_sha256',m['native_resource_pack_sha256'],'--es','native_resource_producer_sha256',m['native_resource_producer_sha256']]
 phase={'name':'native-family','expected_test':'native-resource-original-NVN','launch_command':B+args,'launches':1};active_phase=phase
 result['runs']=[phase];result['stage']='one-cold-launch';save();launched=True
 with (a.output/'launch.txt').open('w') as f:launch=subprocess.Popen(B+args,stdout=f,stderr=subprocess.STDOUT)
 deadline=time.monotonic()+90;rows=[]
 while True:
  log=read_oracle_log(phase);rows,stage,terminal=parse_oracle_log(log);phase['rows']=rows
  if stage=='native-assertion-failed':phase['native_terminal_seen']=True
  current,kind=observe_pid(phase,allow_claim=not phase.get('native_terminal_seen',False))
  if stage=='native-assertion-failed' or kind in ('ambiguous','replacement','unclaimed'):
   wait_natural_exit(phase);raise RuntimeError('Original native failure or lost first-PID ownership; no retry')
  if stage is not None:
   assert first_pid is not None and kind=='owned','Terminal cannot replace first-PID ownership'
   phase['stage']=stage;phase['terminal']=terminal;save();break
  if first_pid is not None and not current:
   phase['stage']='exited-without-terminal';save();raise RuntimeError('Owned process exited; no retry')
  if time.monotonic()>=deadline:raise TimeoutError('Single functional bound reached; no relaunch')
  time.sleep(.2)
 # Preserve official first-PID maps BEFORE any terminal postcondition. Even a
 # later guard failure leaves the data for offline review rather than rerun.
 current,kind=observe_pid(phase,allow_claim=False)
 assert kind=='owned' and current==first_pid,'Cannot read maps of an unowned PID'
 maps=ad('exec-out','run-as',pkg,'cat','/proc/'+first_pid+'/maps')
 (a.output/'maps.txt').write_text(maps);phase['maps_saved_before_postconditions']=True;save()
 launch.wait(timeout=5)
 assert phase['stage']=='oracle-passed' and terminal['rc']==0
 assert terminal['uid']>=10000 and terminal['pid']==int(first_pid)
 assert terminal['actual_ASTC_GPU'] is True and terminal['actual_shared_heap'] is True and terminal['window_bytes']==65536
 assert terminal['game_modules_executed'] is False
 config=[r for r in rows if r.get('native_window_family_app_configuration') is True]
 assert len(config)==1 and config[0]['uid']==terminal['uid'] and config[0]['pid']==int(first_pid)
 assert config[0]['previous_writer_ASTC'] is True
 assert config[0]['provider']=='adreno' and config[0]['async'] is True and config[0]['validation']=='disabled_explicitly'
 passed=[r for r in rows if r.get('test')=='native-resource-original-NVN']
 assert len(passed)==1 and passed[0]['result']=='passed' and passed[0]['checks']==408
 assert passed[0]['format']==158 and passed[0]['mips']==9 and passed[0]['family_version']==2 and passed[0]['tail_levels']==5
 assert passed[0]['no_pool_shadow'] is True and passed[0]['full_all_mips'] is True and passed[0]['tail_packaged_and_B2T'] is True
 assert passed[0]['native_offset']==1056768 and passed[0]['pool_bytes']==4194304 and passed[0]['game_modules_executed'] is False
 previous=[r for r in rows if r.get('test')=='native-previous-writer-ASTC-pending-FS']
 assert len(previous)==1 and previous[0]['result']=='passed' and previous[0]['checks']==36
 assert previous[0]['positive_cases']==2 and previous[0]['unknown_negative_cases']==1 and previous[0]['mips']==[4,5,6]
 for key in ('old_TRANSFER_and_COMPUTE_histories_retained','current_recording_denied','FS_pixels_reference_equal','CS_pixels_reference_equal','T2B_compressed_blocks_exact'):assert previous[0][key] is True
 lane=[r for r in rows if r.get('native_allocation_lane') is True]
 assert len(lane)==1 and lane[0]['imports']==1 and lane[0]['import_bytes']==135168 and lane[0]['metadata_bytes']==120
 for key in ('same_physical_pages_as_Buffer_imports','no_payload_copy','window_two_unions','last_release_collected'):assert lane[0][key] is True
 identity=[r for r in rows if r.get('native_resource_identity') is True]
 assert len(identity)==1 and identity[0]['driver_sha256']==m['driver_sha256']
 assert '[shared pool] cold overlapping-import bidirectional GPU byte gate: passed' in log
 assert 'backing=shared-memfd' in log
 identities=[r for r in rows if r.get('native_window_family_app_identity') is True]
 assert len(identities)==1
 for key in ('device_UUID','driver_UUID','pipeline_cache_UUID'):assert re.fullmatch('[0-9a-f]{32}',identities[0][key])
 loader_paths=re.findall(r'\[vulkan loader\] custom=([^\s]+)',log)
 assert len(loader_paths)==1 and loader_paths[0] in allowed_driver_paths,'Unexpected exact loader private path'
 assert not re.search(r'VUID-|FATAL|native fault signal=|CHECK line',log)
 driver_mappings=[]
 for line in maps.splitlines():
  columns=line.split(None,5)
  if len(columns)!=6 or columns[5] not in allowed_driver_paths:continue
  dev=columns[3].split(':');assert len(dev)==2
  major,minor=map(lambda text:int(text,16),dev);inode=int(columns[4])
  expected_stat=driver_alias_stat[columns[5]]
  assert (major,minor,inode)==(expected_stat['major'],expected_stat['minor'],expected_stat['inode']),'Mapped exact driver path has a different device/inode'
  driver_mappings.append({'raw':line,'path':columns[5],'major':major,'minor':minor,'inode':inode})
 assert driver_mappings,'Exact private driver absent from live first-PID maps'
 phase['driver_mappings']=driver_mappings;save()
 assert not any(name in maps for name in ('libvulkan_panfrost.so','libVkLayer_','renderdoc'))
 assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/driver/libvulkan_freedreno.so').split()[0]==sha(a.driver)
 for name,digest in resource_inputs.items():assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/resource4/'+name).split()[0]==digest
 phase.update(result='passed',actual_driver_sha256=sha(a.driver),identity=identities[0]);save()
 stop_owned(phase);result['stage']='actual-original-NVN-native-window-family-owned-passed'
 result.update(no_game_acceptance=True,performance_claim=False)
except BaseException as e:result.update(stage='failed-preserved',error=repr(e))
finally:
 try:
  for client in (installer,launch):
   if client is not None and client.poll() is None:
    client.terminate()
    try:client.wait(timeout=3)
    except subprocess.TimeoutExpired:client.kill();client.wait(timeout=3)
  if installation_requested and not installed:installed=exact_fixture()
  if installed:
   stop_owned();assert not process(pkg);result['fixture_PID_absent']=True
   assert exact_fixture();ad('uninstall',pkg)
   assert not ad('shell','pm','path',pkg,check=False);result['fixture_uninstalled']=True
  if made_tmp:
   ad('shell','rm','-r',tmp);ad('shell','test','!','-e',tmp);result['fixture_tmp_absent']=True
  result['owned_clients_stopped']=all(client is None or client.poll() is not None for client in (installer,launch))
  if original_before is not None:
   result['original_after']=snapshot();assert result['original_after']==original_before;result['original_unchanged']=True
 except BaseException as e:result['cleanup_errors'].append(repr(e))
 save()
print(json.dumps(result))
raise SystemExit(0 if result['stage']=='actual-original-NVN-native-window-family-owned-passed' and not result['cleanup_errors'] else 1)
