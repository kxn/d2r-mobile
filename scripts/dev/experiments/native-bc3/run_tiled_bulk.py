#!/usr/bin/env python3
"""One install, explicit bounded cold batches, one stopped archive pull per batch."""
import argparse, hashlib, io, json, re, subprocess, time, uuid, signal, zipfile, tempfile, shutil, os, shlex
from types import SimpleNamespace
import native_batch_installer as installer_helper
from native_batch_outputs import output_limits,extract_outputs,validate_outputs
from pathlib import Path
P=argparse.ArgumentParser()
P.add_argument('--serial',required=True)
P.add_argument('--artifact',type=Path,required=True)
P.add_argument('--driver',type=Path,required=True)
P.add_argument('--resource-input',type=Path,required=True,help='complete batch prepare root')
P.add_argument('--first-batch',type=int,required=True)
P.add_argument('--batch-count',type=int,required=True)
P.add_argument('--driver-query',type=Path,required=True,help='prior actual aa185 query identity; all three UUIDs must match')
P.add_argument('--original-artifact',type=Path,help='exact locally cached original APK, verified by remote package SHA; no large exec-out pull')
P.add_argument('--module-report',type=Path)
P.add_argument('--resource-report',type=Path)
P.add_argument('--expected-original-apk')
P.add_argument('--expected-original-elf')
P.add_argument('--expected-shaderchain')
P.add_argument('--output',type=Path,required=True)
P.add_argument('--execute',action='store_true')
a=P.parse_args();R=Path(__file__).resolve().parents[4]
pkg='com.d2rmobile.turniprealproducer';original='com.d2rmobile.nativeport'
B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial]
def sha(p):
 with Path(p).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
PRODUCER_SHA='9194548d1b1c897d299d6f42b9336aa445a48f39c21f099a9b806b77fbace8fa'
m=json.loads((a.artifact/'candidate.apk.build.json').read_text())
assert m['package']==pkg and m['activity']=='com.d2rmobile.nativeport.MainActivity'
assert sha(a.artifact/'candidate.apk')==m['apk_sha256']
with zipfile.ZipFile(a.artifact/'candidate.apk') as archive:
 for leaf,digest in m['native_library_sha256'].items():
  assert hashlib.sha256(archive.read('lib/arm64-v8a/'+leaf)).hexdigest()==digest
assert len(m['native_library_sha256'])==6
assert sha(a.driver)=='3e9376ed3759af2cd015cf5608274f661ba3b33df6e95cb6fa2542666f34ee50'
ready=json.loads((a.artifact/'ready.json').read_text())
assert ready['APK']['sha256']==m['apk_sha256'] and ready['producer_source_sha256']==PRODUCER_SHA==m['producer_source_sha256']
assert ready['native_libraries']==m['native_library_sha256']
# Keep the immutable carrier's original driver provenance, and separately bind
# this new experiment's driver build. Never relabel its old exported payloads.
assert ready['driver']['sha256']=='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'
new_build=R/'.local/build/turnip-native-bc3-tiled-v1/build.json'
assert sha(new_build)=='2bd2a4dda07326e662a2b1b706db0b7dde0c977a67a1efe3cb90e8b06c0b8fb4'
new_driver=json.loads(new_build.read_text());assert new_driver['complete'] and new_driver['driver']['sha256']==sha(a.driver)
assert sha(Path(ready['driver_build']['path']))==ready['driver_build']['sha256']
assert sha(a.resource_input/'batch-prepare.json')=='320bac7f3b4beaa8ecd9cd4dba55c795f4d612c1c5010dbbe07f368f0086001f' 
prepared=json.loads((a.resource_input/'batch-prepare.json').read_text())
assert prepared['complete'] and prepared['batch_max_entries']==128
assert len(prepared['entries'])==6217 and len(prepared['batches'])==49
assert a.first_batch>=0 and 1<=a.batch_count<=49 and a.first_batch+a.batch_count<=len(prepared['batches'])
selected={row['id']:row for row in prepared['entries']}
assert len(selected)==len(prepared['entries'])
work=[]
for index in range(a.first_batch,a.first_batch+a.batch_count):
 batch=prepared['batches'][index];assert batch['ordinal']==index
 manifest=Path(batch['manifest']['path']);assert sha(manifest)==batch['manifest']['sha256']
 entries=[selected[identity] for identity in batch['entry_ids']]
 assert 0<len(entries)<=128 and len(set(batch['entry_ids']))==len(entries)
 hashes={'resource-batch.tsv':sha(manifest)}
 for row in entries:
  assert re.fullmatch('[0-9a-f]{64}',row['id'])
  for key,leaf in [('input','resource-input.bin'),('source','resource-source.json')]:
   assert sha(row[key]['path'])==row[key]['sha256']
   hashes['items/'+row['id']+'/'+leaf]=row[key]['sha256']
 work.append({'index':index,'batch':batch,'manifest':manifest,'entries':entries,'input_hashes':hashes})
expected_query=json.loads(a.driver_query.read_text())
assert expected_query['complete'] and expected_query['driverID']==18
assert expected_query['producer_source_sha256']==PRODUCER_SHA
assert expected_query['layout_UUID']=='4504211f903049f5ae3acd7eda74b7bb'
assert all(re.fullmatch('[0-9a-f]{32}',expected_query[k]) for k in ('layout_UUID','device_UUID','driver_UUID'))
baseline_bound=bool(a.original_artifact and a.module_report and a.expected_original_apk and a.expected_original_elf and a.expected_shaderchain)
if a.execute:assert baseline_bound,'Execution requires local exact APK/ELF/28modules/shaderchain bindings'
if a.expected_original_elf:assert re.fullmatch('[0-9a-f]{64}',a.expected_original_elf)
if a.expected_original_apk:assert re.fullmatch('[0-9a-f]{64}',a.expected_original_apk)
if a.expected_shaderchain:assert re.fullmatch('[0-9a-f]{12,64}',a.expected_shaderchain)
expected={}
if a.module_report:
 deployment=json.loads(a.module_report.read_text())
 assert deployment['verified_on_device'] and deployment['serial']==a.serial and deployment['package']==original
 assert 28<=len(deployment['artifacts'])<=64
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
plan={'scope':'New exact native-tiled driver, fresh opaque payloads. ONE installation, explicit finite cold original BC3 UNORM/sRGB batches for the unchanged6217 logical set; existing driver host-copy bake per entry; each stops/pulls/verifies before next; no game/sampling/performance claim',
 'serial':a.serial,'package':pkg,'activity':m['activity'],'APK_sha256':m['apk_sha256'],
 'native_libraries':m['native_library_sha256'],'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),'new_driver_build_sha256':sha(new_build),'carrier_prior_driver_sha256':ready['driver']['sha256'],
 'expected_original_APK':a.expected_original_apk,'expected_original_ELF':a.expected_original_elf,'module_report_sha256':sha(a.module_report) if a.module_report else None,'module_count':len(deployment['artifacts']) if a.module_report else 0,'resource_count':len(resources['expected_hashes']) if a.resource_report else 0,'resource_report_sha256':sha(a.resource_report) if a.resource_report else None,
 'baseline_bound':baseline_bound,'expected_shaderchain':a.expected_shaderchain,
 'crash_natural_exit_bound_seconds':30,'functional_timeout_seconds_per_batch':120,'installation_timeout_seconds':100,
 'launches_allowed':a.batch_count,'first_batch':a.first_batch,'batch_count':a.batch_count,
 'batches':[{'index':w['index'],'entries':len(w['entries']),'manifest_sha256':sha(w['manifest'])} for w in work],
 'entry_count':sum(len(w['entries']) for w in work),'prepare_manifest_sha256':sha(a.resource_input/'batch-prepare.json'),
 'driver_query_sha256':sha(a.driver_query),'producer_source_sha256':PRODUCER_SHA,'GUP_unprotect_retry':False,
 'installer_helper_sha256':sha(Path(installer_helper.__file__)),
 'output_helper_sha256':sha(Path(__file__).with_name('native_batch_outputs.py')),
 'writes_only':'new independent package + unique /data/local/tmp directory; cleanup exact package only'}
if not a.execute:
 print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(exist_ok=False,parents=True)
tmp='/data/local/tmp/dm-turnip-host-image-'+uuid.uuid4().hex
result={**plan,'stage':'preflight','cleanup_errors':[]};made_tmp=False;installation_requested=False;installed=False;launched=False;installer=None;launch=None;original_before=None;first_pid=None;active_phase=None
result['owned_tmp']=tmp

def save():(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
def ad(*args,check=True,timeout=15):
 p=subprocess.run(B+list(map(str,args)),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout)
 error=p.stderr.decode(errors='replace')
 if p.returncode and (check or any(x in error.lower() for x in ('device offline','no devices','device not found','connection reset','error: closed'))):
  raise RuntimeError(repr(args)+': '+error)
 return p.stdout.decode(errors='replace').strip()
def process(name):return ad('shell','pidof',name,check=False)
def validate_driver_maps(maps_text,aliases):
 mappings=[]
 for line in maps_text.splitlines():
  columns=line.split(None,5)
  if len(columns)!=6 or columns[5] not in aliases:continue
  expected=aliases[columns[5]]
  major,minor=(int(v,16) for v in columns[3].split(':'))
  assert (major,minor,int(columns[4]))==(expected['major'],expected['minor'],expected['inode'])
  mappings.append(line)
 assert mappings,'Exact private driver path/device/inode missing from owned live PID maps'
 return mappings

def valid_provider(log,allowed_paths):
 paths=[line.split('[vulkan loader] custom=',1)[1].split()[0] for line in log.splitlines() if '[vulkan loader] custom=' in line]
 return len(paths)==1 and paths[0] in allowed_paths

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
 # Reuse accepted e391 installer verbatim: checkbox is the only action until
 # a fresh UI snapshot proves checked; exactly one owned adb install client.
 def require(condition,message):
  if not condition:raise AssertionError(message)
 def stopped():
  assert not process(original) and not process(pkg)
 dev=SimpleNamespace(command=B,stopped=stopped)
 installer_helper.install(dev,a.artifact/'candidate.apk',m['apk_sha256'],a.output/'install.txt',require,pkg)
 receipt=a.output/'install-installer/result.json';proof=json.loads(receipt.read_text())
 assert proof['owned_client_complete'] and proof['xml_cleanup_complete'] and proof['install_resolved']
 result['installer_receipt']={'path':str(receipt),'sha256':sha(receipt)};save()

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
 ad('shell','test','!','-e',tmp);ad('shell','mkdir',tmp);made_tmp=True;installation_requested=True
 install();installed=True;assert exact_fixture()
 assert not process(pkg),'Installer unexpectedly opened fixture; no test launch'
 ad('push',a.driver,tmp+'/libvulkan_freedreno.so')
 result['runs']=[]
 for task in work:
  index=task['index'];entries=task['entries'];manifest=task['manifest'];resource_hashes=task['input_hashes']
  name=f'batch{index:04d}';private='files/.native-bulk-'+Path(tmp).name+'-'+name
  assert exact_fixture() and not process(pkg),'Fresh owned process required for every batch'
  assert not process(original),'Original game must remain stopped'
  ad('shell','run-as',pkg,'test','!','-e','files/runtime')
  ad('shell','run-as',pkg,'test','!','-e',private)
  with tempfile.TemporaryDirectory(prefix='native-resource-batch-stage-') as staging:
   stage=Path(staging)/name;stage.mkdir()
   shutil.copy2(manifest,stage/'resource-batch.tsv')
   for row in entries:
    dest=stage/'items'/row['id'];dest.mkdir(parents=True)
    for key,leaf in [('input','resource-input.bin'),('source','resource-source.json')]:shutil.copy2(row[key]['path'],dest/leaf)
   ad('push',stage,tmp+'/'+name,timeout=180)
  ad('shell','run-as',pkg,'mkdir','-p',private+'/vulkan-driver',private+'/vulkan-fixture')
  ad('shell','run-as',pkg,'cp',tmp+'/'+name+'/resource-batch.tsv',private+'/resource-batch.tsv')
  ad('shell','run-as',pkg,'cp','-R',tmp+'/'+name+'/items',private+'/items')
  ad('shell','run-as',pkg,'cp',tmp+'/libvulkan_freedreno.so',private+'/vulkan-driver/libvulkan_freedreno.so')
  ad('shell','run-as',pkg,'mv',private,'files/runtime')
  raw=ad('exec-out','run-as',pkg,'sha256sum',*['files/runtime/'+n for n in resource_hashes])
  assert {line.split(None,1)[1].strip().removeprefix('files/runtime/'):line.split()[0] for line in raw.splitlines()}==resource_hashes
  driver_alias_stat={}
  for path in (f'/data/data/{pkg}/files/runtime/vulkan-driver/libvulkan_freedreno.so',f'/data/user/0/{pkg}/files/runtime/vulkan-driver/libvulkan_freedreno.so'):
   raw_stat=ad('exec-out','run-as',pkg,'stat','-c','%d:%i:%s',path)
   dev,inode,size=map(int,raw_stat.split(':'));digest=ad('exec-out','run-as',pkg,'sha256sum',path).split()[0]
   assert inode>0 and size==a.driver.stat().st_size and digest==sha(a.driver)
   driver_alias_stat[path]={'major':os.major(dev),'minor':os.minor(dev),'inode':inode,'size':size,'device':dev,'sha256':digest,'raw':raw_stat}
  assert len({(v['device'],v['inode'],v['size']) for v in driver_alias_stat.values()})==1
  kind='resource-bake-batch';test_name='native-host-image-payload'
  args=['shell','am','start','-W','-n',pkg+'/'+m['activity'],'--ez','run_game','false',
   '--ez','native_image_test','true','--es','native_image_mode','--'+kind,'--es','vulkan_driver','adreno','--ez','vulkan_test_validation','false']
  first_pid=None;run={'name':name,'batch_index':index,'private_input_directory':private,'runtime_path_during_owned_PID':'files/runtime','kind':kind,'expected_test':test_name,'launch_command':B+args,'launches':1,'driver_alias_stat':driver_alias_stat};active_phase=run
  result['runs'].append(run);result.update(stage='cold-'+name,launches=len(result['runs']));save();launched=True
  assert not process(pkg),'No process may predate this batch cold launch'
  run['PID_absent_immediately_before_launch']=True;save()
  with (a.output/(name+'-launch.txt')).open('w') as launch_log:launch=subprocess.Popen(B+args,stdout=launch_log,stderr=subprocess.STDOUT)
  deadline=time.monotonic()+120;log='';rows=[];phase=run
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
  assert run.get('stage')=='oracle-passed','Original failure or lost ownership: stop transaction, no later batch or retry'
  final=run['final']
  assert final['result']=='passed' and final['formats']==len(entries) and final['checks']>0
  assert final['mode']=='--'+kind and final['game_modules_executed'] is False
  config=[r for r in rows if r.get('native_image_app_configuration') is True]
  assert len(config)==1 and config[0]['uid']>=10000 and config[0]['mode']=='--'+kind
  assert config[0]['validation_layer']=='disabled_explicitly' and config[0]['synchronization_validation']=='disabled_explicitly'
  assert config[0]['game_modules_executed'] is False
  allowed_paths=set(driver_alias_stat)
  assert valid_provider(log,allowed_paths)
  assert not re.search(r'VUID-|FATAL|native fault signal=|CHECK line',log)
  identity=[r for r in rows if r.get('native_device_identity') is True]
  assert len(identity)==1 and identity[0]['driverID']==18
  assert all(identity[0][k]==expected_query[k] for k in ('layout_UUID','device_UUID','driver_UUID'))
  run['queried_identity']=identity[0]
  maps_text=ad('exec-out','run-as',pkg,'cat','/proc/'+first_pid+'/maps')
  (a.output/(name+'-maps.txt')).write_text(maps_text)
  run['mapped_driver']=validate_driver_maps(maps_text,driver_alias_stat)
  assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/vulkan-driver/libvulkan_freedreno.so').split()[0]==sha(a.driver)
  baked=[r for r in rows if r.get('native_resource_bake_complete') is True]
  assert len(baked)==len(entries) and all(r['GPU_sampling_executed'] is False for r in baked)
  # No live producer while tar is written, transferred, inspected or extracted.
  stop_owned(run);assert not process(pkg)
  run['PID_absent_before_tar']=True;save()
  ad('shell','run-as',pkg,'mv','files/runtime',private)
  list_path=a.output/(name+'-output-list.txt');list_path.write_text(''.join(n+'\n' for n in output_limits(entries)))
  remote_list=tmp+'/'+name+'-output-list.txt';remote_tar=tmp+'/'+name+'-outputs.tar'
  ad('push',list_path,remote_list)
  ad('shell','test','!','-e',remote_tar)
  command=shlex.join(['run-as',pkg,'tar','-C',private,'-c','-T',remote_list])+' > '+shlex.quote(remote_tar)
  ad('shell',command,timeout=180)
  remote_sha=ad('shell','sha256sum',remote_tar).split()[0]
  archive=a.output/(name+'-outputs.tar')
  ad('pull',remote_tar,archive,timeout=180)
  assert sha(archive)==remote_sha
  destination=a.output/'batches'/f'{index:04d}'
  run['baked_outputs']=extract_outputs(archive,destination,entries)
  run['actual_batch_complete']=validate_outputs(destination,entries,sha(manifest),identity[0],PRODUCER_SHA)
  run['archive']={'path':str(archive),'sha256':remote_sha,'bytes':archive.stat().st_size,'members':len(run['baked_outputs'])}
  run['output_directory']=str(destination)
  run.update(result='passed',PID_absent_after=True);save()
  # Only this fresh independent package's stopped owned phase is retired.
  assert not process(pkg)
  ad('shell','run-as',pkg,'rm','-r',private)
  ad('shell','rm','-r',tmp+'/'+name)
  ad('shell','rm',remote_list,remote_tar)
  run['owned_phase_removed_after_verified_pull']=True;save()
 result.update(stage='actual-turnip-native-resource-batches-owned-passed',
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
# Compatible per-batch views are published only after the ONE outer transaction
# has stopped/uninstalled/cleaned up and reverified the untouched original app.
if result['stage']=='actual-turnip-native-resource-batches-owned-passed' and not result['cleanup_errors']:
 try:
  assert all(result.get(key) is True for key in ('original_unchanged','fixture_PID_absent','fixture_uninstalled','fixture_tmp_absent','owned_clients_stopped'))
  receipt_index=[];specs=[]
  for task,run in zip(work,result['runs']):
   assert run['batch_index']==task['index'] and run['result']=='passed'
   root=Path(run['output_directory'])
   view={**plan,'stage':'actual-turnip-native-resource-batch-owned-passed','cleanup_errors':[],
    'batch_index':task['index'],'batch_manifest_sha256':sha(task['manifest']),'entry_count':len(task['entries']),
    'runs':[run],'baked_outputs':run['baked_outputs'],'actual_batch_complete':run['actual_batch_complete'],
    'original_before':result['original_before'],'original_after':result['original_after'],
    'outer_transaction_result':str(a.output/'result.json'),
    **{key:result[key] for key in ('original_unchanged','fixture_PID_absent','fixture_uninstalled','fixture_tmp_absent','owned_clients_stopped')}}
   path=root/'result.json';assert not path.exists();path.write_text(json.dumps(view,indent=2)+'\n')
   receipt_index.append({'path':str(path),'sha256':sha(path)})
   for row in task['entries']:specs.append({'source':row['source']['path'],'texture':row['texture']['path'],'baked':str(root/'items'/row['id'])})
  assert len(receipt_index)==a.batch_count
  index={'schema':1,'complete':True,'producer_source_sha256':PRODUCER_SHA,
   'source_pack_manifest_sha256':plan['prepare_manifest_sha256'],'results':receipt_index,
   'scope':'Requested explicit batches only; complete full native composition still requires every prepared entry',
   'first_batch':a.first_batch,'batch_count':a.batch_count,'entries':len(specs)}
  (a.output/'actual-batch-receipt-index.json').write_text(json.dumps(index,indent=2)+'\n')
  (a.output/'baked-entries.json').write_text(json.dumps(specs,indent=2)+'\n')
  result['receipt_index_sha256']=sha(a.output/'actual-batch-receipt-index.json');save()
 except BaseException as error:
  result.update(stage='failed-preserved',postcleanup_publication_error=repr(error));save()
print(json.dumps(result))
raise SystemExit(0 if result['stage']=='actual-turnip-native-resource-batches-owned-passed' and not result['cleanup_errors'] else 1)
