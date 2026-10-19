#!/usr/bin/env python3
"""One isolated appUID original NVN owned-buffer functional run; not profiling."""
import argparse, hashlib, json, re, subprocess, time, uuid, xml.etree.ElementTree as ET
from pathlib import Path
P=argparse.ArgumentParser()
P.add_argument('--serial',required=True)
P.add_argument('--artifact',type=Path,required=True)
P.add_argument('--driver',type=Path,required=True)
P.add_argument('--module-report',type=Path,required=True)
P.add_argument('--expected-original-apk',required=True)
P.add_argument('--output',type=Path,required=True)
P.add_argument('--execute',action='store_true')
a=P.parse_args();R=Path(__file__).resolve().parents[2]
pkg='com.d2rmobile.ownednvnfixture';original='com.d2rmobile.nativeport'
B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',a.serial]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((a.artifact/'candidate.apk.build.json').read_text())
assert m['package']==pkg and m['activity']=='com.d2rmobile.nativeport.MainActivity'
assert sha(a.artifact/'candidate.apk')==m['apk_sha256']
assert sha(a.driver)=='bdd9df32c38f7c82fb58d815d6a021e8641a545f9fdb1b401416d6908d265d5d'
assert re.fullmatch('[0-9a-f]{64}',a.expected_original_apk)
deployment=json.loads(a.module_report.read_text())
expected={'files/runtime/modules/'+n:v['sha256'] for n,v in deployment['artifacts'].items()}
assert len(expected)==28
plan={'scope':'ONE own64KiB original NVN shader/draw fixture; game=false; original package read-only; VVL explicitly unavailable/disabled; no performance claim',
 'serial':a.serial,'package':pkg,'activity':m['activity'],'APK_sha256':m['apk_sha256'],
 'native_libraries':m['native_library_sha256'],'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),
 'expected_original_APK':a.expected_original_apk,'module_report_sha256':sha(a.module_report),'module_count':len(expected),
 'functional_timeout_seconds':45,'launches_allowed':1,'GUP_unprotect_retry':False,
 'writes_only':'new independent package + unique /data/local/tmp directory; cleanup exact package only'}
if not a.execute:
 print(json.dumps(plan,indent=2));raise SystemExit(0)
a.output.mkdir(exist_ok=False,parents=True)
tmp='/data/local/tmp/dm-owned-nvn-'+uuid.uuid4().hex
result={**plan,'stage':'preflight','cleanup_errors':[]};made_tmp=False;installation_requested=False;installed=False;launched=False;installer=None;original_before=None

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
 digest=ad('shell','sha256sum',path).split()[0]
 assert digest==a.expected_original_apk,'Original APK changed since lease: do not install'
 raw=ad('exec-out','run-as',original,'sha256sum',*expected)
 got={line.split(None,1)[1].strip():line.split(None,1)[0] for line in raw.splitlines()}
 assert got==expected,'Original module baseline mismatch'
 chain=ad('exec-out','run-as',original,'cat','files/runtime/shaders/.chain')
 assert chain=='cbd8d2da0b0f'
 return {'APK':digest,'modules':got,'shaderchain':chain,'PID_absent':True}
def exact_fixture():
 path=ad('shell','pm','path',pkg,check=False).removeprefix('package:')
 if not path:return False
 assert ad('shell','sha256sum',path).split()[0]==m['apk_sha256'],'Unclaimed package; leave untouched'
 return True
def install():
 global installer
 with (a.output/'install.txt').open('w') as log:
  installer=subprocess.Popen(B+['install','--no-incremental',str(a.artifact/'candidate.apk')],stdout=log,stderr=subprocess.STDOUT)
  deadline=time.monotonic()+110;approvals=set()
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
      (n.get('package')=='com.android.packageinstaller' and n.get('resource-id')=='android:id/button1') or
      (n.get('package')=='com.oplus.appdetail' and n.get('resource-id','').endswith('/btn_left') and n.get('text')=='Install')]
    if safe and len(nodes)==1:
     key=nodes[0].get('package')+':'+nodes[0].get('resource-id')
     if key not in approvals:
      (a.output/('installer-'+str(len(approvals))+'.xml')).write_text(xml)
      x,y,z,w=map(int,re.findall(r'\d+',nodes[0].get('bounds')))
      ad('shell','input','tap',str((x+z)//2),str((y+w)//2));approvals.add(key)
   time.sleep(.5)
  assert installer.returncode==0,(a.output/'install.txt').read_text()
try:
 original_before=snapshot();result['original_before']=original_before;save()
 assert not ad('shell','pm','path',pkg,check=False),'Existing independent fixture: do not replace'
 assert not process(pkg)
 ad('shell','mkdir','-p',tmp);made_tmp=True;installation_requested=True
 install();installed=True;assert exact_fixture()
 ad('push',a.driver,tmp+'/libvulkan_panfrost.so')
 ad('shell','run-as',pkg,'mkdir','-p','files/runtime/vulkan-panfrost','files/runtime/vulkan-fixture')
 ad('shell','run-as',pkg,'cp',tmp+'/libvulkan_panfrost.so','files/runtime/vulkan-panfrost/libvulkan_panfrost.so')
 assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/vulkan-panfrost/libvulkan_panfrost.so').split()[0]==sha(a.driver)
 fixture=json.loads((a.artifact/'fixture-hashes.json').read_text())
 # Only original owned GLSL fixture bytes, never game or main package inputs.
 ad('push',a.artifact/'vulkan-fixture',tmp+'/vulkan-fixture')
 ad('shell','run-as',pkg,'cp','-R',tmp+'/vulkan-fixture/.','files/runtime/vulkan-fixture/')
 raw=ad('exec-out','run-as',pkg,'sha256sum',*['files/runtime/vulkan-fixture/'+n for n in fixture])
 got={line.split(None,1)[1].strip().removeprefix('files/runtime/vulkan-fixture/'):line.split()[0] for line in raw.splitlines()}
 assert got==fixture,'Fixture bytes mismatch'
 args=['shell','am','start','-W','-n',pkg+'/'+m['activity'],'--ez','run_game','false',
  '--ez','run_vulkan_tests','true','--ez','owned_nvn_buffer_test','true','--ez','async_submit','true',
  '--es','vulkan_driver','panvk','--ez','vulkan_test_validation','false']
 result.update(stage='one-owned-nvn-launch',launch_command=B+args,launches=1);save();launched=True
 with (a.output/'launch.txt').open('w') as log:launch=subprocess.Popen(B+args,stdout=log,stderr=subprocess.STDOUT)
 deadline=time.monotonic()+45;seen=False;rows=[]
 while True:
  current=process(pkg);seen|=bool(current)
  if current:result['fixture_PID_raw']=current
  log=ad('exec-out','run-as',pkg,'cat','files/runtime/vulkan-test.log',check=False)
  if log:(a.output/'vulkan-test.log').write_text(log)
  rows=[]
  for line in log.splitlines():
   if line.startswith('{'):
    try:rows.append(json.loads(line))
    except json.JSONDecodeError:pass
  finals=[r for r in rows if r.get('test')=='owned-nvn-buffer']
  if finals:result['final']=finals[-1];break
  if 'FATAL' in log or 'native fault signal=' in log:raise RuntimeError('Original assertion/fault preserved; no retry')
  if seen and not current:raise RuntimeError('Fixture exited without final result')
  if time.monotonic()>deadline:raise TimeoutError('45s functional limit; no retry')
  time.sleep(.2)
 launch.wait(timeout=5);result['results']=rows;final=result['final']
 assert final['result']=='passed' and final['checks']==2371 and final['uid']>=10000
 assert final['flags']==11 and final['host_coherent'] is False and final['atom']==64
 assert final['original_nvn'] is True and final['game_modules_executed'] is False and final['owned_pool_bytes']==65536
 assert 'provider=panvk' in log and 'driverID=20' in log
 assert any(r.get('validation_layer')=='disabled_explicitly' for r in rows)
 cycles=[r for r in rows if r.get('test_details')=='owned-nvn-cycle']
 assert len(cycles)==4 and all(r['flags']==11 and r['pixels_and_live_CPU_verified'] is True for r in cycles)
 assert [r['retired'] for r in cycles]==[False,False,False,True]
 flushes=[r for r in rows if r.get('test_details')=='owned-nvn-flush']
 assert len(flushes)>=3 and all(r['flags']==11 and r['atom']==64 and r['offset']%64==0 and r['bytes']%64==0 for r in flushes)
 result.update(stage='actual-noncoherent-owned-NVN-passed',flush_ranges=flushes)
except BaseException as e:result.update(stage='failed-preserved',error=repr(e))
finally:
 try:
  if installer is not None and installer.poll() is None:
   installer.terminate()
   try:installer.wait(timeout=3)
   except subprocess.TimeoutExpired:installer.kill();installer.wait(timeout=3)
  if installation_requested and not installed:installed=exact_fixture()
  if installed:
   (a.output/'app-events.txt').write_text(ad('logcat','-d','-v','threadtime','-s','AndroidRuntime:E','DEBUG:E','libc:E','MESA:I'))
   if process(pkg):
    assert launched,'Unexpected fixture process; do not kill'
    ad('shell','am','force-stop',pkg)
   assert not process(pkg);result['fixture_PID_absent']=True
   assert exact_fixture();ad('uninstall',pkg)
   assert not ad('shell','pm','path',pkg,check=False);result['fixture_uninstalled']=True
  if made_tmp:ad('shell','rm','-r',tmp)
  if original_before is not None:
   result['original_after']=snapshot();assert result['original_after']==original_before
   result['original_unchanged']=True
 except BaseException as e:result['cleanup_errors'].append(repr(e))
 save()
print(json.dumps(result))
raise SystemExit(0 if result['stage']=='actual-noncoherent-owned-NVN-passed' and not result['cleanup_errors'] else 1)
