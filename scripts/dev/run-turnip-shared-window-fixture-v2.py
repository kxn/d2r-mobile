#!/usr/bin/env python3
"""V2 canonical Android private-path correction; ONE ordinary-UID cold run of the ORIGINAL NVN shared-window/ASTC oracle.

Adapted from run-turnip-real-native-family-v2 ownership/installer transaction.
Default is an offline plan. No game launch, system setting changes or profiler.
"""
import argparse,hashlib,io,json,re,subprocess,time,uuid,signal,zipfile,xml.etree.ElementTree as ET
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
pkg='com.d2rmobile.sharedwindowfixture';original='com.d2rmobile.nativeport'
B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((a.artifact/'candidate.apk.build.json').read_text())
assert m['package']==pkg and m['activity']==pkg+'.MainActivity'
assert m['contains_game_binary'] is False and m['original_game_classes_assets_packaged'] is False
assert m['requires_actual_ASTC_GPU'] is True
assert sha(a.artifact/'candidate.apk')==m['apk_sha256']
assert sha(a.original_artifact)==a.expected_original_apk
with zipfile.ZipFile(a.artifact/'candidate.apk') as z:
 for name,digest in m['native_library_sha256'].items():assert hashlib.sha256(z.read('lib/arm64-v8a/'+name)).hexdigest()==digest
assert len(m['native_library_sha256'])==6
assert sha(a.driver)==m['driver_sha256']=='3c8710beb5c33016479601603d41f15508ab185c7b18c684ebe3f8e0ecfa3e61'
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
plan={'scope':'ONE cold original-NVN shared-window byte oracle; actual memfd SVC heap, overlap gate, ASTC physical copy; no game/performance claim',
 'serial':a.serial,'package':pkg,'activity':m['activity'],'APK_sha256':m['apk_sha256'],
 'native_libraries':m['native_library_sha256'],'shader_files':shader_files,'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),
 'expected_original_APK':a.expected_original_apk,'expected_original_ELF':a.expected_original_elf,
 'module_report_sha256':sha(a.module_report),'module_count':len(module_items),
 'resource_report_sha256':sha(a.resource_report) if a.resource_report else None,'expected_shaderchain':a.expected_shaderchain,
 'controller_revision':'v2-exact-canonical-private-path',
 'functional_timeout_seconds':90,'installation_timeout_seconds':70,'launches_allowed':1,
 'writes_only':'new independent package + unique tmp directory, original package read-only','settings_changes':False}
if not a.execute:
 print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(exist_ok=False,parents=True)
tmp='/data/local/tmp/dm-shared-window-'+uuid.uuid4().hex
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
 log=ad('exec-out','run-as',pkg,'cat','files/runtime/window-test.log',check=False)
 if log:(a.output/(phase['name']+'-vulkan-test.log')).write_text(log)
 return log

def parse_oracle_log(log):
 rows=[]
 for line in log.splitlines():
  if line.startswith('{'):
   try:rows.append(json.loads(line))
   except json.JSONDecodeError:pass
 if 'FATAL' in log or 'native fault signal=' in log or 'CHECK line' in log:return rows,'native-assertion-failed',None
 terminals=[r for r in rows if r.get('shared_window_app_terminal') is True]
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
    safe=any(n.get('text')=='D2R Shared Window Fixture' for n in tree.iter('node'))
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
 canonical_driver=ad('exec-out','run-as',pkg,'readlink','-f','files/runtime/driver/libvulkan_freedreno.so')
 allowed_driver_paths={'/data/user/0/'+pkg+'/files/runtime/driver/libvulkan_freedreno.so',
                       '/data/data/'+pkg+'/files/runtime/driver/libvulkan_freedreno.so'}
 assert canonical_driver in allowed_driver_paths,'Unexpected canonical private driver path'
 result['canonical_driver_path']=canonical_driver;save()
 ad('push',a.artifact/'vulkan-fixture',tmp+'/vulkan-fixture')
 ad('shell','run-as',pkg,'cp','-R',tmp+'/vulkan-fixture/.','files/runtime/vulkan-fixture/')
 raw=ad('exec-out','run-as',pkg,'sha256sum',*['files/runtime/vulkan-fixture/'+n for n in shader_files])
 assert {line.split(None,1)[1].strip().removeprefix('files/runtime/vulkan-fixture/'):line.split()[0] for line in raw.splitlines()}==shader_files
 result['shader_files_on_device_verified']=True
 args=['shell','am','start','-W','-n',pkg+'/'+m['activity'],'--es','fixture_directory','/data/user/0/'+pkg+'/files/runtime']
 phase={'name':'windows','expected_test':'shared-import-windows','launch_command':B+args,'launches':1};active_phase=phase
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
 launch.wait(timeout=5)
 assert phase['stage']=='oracle-passed' and terminal['rc']==0
 assert terminal['uid']>=10000 and terminal['pid']==int(first_pid)
 assert terminal['actual_ASTC_GPU'] is True and terminal['actual_shared_heap'] is True and terminal['window_bytes']==65536
 assert terminal['game_modules_executed'] is False
 config=[r for r in rows if r.get('shared_window_app_configuration') is True]
 assert len(config)==1 and config[0]['uid']==terminal['uid'] and config[0]['pid']==int(first_pid)
 assert config[0]['provider']=='adreno' and config[0]['async'] is True and config[0]['validation']=='disabled_explicitly'
 passed=[r for r in rows if r.get('test')=='shared-import-windows']
 assert len(passed)==1 and passed[0]['result']=='passed' and passed[0]['checks']>0
 assert passed[0]['actual_memfd_heap'] is True and passed[0]['no_shadow'] is True and passed[0]['observer_union'] is True
 astc=[r for r in rows if r.get('test_details')=='ASTC4-copy-physical-encoding']
 assert len(astc)==1 and astc[0]['result']=='passed' and astc[0]['actual_ASTC_GPU'] is True
 assert astc[0]['fresh_BC3_array_became_ASTC4'] and astc[0]['mip_layer_ROI_preserved'] and astc[0]['canonical_pool_bytes_unchanged']
 identities=[r for r in rows if r.get('shared_window_app_identity') is True]
 assert len(identities)==1
 for key in ('device_UUID','driver_UUID','pipeline_cache_UUID'):assert re.fullmatch('[0-9a-f]{32}',identities[0][key])
 loader_paths=re.findall(r'\[vulkan loader\] custom=([^\s]+)',log)
 assert loader_paths==[canonical_driver],'Loader must use the exact prelaunch canonical driver'
 assert not re.search(r'VUID-|FATAL|native fault signal=|CHECK line',log)
 maps=ad('exec-out','run-as',pkg,'cat','/proc/'+first_pid+'/maps');(a.output/'maps.txt').write_text(maps)
 mapped_paths={line.split(None,5)[5] for line in maps.splitlines() if len(line.split(None,5))==6}
 assert canonical_driver in mapped_paths,'Exact canonical loaded driver absent from live first-PID maps'
 assert not any(name in maps for name in ('libvulkan_panfrost.so','libVkLayer_','renderdoc'))
 assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/driver/libvulkan_freedreno.so').split()[0]==sha(a.driver)
 phase.update(result='passed',actual_driver_sha256=sha(a.driver),identity=identities[0]);save()
 stop_owned(phase);result['stage']='actual-original-NVN-shared-window-owned-passed'
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
raise SystemExit(0 if result['stage']=='actual-original-NVN-shared-window-owned-passed' and not result['cleanup_errors'] else 1)
