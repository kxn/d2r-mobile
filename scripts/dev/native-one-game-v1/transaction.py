#!/usr/bin/env python3
"""Reused original-game transaction primitives plus one official LLDB hook.
Reuse the frozen resource Device/Hashes/journal and original launcher/save.
No device operation at import; no profiler or target expression execution.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, shlex, signal, subprocess, sys, tarfile, time, uuid, zipfile
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport'
COMMON=R/'.local/build/deploy-astc6-full6-36a9781-v1.py'
SOURCE=Path(__file__).resolve().parents[3]
DRIVER_READY=R/'.local/logs/turnip-original-q735-v1/source-ready-audit-agent.json'
RESOURCE_PROOF=R/'.local/logs/panvk-original-fork-ui-only-resource-proof-v2.json'
BASELINE=R/'.local/build/turnip-original-q735-v1-frozen/baseline-readonly.json'
DRIVER=R/'.local/build/turnip-host-import-v2/driver/libvulkan_freedreno.so'
UI=R/'.local/build/android-romfs-overlay'
OVERLAY='files/runtime/romfs_overlay';DRIVER_DIR='files/runtime/vulkan-driver'
DIRS=[('driver',DRIVER_DIR),('temp','files/runtime/temp'),('cache','cache')]
LOCK='files/runtime/.astc6-full-controller-lock';OWNER='.original-fork-baseline-owner'
APK='d7f9a720480b696af0ac70acb5773a2d14842a3e4840c030fdbb049b9fe83587'
ELF='6377227d545d878caaf9636209de98814335495e9e71f4aedc0bff61fb03d34a'
SO='3c8710beb5c33016479601603d41f15508ab185c7b18c684ebe3f8e0ecfa3e61';CHAIN='9507875007c6'
spec=importlib.util.spec_from_file_location('existing_resource_journal',COMMON);common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
require=common.require;journal=common.journal
sha=lambda p:common.Hashes().file(p)
read=lambda p:json.loads(Path(p).read_text())
spec=importlib.util.spec_from_file_location('native_one_identity',Path(__file__).parent/'identity.py');identity=importlib.util.module_from_spec(spec);spec.loader.exec_module(identity)
NATIVE_TABLE_LOCAL=None
NATIVE_COMPOSITION=None


WINDOW_BYTES=16777216
MODE=True
DEBUGGER=None
NATIVE_TABLE=None
PACK_SHA=None
PRODUCER_SHA=None
def launch_command(serial,output):
 return [sys.executable,str(SOURCE/'scripts/dev/android-game-launch.py'),'--serial',serial,'--external-profile','--custom-vulkan-driver','--vulkan-provider','adreno','--shared-guest-heap','--no-legacy-astc-copy','--no-astc-decode-unorm8','--no-defer-pool-publication','--scale-3d','0.75','--fixed-3d','--pool-copy-accounting','--game-res','2750x1260','--timeout','90','--report',str(output/'launch.json'),'--pool-window-bytes','16777216','--expect-shared-import-windows','--native-resource-table',NATIVE_TABLE,'--native-resource-pack-sha256',PACK_SHA,'--native-resource-producer-sha256',PRODUCER_SHA]


def inventory(dev,path):
 require(dev.app('sh','-c','if test -L "$1"; then exit 5; fi; if test -d "$1"; then echo DIR; elif test -e "$1"; then exit 6; fi','sh',path) in ('','DIR'),'unknown directory kind')
 kind=dev.app('sh','-c','if test -d "$1"; then echo DIR; fi','sh',path)
 if not kind:return None
 require(not dev.app('find',path,'-type','l'),'directory contains symlink')
 files=dev.app('find',path,'-type','f').splitlines();require(all(n.startswith(path+'/') and '..'not in Path(n).parts for n in files),'unsafe directory inventory')
 if not files:return {}
 lines=[]
 # Keep each ADB shell/run-as request bounded: an existing driver cache can
 # contain hundreds of paths and exceed the shell service command limit.
 for start in range(0,len(files),128):lines.extend(dev.app('sha256sum',*files[start:start+128]).splitlines())
 result={n.removeprefix(path+'/'):h for h,n in (line.split(None,1)for line in lines)}
 require(len(result)==len(files) and all(re.fullmatch('[a-f0-9]{64}',h)for h in result.values()),'incomplete directory hashes');return result


def rename_dir(dev,src,dst,expected):
 dev.stopped();require(inventory(dev,src)==expected and inventory(dev,dst)is None,'directory rename conflict')
 # Existing private-driver atomic directory publication: old files are never
 # copied or rewritten. A stopped PID is checked again inside run-as.
 dev.app('sh','-c','set -eu; test -z "$(pidof "$3" || true)"; test -d "$1"; test ! -e "$2"; mv "$1" "$2"','sh',src,dst,PKG)
 require(inventory(dev,dst)==expected,'directory rename verification failed')


def claim_pid(d,current,own):
 if not current:return own
 if not current.isdecimal() or (own is not None and current!=own):
  d['ownership_failed']=True
  raise RuntimeError('unowned/multiple/replacement game PID')
 return current

def screenshot_collect(dev,pid,d,path,output,label):
 require(dev.adb('shell','pidof '+PKG+' || true')==pid,'screenshot requires same owned PID')
 remote=d['remote_screenshot'].removesuffix('.png')+'-'+label+'.png'
 shot={'remote':remote,'local':str(output/(label+'.png'))};d.setdefault('screenshots',[]).append(shot);journal(path,d)
 dev.adb('shell','test','!','-e',remote);shot['screencap_requested']=True;journal(path,d)
 dev.adb('shell','screencap','-p',remote);shot['device_written']=True;journal(path,d)
 screenshot_pull_remove(dev,shot,path,d)

def screenshot_pull_remove(dev,shot,path,d):
 if shot.get('temp_removed')or not shot.get('screencap_requested'):return
 remote=shot['remote']
 exists=dev.adb('shell',shlex.join(['sh','-c','if test -f "$1"; then echo FILE; fi','sh',remote]))
 if not exists:return
 require(exists=='FILE','unknown screenshot temporary kind');shot['device_written']=True
 digest=dev.adb('shell','sha256sum',remote).split()[0];require(re.fullmatch('[a-f0-9]{64}',digest),'screenshot hash absent')
 if shot.get('sha256'):require(shot['sha256']==digest,'owned screenshot changed')
 shot['sha256']=digest;journal(path,d);local=Path(shot['local'])
 dev.adb('pull',remote,str(local));require(sha(local)==digest and local.read_bytes()[:8]==b'\x89PNG\r\n\x1a\n','screenshot pull/hash/header mismatch');shot['pulled']=True;journal(path,d)
 dev.adb('shell',shlex.join(['sh','-c','test "$(sha256sum "$1" | cut -d" " -f1)" = "$2" && rm "$1"','sh',remote,digest]));shot['temp_removed']=True;journal(path,d)

def original_route(dev,pid,d,path,output,state,debugger,points=None):
 # First three points from the already accepted shared-window original5walks.
 # Host normal joystick input only; no writes to guest/world/quest state.
 route=points if points is not None else [(5215,6289),(5225,6295),(5235,6289)]
 if not(state.get('valid')and state.get('world_input_ready')and state.get('area')==1 and abs(state.get('x',0)-5225)<=24 and abs(state.get('y',0)-6289)<=24):
  d['original_route']='skipped: outside previously verified camp start';journal(path,d);return
 d['original_route_points']=route;journal(path,d);env=os.environ.copy();env['PATH']=str(R/'.local/android-sdk/platform-tools')+':'+env.get('PATH','')
 for n,(x,y)in enumerate(route):
  require(dev.adb('shell','pidof '+PKG+' || true')==pid,'route requires same owned PID')
  command=[sys.executable,str(R/'scripts/dev/android-game-walk-to.py'),str(x),str(y),'--radius','4','--timeout','15','--ready-timeout','2','--serial',d['serial']]
  with(output/('walk-'+str(n)+'.log')).open('w')as f:subprocess.run(command,env=env,check=True,stdout=f,stderr=subprocess.STDOUT,timeout=25)
  d['original_route_completed']=n+1;journal(path,d)

def wait_game(dev,path,d,output):
 command=launch_command(d['serial'],output);d['launch_command']=command;d['launch_attempted']=True;journal(path,d)
 own=None;ambiguous=False;launcher=None;debugger=None
 def pid():return dev.adb('shell','pidof '+PKG+' || true')
 def observe():
  nonlocal own,ambiguous,debugger
  current=pid();d.setdefault('PID_raw',[]).append(current);journal(path,d)
  if current:
   try:claimed=claim_pid(d,current,own)
   except RuntimeError:ambiguous=True;journal(path,d);raise
   if own is None:
    own=claimed;d['first_PID']=int(own)
    uid=dev.app('id','-u');require(uid.isdecimal() and 10000<=int(uid)<200000,'not ordinary app UID')
    status=dev.app('cat','/proc/'+own+'/status');(output/'owned-status.txt').write_text(status)
    actual=re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',status,re.M);tgid=re.search(r'^Tgid:\s+(\d+)',status,re.M)
    require(actual and tgid and tgid.group(1)==own and all(v==uid for v in actual.groups()),'ordinary PID UID proof failed');d['ordinary_UID']=int(uid);journal(path,d)
    require(DEBUGGER is not None,'missing frozen official LLDB helper');d['debugger_command']=[sys.executable,str(DEBUGGER),own,str(output)];journal(path,d)
    debugger=subprocess.Popen(d['debugger_command'],stdout=(output/'debugger-console.log').open('w'),stderr=subprocess.STDOUT,cwd=R)
  return current
 try:
  with(output/'launch.log').open('w')as f:
   launcher=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT,cwd=R);deadline=time.monotonic()+120
   while launcher.poll()is None:
    observe();require(time.monotonic()<deadline,'one launcher deadline');time.sleep(.3)
   observe();require(launcher.returncode==0,'original HUD launcher failed')
  result=read(output/'launch.json');require(own is not None and result['pid']==int(own),'launcher PID mismatch')
  (output/'game-state-before-postconditions.jsonl').write_text(dev.app('cat','files/runtime/game-state.jsonl'))
  d['original_HUD_reached']=result.get('original_hud')is True;d['game_modules_executed']=d['original_HUD_reached'];journal(path,d)
  screenshot_collect(dev,own,d,path,output,'HUD-before-postconditions')
  initial=read_json_line(dev.app('tail','-n','1','files/runtime/game-state.jsonl'))
  original_route(dev,own,d,path,output,initial,debugger)
  (output/'game-state-after-route.jsonl').write_text(dev.app('cat','files/runtime/game-state.jsonl'))
  screenshot_collect(dev,own,d,path,output,'HUD-after-route')
  require(result['requested_shared_guest_heap'] is MODE and result['actual_guest_heap']['shared'] is MODE and result['actual_vulkan_provider']=='adreno' and result['requested_legacy_astc_copy'] is False,'fixed control mode mismatch')
  require(result['actual_defer_pool_publication'] is False and result['requested_astc_decode_unorm8'] is False,'unexpected publication/decode override')
  require(debugger is not None,'no debugger attached to owned first PID');debugger.wait(timeout=90);d['debugger_exit']=debugger.returncode
  if (output/'debugger-result.json').is_file():d['debugger_result']=read(output/'debugger-result.json')
  else:d['debugger_result']={'status':'inconclusive_missing_debugger_report'}
  native=dev.app('cat','files/runtime/native.log');(output/'native.log').write_text(native)
  require('[guest]'in native and '[vulkan loader] custom='in native and '[vulkan] device=Turnip Adreno (TM) 735 'in native and 'FATAL'not in native,'native game/provider failure')
  require('[shared pool] startup probe mapping=memfd-pair-guest-RO' in native and 'backing=shared-memfd' in native and '[shared pool] immutable startup shared;' in native,'actual memfd guestRO shared required; fallback cannot pass')
  require('import_window_bytes='+str(WINDOW_BYTES)+' no online mirror fallback' in native,'actual shared window mode mismatch')
  if WINDOW_BYTES:require('cold overlapping-import bidirectional GPU byte gate: passed'in native,'missing actual overlapping-import GPU proof')
  require('[native resources] fixed table installed,' in native,'cold native resource table not installed')
  proof=identity.verify(NATIVE_TABLE_LOCAL,NATIVE_COMPOSITION,read(output/'native-binding-stops.json'),native)
  require(proof['PID']==int(own),'native recipe capture PID mismatch');(output/'native-identity-proof.json').write_text(json.dumps(proof,indent=2)+'\n');d['native_identity']=proof;journal(path,d)
  require(result.get('requested_native_resource_table')==NATIVE_TABLE and result.get('requested_native_resource_pack_sha256')==PACK_SHA and result.get('requested_native_resource_producer_sha256')==PRODUCER_SHA,'cold native launch contract mismatch')
  current=read_json_line(dev.app('tail','-n','1','files/runtime/game-state.jsonl'))
  require(current.get('guest_heap',{}).get('shared') is MODE,'actual shared heap differs');require(current.get('pool_mirrors',{}).get('backed_bytes',0)>0,'actual backing bytes missing');require(not MODE or ('[shared pool] immutable startup shared;'in native and current.get('pool_copy_accounting',{}).get('initial_shadow_bytes',-1)==0 and current.get('pool_copy_accounting',{}).get('initial_mirror_bytes',-1)==0),'shared startup/no-shadow copy evidence missing')
  require(current['valid'] and current['world_input_ready']and current['area']==1,'original camp not ready')
  require(current.get('pool_windows',{}).get('window_bytes')==WINDOW_BYTES,'published active window differs')
  (output/'camp-state.json').write_text(json.dumps(current,indent=2)+'\n');d['original_HUD_reached']=True;d['game_modules_executed']=True;journal(path,d)
  (output/'game-state.jsonl').write_text(dev.app('cat','files/runtime/game-state.jsonl'))
 finally:
  if debugger is not None and debugger.poll() is None:
   debugger.send_signal(signal.SIGINT)
   try:debugger.wait(timeout=10)
   except subprocess.TimeoutExpired:raise RuntimeError('official LLDB helper still live; do not restore')
  if launcher is not None and launcher.poll()is None:
   launcher.terminate()
   try:launcher.wait(timeout=15)
   except subprocess.TimeoutExpired:raise RuntimeError('owned launcher still active; preserve transaction')
  if (output/'debugger-result.json').is_file():
   try:d['debugger_result']=read(output/'debugger-result.json');journal(path,d)
   except Exception as e:d['debugger_report_error']=repr(e)
  current=pid()
  try:
   (output/'native.log').write_text(dev.app('cat','files/runtime/native.log'))
   (output/'crash-logcat.txt').write_text(dev.adb('logcat','-b','crash','-d','-v','threadtime'))
  except Exception as e:d['log_collection_error']=repr(e)
  for shot in d.get('screenshots',[]):
   try:screenshot_pull_remove(dev,shot,path,d)
   except Exception as e:d.setdefault('screenshot_collection_errors',[]).append(repr(e));journal(path,d)
  if current:
   require(not ambiguous and own is not None and current==own and current.isdecimal(),'unknown/replacement PID: no save/stop/restore')
   savecommand=[sys.executable,str(R/'scripts/dev/android-game-background-save.py'),'--serial',d['serial'],'--report',str(output/'fresh-save.json')]
   with(output/'fresh-save.log').open('w')as f:subprocess.run(savecommand,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
   fresh=read(output/'fresh-save.json');require(fresh['fresh_original_lifecycle_save'] is True and fresh['save_commits']==2 and fresh['pid']==int(own),'no fresh original lifecycle save')
   require(pid()==own,'PID changed while saving');dev.adb('shell','am','force-stop',PKG);require(not pid(),'owned process still alive');d['fresh_save_then_stopped']=True
  else:d['already_exited_no_save_restart']=True
  journal(path,d)

def read_json_line(text):return json.loads(text)
