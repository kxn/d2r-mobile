#!/usr/bin/env python3
"""One reversible ordinary-UID original022 NVN BC3 subset. Offline by default.
Reuses established AA package/PID/inode/installer cleanup; original game read-only.
No game start, property/settings mutation, TRACE or profiling.
Selected ac88 contains diagnostic source changes, all additional flags OFF; not untouched upstream PanVK.
"""
import argparse,hashlib,importlib.util,json,re,subprocess,time,uuid,signal,zipfile,os,xml.etree.ElementTree as ET
from pathlib import Path
R=Path('/home/dl/d2r-mobile');READY=R/'.local/logs/panvk-bc4-layered-ca619b3-source-ready-agent.json'
READY_SHA='292f477dec43d5dbe210a25773259efea577f6ca62bfa4c880e05f60ed8e6cb4'
PREVIOUS=R/'.local/worktrees/panvk-shader-map-game-v7/scripts/dev/run-panvk-minegraph-game.py'
S=importlib.util.spec_from_file_location('reviewed_original022_transaction',PREVIOUS);previous=importlib.util.module_from_spec(S);S.loader.exec_module(previous)
P=argparse.ArgumentParser();P.add_argument('--serial',default='10.10.10.44:34455');P.add_argument('--artifact',type=Path,required=True);P.add_argument('--output',type=Path,required=True);P.add_argument('--execute',action='store_true');a=P.parse_args()
pkg='com.d2rmobile.panvkbc3fixture';original='com.d2rmobile.nativeport';B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(READY)==READY_SHA;ready=json.loads(READY.read_text());assert ready['original_runtime_base']=='022d6254d9de565852fa22394b6403491db86707'
for row in ready['only_changed_files']:assert sha(row['path'])==row['sha256']
base_source=Path(ready['source_root']);base_manifest=ready['canonical_manifest'];assert sha(base_manifest['path'])==base_manifest['sha256']
canonical=json.loads(Path(base_manifest['path']).read_text());assert all(sha(base_source/n)==h for n,h in canonical['source_inputs'].items())
assert sha(a.artifact/'candidate.apk.build.json')==ready['ordinary_APK_manifest']['sha256']
m=json.loads((a.artifact/'candidate.apk.build.json').read_text());assert sha(a.artifact/'candidate.apk')==m['apk_sha256']==ready['ordinary_UID_independent_APK']['sha256']
assert m['package']==pkg and m['activity']==pkg+'.MainActivity' and not m['contains_game_binary']and not m['original_app_DEX_assets_packaged']and not m['shared_heap']and not m['TRACE']
assert m['source_revision']==ready['source_revision']and m['native_library_sha256']==canonical['native_library_sha256']and len(m['native_library_sha256'])==6
with zipfile.ZipFile(a.artifact/'candidate.apk')as z:
 assert all(hashlib.sha256(z.read('lib/arm64-v8a/'+n)).hexdigest()==h for n,h in m['native_library_sha256'].items())
assert sha(a.artifact/'driver/libvulkan_panfrost.so')==m['driver_sha256']==ready['driver_unchanged']['sha256']
assert sha(a.artifact/'dataset-source.json')==m['dataset_source_sha256']==ready['dataset_source']['sha256']
assert sha(a.artifact/'bc4-layered.bin')==m['dataset_sha256']==ready['dataset']['sha256']
shader_files=m['shader_files'];assert set(shader_files)=={k+e for k in ['e5888d20bd2a77b9','c113a6c86bb7e554']for e in ['.meta','.spv']}
assert all(sha(a.artifact/'vulkan-fixture'/n)==h for n,h in shader_files.items())
driver,promotion=previous.proof();assert driver['driver']['sha256']==m['driver_sha256']
expected=promotion['promoted_module_and_resource_hashes']
plan={'scope':'ONE ordinaryUID original022 actualNVN BC4source->252layerarray/layer250/7mip functional run; one final fence,no game/performance/v8-WAIT equivalence claim','serial':a.serial,'package':pkg,'source_ready_sha256':READY_SHA,'APK_sha256':m['apk_sha256'],'driver_sha256':m['driver_sha256'],'dataset_sha256':m['dataset_sha256'],'source_revision':m['source_revision'],'controller_sha256':sha(__file__),'fixture_case':'bc4-layered','NVN_submissions':5,'explicit_intermediate_drains':0,'final_fence_waits':1,'functional_bound_seconds':75,'install_bound_seconds':70,'launches':1,'shared':False,'TRACE':False,'settings_properties_changed':False,'selected_driver_boundary':'Frozen ac88 diagnostic-source ICD4e7b29e7; startup+bc_trace tokens, TRACE/SYNC/DUMP/SHADER_MAP OFF, unchanged GPU path; not stock or untouched Mesa','original_baseline':'Existing production022 APK/35resources/stockMali directories are read-only, never save/restage/stop','original_game_policy':'must be stopped; APK35files+4dirs+shaders+properties+layer settings read-only before/after'}
if not a.execute:print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(parents=True,exist_ok=False);tmp='/data/local/tmp/dm-panvk-bc3-'+uuid.uuid4().hex
result={**plan,'stage':'preflight','cleanup_errors':[],'owned_tmp':tmp};made_tmp=False;installation_requested=False;installed=False;installer=None;launch=None;original_before=None;first_pid=None;active_phase=None

def validate_rows(rows,first_pid):
 final=[r for r in rows if r.get('BC3_app_terminal')is True];assert len(final)==1
 terminal=final[0];assert terminal['rc']==0 and terminal['pid']==int(first_pid)and 10000<=terminal['uid']<200000 and terminal['vendor_id']==0x13b5 and terminal['shared']is False and terminal['game_modules_executed']is False
 config=[r for r in rows if r.get('BC3_app_configuration')is True];assert len(config)==1
 c=config[0];assert c['pid']==int(first_pid)and c['uid']==terminal['uid']and c['provider']=='panvk'and c['shared']is False and c['TRACE']is False and c['game_modules_executed']is False
 body=[r for r in rows if r.get('test')=='original022-BC4-layered-T2T'];assert len(body)==1
 row=body[0];assert row['result']=='passed'and row['BC_format']==139 and row['mips_copied']==7 and row['destination_layers']==252 and row['only_layer_read']==250 and row['source_levels']==8 and row['destination_levels']==7 and row['sampled_pixels']==43688 and row['raw_BC_bytes_compared']==21856 and row['backend_CPU_BC_decode']is False and row['game_modules_executed']is False and row['checks']==87454
 assert row['NVN_submissions']==5 and row['independent_command_objects']==5 and row['explicit_middle_drains']==0 and row['final_fence_waits']==1
 case=[r for r in rows if 'BC3_app_case'in r];assert case==[{'BC3_app_case':'bc4-layered'}]
 assert not any(r.get('test')in ['original022-BC3-capture-focused','original022-BC3-capture-queued21']for r in rows)
 return terminal

def validate_bc_shadow(log):
 banners=re.findall(r'^\[MESA-PANVK\] panvk_debug: ([^\n]+)$',log,re.M)
 assert banners and all(set(v.split('|'))=={'startup','bc_trace'}for v in banners),'Exact startup+bc_trace required; no TRACE/SYNC/DUMP/metadata'
 for levels,layers in [(8,1),(7,252)]:assert re.search(r'^\[MESA-PANVK\] bc_img: fmt=139 128x128x1 mips='+str(levels)+r' layers='+str(layers)+r' usage=0x[0-9a-f]+$',log,re.M),'Requested native ICD emulatedBC4 shadow not observed'
 copies=re.findall(r'^\[MESA-PANVK\] bc_copyimg: sfmt=139 dfmt=139 regions=1$',log,re.M);assert len(copies)==7,'All7 actual BC4 T2T command recordings required'
 decode=re.findall(r'^\[MESA-PANVK\] bc_decode: fmt=139 idx=1 level=(\d+) layer=(\d+)\+1 ext=(\d+)x(\d+)x1 disp=\d+x\d+x1 src=0x[0-9a-f]+ dst=0x[0-9a-f]+$',log,re.M)
 coverage={layer:sorted({int(m)for m,l,w,h in decode if int(l)==layer and int(m)<7 and int(w)==128>>int(m)and int(h)==128>>int(m)})for layer in [0,250]}
 assert all(v==list(range(7))for v in coverage.values()),'Source/target BC4 decode mip coverage incomplete'
 return {'BC4_emulated_images_requested_shapes_observed':True,'T2T_API_record_count':len(copies),'decode_API_mips_by_layer':coverage,'recorded_decode_lines':len(decode),'scope':'DriverCPU command-recording evidence only; combined actual GPU pixel/raw oracle is required. Original preservation may initialize other layers/mips; these are not sampled bythetest.'}

def save():(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
def ad(*args,check=True):
 p=subprocess.run(B+list(map(str,args)),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15);error=p.stderr.decode(errors='replace')
 if p.returncode and(check or any(x in error.lower()for x in ('device offline','no devices','device not found','connection reset','error: closed'))):raise RuntimeError(repr(args)+': '+error)
 return p.stdout.decode(errors='replace').strip()
def process(name):return ad('shell','pidof',name,check=False)
def snapshot():
 dev=previous.old.common.Device(a.serial);dev.stopped();base=previous.baseline(dev,expected)
 return {'baseline':base,'directories':{n:previous.inventory(dev,n)for n in previous.DIRS.values()},'layers':previous.layers(dev),'properties':{k:previous.get_property(dev,k)for k in previous.PROPERTIES}}
def exact_fixture():
 path=ad('shell','pm','path',pkg,check=False).removeprefix('package:')
 if not path:return False
 assert path.startswith('/')and'\n'not in path
 assert ad('shell','sha256sum',path).split()[0]==m['apk_sha256'],'Unclaimed APK; leave untouched'
 return True

def parse_oracle_log(log):
 rows=[]
 for line in log.splitlines():
  if line.startswith('{'):
   try:rows.append(json.loads(line))
   except json.JSONDecodeError:pass
 if any(x in log for x in ('FATAL','native fault signal=','CHECK line')):return rows,'native-assertion-failed',None
 terminals=[r for r in rows if r.get('BC3_app_terminal')is True]
 if terminals:return rows,('oracle-passed'if terminals[-1].get('rc')==0 else'oracle-error'),terminals[-1]
 return rows,None,None

def read_oracle_log(phase):
 log=ad('exec-out','run-as',pkg,'cat','files/runtime/bc3-test.log',check=False)
 if log:(a.output/(phase['name']+'-vulkan-test.log')).write_text(log)
 return log

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
    safe=any(n.get('text')=='Owned BC4 fixture' for n in tree.iter('node'))
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
 assert previous.no_layers(original_before['layers']),'Existing GPU layer configuration; leave unchanged'
 assert not ad('shell','pm','path',pkg,check=False)and not process(pkg),'Pre-existing test package/process; refuse adoption'
 ad('shell','mkdir',tmp);made_tmp=True;installation_requested=True;install();installed=True;assert exact_fixture()
 assert not process(pkg),'Installer started an unowned process; no relaunch'
 ad('push',a.artifact/'driver/libvulkan_panfrost.so',tmp+'/libvulkan_panfrost.so')
 ad('push',a.artifact/'bc4-layered.bin',tmp+'/bc4-layered.bin');ad('push',a.artifact/'vulkan-fixture',tmp+'/vulkan-fixture')
 ad('shell','run-as',pkg,'mkdir','-p','files/runtime/driver','files/runtime/vulkan-fixture','files/runtime/shader-cache')
 ad('shell','run-as',pkg,'cp',tmp+'/libvulkan_panfrost.so','files/runtime/driver/libvulkan_panfrost.so')
 ad('shell','run-as',pkg,'cp',tmp+'/bc4-layered.bin','files/runtime/bc4-layered.bin')
 ad('shell','run-as',pkg,'cp','-R',tmp+'/vulkan-fixture/.','files/runtime/vulkan-fixture/')
 resource_files={'files/runtime/bc4-layered.bin':m['dataset_sha256'],**{'files/runtime/vulkan-fixture/'+n:h for n,h in shader_files.items()}}
 def verify_resources():
  text=ad('exec-out','run-as',pkg,'sha256sum',*resource_files)
  assert {line.split(None,1)[1].strip():line.split()[0]for line in text.splitlines()}==resource_files
 verify_resources()
 allowed_driver_paths={'/data/user/0/'+pkg+'/files/runtime/driver/libvulkan_panfrost.so','/data/data/'+pkg+'/files/runtime/driver/libvulkan_panfrost.so'}
 driver_alias_stat={}
 for path in sorted(allowed_driver_paths):
  raw=ad('exec-out','run-as',pkg,'stat','-c','%d:%i:%s',path);assert re.fullmatch(r'[0-9]+:[0-9]+:[0-9]+',raw)
  dev,inode,size=map(int,raw.split(':'));assert inode>0 and size==(a.artifact/'driver/libvulkan_panfrost.so').stat().st_size
  assert ad('exec-out','run-as',pkg,'sha256sum',path).split()[0]==m['driver_sha256']
  driver_alias_stat[path]={'device':dev,'major':os.major(dev),'minor':os.minor(dev),'inode':inode,'size':size,'sha256':m['driver_sha256']}
 assert len({(v['device'],v['inode'],v['size'])for v in driver_alias_stat.values()})==1
 result['driver_alias_stat']=driver_alias_stat;save()
 args=['shell','am','start','-W','-n',pkg+'/'+m['activity'],'--es','fixture_directory','/data/user/0/'+pkg+'/files/runtime','--es','fixture_case','bc4-layered']
 phase={'name':'BC4-layered','launch_command':B+args,'launches':1};active_phase=phase;result['runs']=[phase];result['stage']='one-cold-launch';save()
 with(a.output/'launch.txt').open('w')as f:launch=subprocess.Popen(B+args,stdout=f,stderr=subprocess.STDOUT)
 deadline=time.monotonic()+75
 while True:
  log=read_oracle_log(phase);rows,stage,terminal=parse_oracle_log(log);phase['rows']=rows
  if stage=='native-assertion-failed':phase['native_terminal_seen']=True
  current,kind=observe_pid(phase,allow_claim=not phase.get('native_terminal_seen',False))
  if current and kind=='owned'and'ordinary_UID'not in result:
   uid=ad('exec-out','run-as',pkg,'id','-u');status=ad('exec-out','run-as',pkg,'cat','/proc/'+first_pid+'/status')
   (a.output/'first-status.txt').write_text(status);u=re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',status,re.M)
   assert uid.isdecimal()and 10000<=int(uid)<200000 and u and all(v==uid for v in u.groups());result['ordinary_UID']=int(uid);save()
  if stage=='native-assertion-failed'or kind in ('ambiguous','replacement','unclaimed'):
   wait_natural_exit(phase);raise RuntimeError('Native failure/ownership loss; no retry')
  if stage is not None:
   assert first_pid is not None and kind=='owned';phase['stage']=stage;phase['terminal']=terminal;save();break
  if first_pid is not None and not current:raise RuntimeError('Owned process exited without terminal; no retry')
  if time.monotonic()>=deadline:raise TimeoutError('Single bounded functional test; no relaunch')
  time.sleep(.2)
 current,kind=observe_pid(phase,allow_claim=False);assert kind=='owned'and current==first_pid
 maps=ad('exec-out','run-as',pkg,'cat','/proc/'+first_pid+'/maps');(a.output/'maps.txt').write_text(maps)
 assert stage=='oracle-passed';terminal=validate_rows(rows,first_pid);assert terminal['uid']==result['ordinary_UID']
 phase['actual_BC4_shadow']=validate_bc_shadow(log);save()
 loader_paths=re.findall(r'provider=panvk direct ICD=([^\s]+)',log);assert len(loader_paths)==1 and loader_paths[0]in allowed_driver_paths
 assert not re.search(r'VUID-|FATAL|native fault signal=|CHECK line',log)
 assert not any(x in maps.lower()for x in ('renderdoc','libvklayer_','liblwi','libvulkan_freedreno'))
 mapped=[]
 for line in maps.splitlines():
  c=line.split(None,5)
  if len(c)!=6 or c[5]not in allowed_driver_paths:continue
  major,minor=(int(x,16)for x in c[3].split(':'));inode=int(c[4]);st=driver_alias_stat[c[5]]
  assert(major,minor,inode)==(st['major'],st['minor'],st['inode']);mapped.append(line)
 assert mapped;phase['exact_private_driver_mappings']=mapped;verify_resources()
 assert all(ad('exec-out','run-as',pkg,'sha256sum',p).split()[0]==m['driver_sha256']for p in allowed_driver_paths)
 launch.wait(timeout=5);stop_owned(phase);result['stage']='actual-original022-BC4-layered-owned-passed'
except BaseException as e:result.update(stage='failed-preserved',error=repr(e))
finally:
 try:
  for client in(installer,launch):
   if client is not None and client.poll()is None:
    client.terminate()
    try:client.wait(timeout=3)
    except subprocess.TimeoutExpired:client.kill();client.wait(timeout=3)
  if installation_requested and not installed:installed=exact_fixture()
  if installed:
   stop_owned();assert not process(pkg);result['fixture_PID_absent']=True
   # Preserve final native failure output before uninstall; never restart.
   if active_phase is not None:read_oracle_log(active_phase)
   assert exact_fixture();ad('uninstall',pkg);assert not ad('shell','pm','path',pkg,check=False);result['fixture_uninstalled']=True
  if made_tmp:
   # This unique empty-created directory only receives our seven known files
   # and installer XML; refuse unexpected paths before deleting the tree.
   names=ad('shell','find',tmp,'-type','f').splitlines();allowed={tmp+'/libvulkan_panfrost.so',tmp+'/bc4-layered.bin',tmp+'/installer.xml',*(tmp+'/vulkan-fixture/'+n for n in shader_files)}
   assert all(n in allowed for n in names)and not ad('shell','find',tmp,'-type','l')
   ad('shell','rm','-r',tmp);ad('shell','test','!','-e',tmp);result['fixture_tmp_absent']=True
  result['owned_clients_stopped']=all(c is None or c.poll()is not None for c in(installer,launch))
 except BaseException as e:result['cleanup_errors'].append(repr(e))
 try:
  if original_before is not None:
   result['original_after']=snapshot();assert result['original_after']==original_before;result['original_unchanged']=True
 except BaseException as e:result['cleanup_errors'].append(repr(e))
 save()
print(json.dumps(result));raise SystemExit(0 if result['stage']=='actual-original022-BC4-layered-owned-passed'and not result['cleanup_errors']else 1)
