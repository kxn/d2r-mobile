#!/usr/bin/env python3
"""Four cold owned8KiB standard-API controls; default offline plan, never a profiler."""
import argparse,hashlib,io,json,re,signal,subprocess,time,uuid,zipfile,xml.etree.ElementTree as ET
from pathlib import Path
P=argparse.ArgumentParser(description=__doc__)
P.add_argument('--serial',required=True);P.add_argument('--artifact',type=Path,required=True)
P.add_argument('--driver',type=Path,required=True);P.add_argument('--module-report',type=Path,required=True)
P.add_argument('--expected-original-apk',required=True);P.add_argument('--expected-original-elf',required=True)
P.add_argument('--expected-shaderchain',required=True);P.add_argument('--resource-report',type=Path,required=True)
P.add_argument('--output',type=Path,required=True)
P.add_argument('--matrix-order',nargs=4,choices=['private-plain','private-dirty','shared-plain','shared-dirty'],default=['private-plain','private-dirty','shared-plain','shared-dirty'])
P.add_argument('--execute',action='store_true');a=P.parse_args()
if len(set(a.matrix_order))!=4:P.error('--matrix-order must be a permutation of all four controls')
R=Path(__file__).resolve().parents[2];pkg='com.d2rmobile.sharedanondirtyfixture';original='com.d2rmobile.nativeport';assert pkg!=original
B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial];sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((a.artifact/'candidate.apk.build.json').read_text());assert m['package']==pkg and m['activity']=='com.d2rmobile.nativeport.MainActivity'
assert sha(a.artifact/'candidate.apk')==m['apk_sha256']
assert sha(a.driver)=='bdd9df32c38f7c82fb58d815d6a021e8641a545f9fdb1b401416d6908d265d5d'
with zipfile.ZipFile(a.artifact/'candidate.apk')as z:
 for leaf,digest in m['native_library_sha256'].items():assert hashlib.sha256(z.read('lib/arm64-v8a/'+leaf)).hexdigest()==digest
assert len(m['native_library_sha256'])==6
for v in (a.expected_original_apk,a.expected_original_elf):assert re.fullmatch('[0-9a-f]{64}',v)
assert re.fullmatch('[0-9a-f]{12,64}',a.expected_shaderchain)
deployment=json.loads(a.module_report.read_text());assert deployment['verified_on_device']and deployment['serial']==a.serial and deployment['package']==original and len(deployment['artifacts'])==28
expected={}
for name,item in deployment['artifacts'].items():
 assert re.fullmatch('[A-Za-z0-9_.-]+',name)and name not in ('.','..')and re.fullmatch('[0-9a-f]{64}',item['sha256'])
 expected['files/runtime/modules/'+name]=item['sha256']
resources=json.loads(a.resource_report.read_text());assert resources['stage']=='deployed'and resources['phone_hashes_verified']and len(resources['expected_hashes'])==6
for name,digest in resources['expected_hashes'].items():
 assert re.fullmatch('[A-Za-z0-9_./-]+',name)and not Path(name).is_absolute()and '..'not in Path(name).parts and re.fullmatch('[0-9a-f]{64}',digest)
 expected['files/runtime/romfs_overlay/'+name]=digest
matrix_modes={'private-plain':(False,False),'private-dirty':(False,True),'shared-plain':(True,False),'shared-dirty':(True,True)}
matrix=[(name,*matrix_modes[name])for name in a.matrix_order]
plan={'plan_only':not a.execute,'scope':'Four isolated ordinaryUID standard8KiB HOST_ALLOCATION byte controls, no game; no original-package writes, no performance claim',
 'serial':a.serial,'package':pkg,'activity':m['activity'],'APK_sha256':m['apk_sha256'],'native_libraries':m['native_library_sha256'],'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),
 'expected_original_APK':a.expected_original_apk,'expected_original_ELF':a.expected_original_elf,'expected_shaderchain':a.expected_shaderchain,
 'module_report_sha256':sha(a.module_report),'resource_report_sha256':sha(a.resource_report),'module_count':28,'resource_count':6,
 'matrix':[{'name':name,'shared_anonymous':shared,'owned_dirty':dirty,'implicit_host_dependency':False}for name,shared,dirty in matrix],
 'functional_timeout_seconds_each':45,'crash_natural_exit_bound_seconds':30,'installation_timeout_seconds':70,'GUP_unprotect_retry':False,'VVL':'explicitlyfalse; direct PanVK cannot compose AndroidVVL, byte assertions retained',
 'cleanup':'owned independent package+unique tmp; originalAPK/ELF/28modules/chain/full4six read-only before/after; refusePIDreplacement',
 'negative_policy':'No expected-failure conversion to pass. Continue cold cells after preserved native assertion if provider/type/control identity is proved; report each failure. Matrix exit1 if any oracle fails.'}
if not a.execute:print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(exist_ok=False,parents=True)
tmp='/data/local/tmp/dm-shared-anon-'+uuid.uuid4().hex
result={**plan,'stage':'preflight','phases':[],'cleanup_errors':[]};made_tmp=False;requested=False;installed=False;installer=None;launch=None;original_before=None;first_pid=None;active_phase=None
save=lambda:(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
def ad(*args,check=True):
 q=subprocess.run(B+list(map(str,args)),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20);err=q.stderr.decode(errors='replace')
 if q.returncode and(check or any(x in err.lower()for x in ('device offline','no devices','device not found','connection reset','error: closed'))):raise RuntimeError(repr(args)+': '+err)
 return q.stdout.decode(errors='replace').strip()
def process(name):return ad('shell','pidof',name,check=False)
def snapshot():
 assert not process(original),'Original game running; leave untouched'
 path=ad('shell','pm','path',original).removeprefix('package:');assert path.startswith('/')and '\n'not in path
 digest=ad('shell','sha256sum',path).split()[0];assert digest==a.expected_original_apk
 raw=subprocess.run(B+['exec-out','cat',path],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20,check=True).stdout
 assert hashlib.sha256(raw).hexdigest()==digest
 with zipfile.ZipFile(io.BytesIO(raw))as z:elf=hashlib.sha256(z.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()
 assert elf==a.expected_original_elf
 rows=ad('exec-out','run-as',original,'sha256sum',*expected).splitlines();got={v.split(None,1)[1].strip():v.split()[0]for v in rows};assert got==expected
 chain=ad('exec-out','run-as',original,'cat','files/runtime/shaders/.chain');assert chain==a.expected_shaderchain
 return {'APK':digest,'ELF':elf,'modules_and_resources':got,'shaderchain':chain,'PID_absent':True}
def exact_fixture():
 path=ad('shell','pm','path',pkg,check=False).removeprefix('package:')
 if not path:return False
 assert path.startswith('/')and '\n'not in path and ad('shell','sha256sum',path).split()[0]==m['apk_sha256'],'Unknownfixturepackage; leave untouched'
 return True
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
 finals=[r for r in rows if r.get('test')=='host-import-standard-api']
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
 with(a.output/'install.txt').open('w')as out:
  installer=subprocess.Popen(B+['install','--no-incremental',str(a.artifact/'candidate.apk')],stdout=out,stderr=subprocess.STDOUT)
  deadline=time.monotonic()+70;approvals=set()
  while installer.poll()is None:
   if time.monotonic()>deadline:raise TimeoutError('singleinstallpending; noretry')
   remote=tmp+'/installer.xml';ad('shell','uiautomator','dump',remote);xml=ad('exec-out','cat',remote)
   try:tree=ET.fromstring(xml)
   except ET.ParseError:tree=None
   if tree is not None:
    safe=any(n.get('text')=='D2R Mobile'for n in tree.iter('node'))
    nodes=[n for n in tree.iter('node')if(n.get('package')=='com.android.packageinstaller'and n.get('resource-id')=='android:id/button1'and n.get('text')in('Install','安装','继续安装'))or(n.get('package')=='com.oplus.appdetail'and n.get('resource-id','').endswith('/btn_left')and n.get('text')=='Install')]
    if safe and len(nodes)==1:
     key=nodes[0].get('package')+':'+nodes[0].get('resource-id')
     if key not in approvals:
      (a.output/('installer-'+str(len(approvals))+'.xml')).write_text(xml);x,y,z,w=map(int,re.findall(r'\d+',nodes[0].get('bounds')))
      ad('shell','input','tap',str((x+z)//2),str((y+w)//2));approvals.add(key)
   time.sleep(.5)
  assert installer.returncode==0,(a.output/'install.txt').read_text()
def interrupted(sig,frame):raise KeyboardInterrupt('fixture interrupted; owned cleanup')
signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
try:
 original_before=snapshot();result['original_before']=original_before;save()
 assert not ad('shell','pm','path',pkg,check=False)and not process(pkg),'Preexistingfixturepackage/PID not adopted'
 enabled=ad('shell','settings','get','global','enable_gpu_debug_layers');target=ad('shell','settings','get','global','gpu_debug_app')
 assert not(enabled=='1'and target==pkg),'ExternalGPUlayer targetsfixture; no settings mutation'
 result['external_layer_preflight']={'enabled':enabled,'app':target}
 ad('shell','mkdir','-p',tmp);made_tmp=True;requested=True;install();installed=True;assert exact_fixture()and not process(pkg)
 ad('push',a.driver,tmp+'/libvulkan_panfrost.so');ad('shell','run-as',pkg,'mkdir','-p','files/runtime/vulkan-panfrost','files/runtime/vulkan-fixture')
 ad('shell','run-as',pkg,'cp',tmp+'/libvulkan_panfrost.so','files/runtime/vulkan-panfrost/libvulkan_panfrost.so')
 assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/vulkan-panfrost/libvulkan_panfrost.so').split()[0]==sha(a.driver)
 for name,shared,dirty in matrix:
  assert not process(pkg);first_pid=None;phase={'name':name,'requested_shared':shared,'requested_dirty':dirty,'stage':'launch'};active_phase=phase;result['phases'].append(phase);save()
  ad('shell','run-as',pkg,'rm','-f','files/runtime/vulkan-test.log')
  args=['shell','am','start','-W','-n',pkg+'/'+m['activity'],'--ez','run_game','false','--ez','run_vulkan_tests','true','--es','vulkan_driver','panvk','--ez','vulkan_test_validation','false','--ez','host_import_opt_in','true','--ez','host_import_test','true','--ez','host_import_implicit_host','false','--ez','host_import_shared_anonymous',str(shared).lower(),'--ez','host_import_owned_dirty',str(dirty).lower()]
  phase['command']=B+args
  with(a.output/(name+'-launch.txt')).open('w')as out:
   launch=subprocess.Popen(B+args,stdout=out,stderr=subprocess.STDOUT);deadline=time.monotonic()+45;log='';rows=[]
   while True:
    # Retain oracle bytes/FATAL before examining transient crash PID lists.
    log=read_oracle_log(phase);rows,stage,final=parse_oracle_log(log);phase['rows']=rows
    if stage=='native-assertion-failed':phase['native_terminal_seen']=True;phase['stage']=stage
    current,kind=observe_pid(phase,allow_claim=not phase.get('native_terminal_seen',False))
    if stage=='native-assertion-failed' or kind in ('ambiguous','replacement','unclaimed'):
     log=wait_natural_exit(phase);rows,stage,final=parse_oracle_log(log)
     # Final JSON has no PID: after lost ownership a pass cannot be
     # attributed to this launch. Only preserve an explicit native failure.
     phase['stage']='native-assertion-failed' if stage=='native-assertion-failed' else 'ownership-failed'
     phase['terminal_after_unowned_wait']=stage
     if final is not None:phase['untrusted_final']=final
     break
    if stage is not None:
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
   launch.wait(timeout=5);phase['launch_exit']=launch.returncode
  phase['rows']=rows;phase['provider_panvk']='provider=panvk'in log
  memory_rounds=[dict(round=int(r),type=int(t),flags=int(f,16),coherent=int(c),atom=int(n))for r,t,f,c,n in re.findall(r'round=(\d+) import_type=(\d+) flags=(0x[0-9a-f]+) host_coherent=(\d+) atom=(\d+)',log)]
  phase['memory_rounds']=memory_rounds
  phase['actual_noncoherent_0xb']=bool(memory_rounds)and all(v['flags']==11 and v['coherent']==0 and v['atom']==64 for v in memory_rounds)
  phase['validation_explicitly_disabled']=any(r.get('validation_layer')=='disabled_explicitly'for r in rows)
  configuration=[r for r in rows if r.get('test_configuration')=='host-import-anonymous-mapping'];owned=[r for r in rows if r.get('test_configuration')=='standard-owned-dirty']
  assert phase['provider_panvk']and phase['actual_noncoherent_0xb']and phase['validation_explicitly_disabled'],'Provider/type/validationnotproved'
  assert len(configuration)==len(owned)==1 and configuration[0]['kind']==('shared'if shared else'private')and owned[0]['enabled']==dirty,'Actualmatrixmode mismatch'
  if phase['stage']=='oracle-passed':
   final=phase['final'];assert final['uid']>=10000 and final['rounds']==4 and final['import_bytes']==8192 and final['alignment']==4096 and final['checks']>50000
   assert [v['round']for v in memory_rounds]==[0,1,2,3]
   cycles=[r for r in rows if r.get('test_details')=='standard-owned-dirty-cycle'];assert len(cycles)==(12 if dirty else 0)
   if dirty:assert all(r['cpu_first']==r['GPU_copy_readback_first']for r in cycles)
  phase['oracle_passed']=phase['stage']=='oracle-passed';save()
  (a.output/(name+'-app-events.txt')).write_text(ad('logcat','-d','-v','threadtime','-s','AndroidRuntime:E','DEBUG:E','libc:E','MESA:I'))
  stop_owned(phase);phase['PID_absent_after']=True;save()
  if phase['stage'] not in ('oracle-passed','native-assertion-failed'):raise RuntimeError('Infrastructure/unknownresult: stopmatrixwithoutretry')
 result['all_four_oracles_passed']=all(p['oracle_passed']for p in result['phases'])
 result['stage']='all-four-byte-oracles-passed'if result['all_four_oracles_passed']else'four-controls-completed-with-oracle-failures'
except BaseException as error:result.update(stage='failed-preserved',error=repr(error))
finally:
 try:
  for client in(installer,launch):
   if client is not None and client.poll()is None:
    client.terminate()
    try:client.wait(timeout=3)
    except subprocess.TimeoutExpired:client.kill();client.wait(timeout=3)
  if requested and not installed:installed=exact_fixture()
  if installed:
   stop_owned();result['fixture_PID_absent']=True
   assert exact_fixture();ad('uninstall',pkg);assert not ad('shell','pm','path',pkg,check=False);result['fixture_uninstalled']=True
  if made_tmp:ad('shell','rm','-r',tmp);ad('shell','test','!','-e',tmp);result['fixture_tmp_absent']=True
  result['owned_clients_stopped']=all(c is None or c.poll()is not None for c in(installer,launch))
  if original_before is not None:result['original_after']=snapshot();assert result['original_after']==original_before;result['original_unchanged']=True
 except BaseException as error:result['cleanup_errors'].append(repr(error))
 save()
print(json.dumps(result,indent=2));raise SystemExit(0 if result['stage']=='all-four-byte-oracles-passed'and not result['cleanup_errors']else 1)
