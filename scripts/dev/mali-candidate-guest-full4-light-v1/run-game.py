#!/usr/bin/env python3
"""One reversible773/fullASTC4 lightweight camp memory control. Offline by default."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, shlex, signal, subprocess, sys, tarfile, time, uuid, zipfile
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport';HERE=Path(__file__).resolve().parent
SOURCE=R/'.local/worktrees/private-image-cache-guest-boundary-v1';COMMIT='773d530a1cca0390af70b36c0f916e51b78b292a'
def module(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
base=module('accepted_mali_CAS',R/'.local/worktrees/panvk-minegraph-static-hardware-game-v4/scripts/dev/run-panvk-minegraph-game.py')
installer=module('accepted_install',R/'.local/worktrees/native-candidate-short-guest-v1/scripts/dev/native-one-game-v1/installer.py')
require=base.require;sha=base.sha;read=base.read;journal=base.journal;inventory=base.inventory;rename_dir=base.rename_dir
DIRS={'modules':'files/runtime/modules','temp':'files/runtime/temp','cache':'cache','native':'files/runtime/native-one-resources'}
OWNER='.stock-mali-candidate-owner';LOCK=base.LOCK;OLDAPK=R/'.local/build/image-binding-022d625-canonical-source/canonical.apk'
PREFLIGHT=R/'.local/logs/mali-022d625-fixed060-official-dependencies-v2/preflight.json'
PREFLIGHT_SHA='eaab451ed22034404d71bfdce84efe7e690765f55c5a79d4706b9e75964c4f00'
ARTIFACT_REVIEW=R/'.local/logs/private-image-cache-guest-773d530-root-canonical-raw-review.json'
ARTIFACT_REVIEW_SHA='64bce3ebf3e6cb19c78b5a1637bf9cf1a2f27abb4e6e473e232983f70d0b4b0e'
PINS={'canonical':(R/'.local/logs/private-image-cache-guest-773d530-canonical-source-ready-agent.json','ed3e069613ade493f1143382f4f7cb8a55c511efb9ce01cf76b5ab6b5224bb48'),'raw':(R/'.local/logs/private-image-cache-guest-773d530-raw-nso-ready-agent.json','4d230260434e528d1b29f1545af5bc5cc261bbe761e21a3f66591a6827677130')}
def record(p):p=Path(p);return {'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size}
def checked(r):require(sha(r['path'])==r['sha256'],'pinned file changed: '+r['path']);return read(r['path'])
def proof(config,execute=False):
 require(config['runtime_commit']==COMMIT and config['scope']=='stock-Mali773-fullASTC4-lightweight-camp-memory-control','wrong candidate scope')
 require(config['scale_initial']==.75 and config['dynamic_3d']is True and config['provider']=='system'and config['shared']is False and config['legacy_ASTC_copy']is False,'wrong runtime mode')
 require(re.fullmatch(r'10\.10\.10\.44:[0-9]+',config['serial']),'not Mali lease address')
 for p,h in PINS.values():require(sha(p)==h,'Ready changed')
 require(sha(ARTIFACT_REVIEW)==ARTIFACT_REVIEW_SHA and read(ARTIFACT_REVIEW)['runtime']==COMMIT,'root canonical/raw review changed')
 c=read(PINS['canonical'][0]);b=read(PINS['raw'][0]);require(c['commit']==b['source_commit']==COMMIT,'source identity')
 for field in ('canonical_producer_inputs','actual_NDK_dependencies'):
  for row in checked(c[field]):require(sha(row['archive'])==row['sha256']and sha(row['source'])==row['sha256'],'canonical source/dependency changed')
 checked(c['canonical_build_manifest']);apk=c['canonical_APK']['archive'];require(sha(apk['path'])==apk['sha256'],'APK changed')
 with zipfile.ZipFile(apk['path'])as z:
  got={Path(n).name:hashlib.sha256(z.read(n)).hexdigest()for n in z.namelist()if n.startswith('lib/arm64-v8a/')and n.endswith('.so')}
 require(got==c['canonical_native_library_sha256'],'six runtime libraries differ')
 for row in b['raw_original_inputs'].values():require(sha(row['path'])==row['sha256'],'original NSO changed')
 for name,h in b['28_deployment_file_hashes'].items():require(Path(name).name==name and sha(Path(b['bundle'])/name)==h,'raw deployment file changed')
 subprocess.run(b['separate_verify_only_argv'],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 require(sha(PREFLIGHT)==PREFLIGHT_SHA,'original stock resource baseline changed');old=read(PREFLIGHT);require(len(old['original_modules_resources_private_driver_hashes'])==35 and sha(OLDAPK)==old['original_APK']==base.APK,'old022 baseline identity')
 if execute:
  require(config.get('execution_ready')is True,'DRAFT cannot execute')
  q=checked(config['773_Q_actual_root_review']);require(q.get('runtime')==COMMIT or q.get('runtime_commit')==COMMIT,'actual qualification not773')
  require(config['773_Q_actual_root_review']['sha256']=='edd74c5c36e803a176d95cfc5eae3169ffdc96f8bec818e2c3d1db344669560b','actual qualification hash differs')
  for r in config['tool_files']:require(sha(r['path'])==r['sha256'],'tool changed')
 return {'APK':apk,'modules':b['28_deployment_file_hashes'],'bundle':b['bundle'],'baseline35':old['original_modules_resources_private_driver_hashes']}
def command(serial,out):
 # No scale/fixed Intent: nativeBoot default .75, adaptive scaling remains ON.
 return [sys.executable,str(SOURCE/'scripts/dev/android-game-launch.py'),'--serial',serial,'--external-profile','--vulkan-provider','system','--no-shared-guest-heap','--no-legacy-astc-copy','--pool-window-bytes','16777216','--pool-copy-accounting','--game-res','2772x1240','--timeout','90','--report',str(out/'launch.json')]
def baseline(dev,p):
 dev.stopped();path=dev.adb('shell','pm','path',PKG).removeprefix('package:');require(path.startswith('/')and'\n'not in path,'one original APK required');require(dev.adb('shell','sha256sum',path).split()[0]==base.APK,'installed original APK differs')
 lines=dev.app('sha256sum',*p['baseline35']);got={n:h for h,n in(x.split(None,1)for x in lines.splitlines())};require(got==p['baseline35'],'original35 differs')
 require(dev.app('cat','files/runtime/shaders/.chain')==base.CHAIN,'old shader chain differs')
 return {'APK':base.APK,'baseline35':got,'shaders':inventory(dev,'files/runtime/shaders'),'drivers':{name:inventory(dev,'files/runtime/'+name)for name in ['vulkan-panfrost','vulkan-adreno']},'layers':base.layers(dev)}
def owned(inv,d,label,p):
 require(isinstance(inv,dict)and inv.get(OWNER)==d['owner_sha256'],'directory owner differs: '+label)
 if label=='modules':require(inv=={OWNER:d['owner_sha256'],**p['modules']},'candidate modules differ')
 if label=='native':require(inv=={OWNER:d['owner_sha256']},'unexpected native table')
 return True
def snapshot(dev,pid,path):
 require(dev.adb('shell','pidof '+PKG+' || true')==str(pid),'first PID changed')
 d=json.loads(dev.app('tail','-n','1','files/runtime/game-state.jsonl'));require(d['valid']and d['world_input_ready']and .5<=d['scale_3d']<=.75 and not d['guest_heap']['shared']and d['game_output']==d['surface_output']=={'width':2772,'height':1240}and not d['world_freeze']['host_frozen']and not d['runtime_extrap']['enabled'],'stock scene/mode changed');path.write_text(json.dumps(d,indent=2)+'\n');return d
def showmap(dev,pid,out):
 # One standard tool invocation after the same original-input camp route; no CPU/GPU windows; never a loop.
 result={'PID':pid,'tool':'/system/bin/showmap','attempted':False};helpq=subprocess.run(dev.command+['shell','/system/bin/showmap','--help'],capture_output=True,text=True,timeout=15)
 (out/'showmap-help.txt').write_text(helpq.stdout+helpq.stderr);result['help_returncode']=helpq.returncode
 if not all(k in helpq.stdout+helpq.stderr for k in ('-a','-v','-o','json')):result['status']='actualunsupported';(out/'showmap-result.json').write_text(json.dumps(result,indent=2)+'\n');return
 result['tool_sha256']=dev.adb('shell','sha256sum','/system/bin/showmap').split()[0];snapshot(dev,pid,out/'before-showmap.json');result['attempted']=True
 cmd=dev.command+['shell',shlex.join(['run-as',PKG,'/system/bin/showmap','-a','-v','-o','json',str(pid)])];q=subprocess.run(cmd,capture_output=True,text=True,timeout=20);(out/'official-showmap.json').write_text(q.stdout);(out/'official-showmap.stderr.txt').write_text(q.stderr);valid_json=False
 if q.returncode==0:
  try:json.loads(q.stdout);valid_json=True
  except ValueError:pass
 result.update(command=cmd,returncode=q.returncode,valid_JSON=valid_json,status='actual-complete'if q.returncode==0 and valid_json else'actualpermission-or-tool-failure');(out/'showmap-result.json').write_text(json.dumps(result,indent=2)+'\n')
def launch(dev,d,p,out):
 own=None;client=None
 def same():require(dev.adb('shell','pidof '+PKG+' || true')==str(own),'first PID changed')
 def invoke(label,cmd,timeout):
  with(out/(label+'.log')).open('x')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=timeout)
 try:
  with(out/'launch.log').open('x')as f:
   client=subprocess.Popen(command(d['serial'],out),stdout=f,stderr=subprocess.STDOUT);deadline=time.monotonic()+120
   while client.poll()is None:
    now=dev.adb('shell','pidof '+PKG+' || true')
    if now:
     require(now.isdecimal()and(own is None or int(now)==own),'launch PID replaced')
     if own is None:own=int(now);d['first_PID']=own;d['ordinary_UID']=int(dev.app('id','-u'));require(10000<=d['ordinary_UID']<200000,'ordinary UID required');journal(out/'result.json',d)
    require(time.monotonic()<deadline,'launch timeout');time.sleep(.4)
   require(client.returncode==0,'original launcher failed')
  same();state=snapshot(dev,own,out/'before-route.json');native=dev.app('cat','files/runtime/native.log');require('[native resources] fixed table installed'not in native and 'FATAL'not in native,'wrong native/fatal path');require(read(out/'launch.json')['actual_vulkan_provider']=='system','not system Vulkan')
  status=dev.app('cat','/proc/'+str(own)+'/status');u=re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',status,re.M);require(u and all(int(x)==d['ordinary_UID']for x in u.groups()),'PID UID differs');(out/'first-status.txt').write_text(status)
  maps=dev.app('cat','/proc/'+str(own)+'/maps');(out/'maps.txt').write_text(maps);require(not any(x in maps.lower()for x in ['renderdoc','libvklayer','liblwi','libvulkan_panfrost','libvulkan_freedreno']),'unexpected driver/layer')
  d['route_completed']=0
  if state['area']==1 and abs(state['x']-5225)<=24 and abs(state['y']-6289)<=24:
   for i,(x,y)in enumerate([(5225,6295),(5235,6289)]):
    same();invoke('walk-'+str(i),[sys.executable,str(SOURCE/'scripts/dev/android-game-walk-to.py'),str(x),str(y),'--radius','4','--timeout','5','--ready-timeout','2','--serial',d['serial']],25);d['route_completed']+=1
  else:d['route_skipped']='Outside already verified camp bounds; actual stationary scene only'
  snapshot(dev,own,out/'after-route.json')
  same();(out/'official-dumpsys-meminfo.txt').write_text(dev.adb('shell','dumpsys','meminfo',str(own)));showmap(dev,own,out);snapshot(dev,own,out/'after-tools.json')
  q=subprocess.run(dev.command+['exec-out','screencap','-p'],capture_output=True,check=True,timeout=15);require(q.stdout.startswith(b'\x89PNG\r\n\x1a\n'),'screenshot notPNG');(out/'HUD.png').write_bytes(q.stdout)
  d['actual_HUD_and_lightweight_memory_control_complete']=True;d['visual_review_pending']=True
 finally:
  if client and client.poll()is None:client.terminate();client.wait(timeout=10)
  for remote,local in [('files/runtime/native.log','native.log'),('files/runtime/game-state.jsonl','game-state.jsonl')]:
   try:(out/local).write_text(dev.app('sh','-c','if test -f "$1"; then cat "$1"; fi','sh',remote))
   except Exception as e:d.setdefault('log_errors',[]).append(repr(e))
  try:(out/'crash-logcat.txt').write_text(dev.adb('logcat','-b','crash','-d','-v','threadtime','-t','200'))
  except Exception as e:d.setdefault('log_errors',[]).append(repr(e))
  now=dev.adb('shell','pidof '+PKG+' || true')
  if now:
   require(own is not None and now==str(own),'unowned PID: preserve transaction')
   invoke('fresh-save',[sys.executable,str(R/'scripts/dev/android-game-background-save.py'),'--serial',d['serial'],'--report',str(out/'fresh-save.json')],45);s=read(out/'fresh-save.json');require(s['pid']==own and s['fresh_original_lifecycle_save']and s['save_commits']==2,'fresh original save2 required');same();dev.adb('shell','am','force-stop',PKG);dev.stopped();d['fresh_save2_then_stopped']=True
  else:d['already_exited_no_save_restart']=True
def restore(dev,d,p,out):
 dev.stopped();require(not installer.UNRESOLVED,'unresolved installer: preserve backups')
 for label,target in reversed(list(DIRS.items())):
  before=d['old_dirs'][label];backup=d['stage']+'/backup/'+label;stored=inventory(dev,backup);current=inventory(dev,target)
  if stored is None and before is not None:require(current==before,'original backup missing/target changed');continue
  if stored is not None:require(stored==before,'original backup changed')
  if current is not None:owned(current,d,label,p);rename_dir(dev,target,d['stage']+'/after-'+label,current)
  if before is not None:rename_dir(dev,backup,target,before)
  require(inventory(dev,target)==before,'restore differs '+label)
 current=dev.adb('shell','pm','path',PKG).removeprefix('package:');digest=dev.adb('shell','sha256sum',current).split()[0]
 if digest!=base.APK:
  require(digest==p['APK']['sha256'],'unknown installed APK: preserve backups')
  installer.install(dev,OLDAPK,base.APK,out/'restore-original.log',require,PKG)
 require(baseline(dev,p)==d['baseline_before'],'original APK/35/shaders/drivers/layers differs');d['original_restored']=True
 # Original CAS cleanup: only known owner/hashed leaves, no recursive rm.
 stage=d['stage'];require(dev.app('cat',stage+'/'+OWNER)==d['token'],'stage ownership changed')
 require(not dev.app('find',stage,'-type','l'),'stage symlink')
 for label in DIRS:
  for prefix in ('incoming/','after-'):
   inv=inventory(dev,stage+'/'+prefix+label)
   if inv is not None:owned(inv,d,label,p)
 files=dev.app('find',stage,'-type','f').splitlines();allowed=tuple(stage+'/'+prefix+label+'/'for label in DIRS for prefix in ('incoming/','after-'))
 require(all(n==stage+'/'+OWNER or n.startswith(allowed)for n in files),'unknown staging leaf')
 for n in files:
  h=dev.digest(n);dev.app('sh','-c','set -eu; test -z "$(pidof "$3" || true)"; test "$(sha256sum "$1" | cut -d" " -f1)" = "$2"; rm "$1"','sh',n,h,PKG)
 for n in sorted(dev.app('find',stage,'-type','d').splitlines(),key=lambda n:(n.count('/'),n),reverse=True):dev.app('rmdir',n)
 if d.get('transfer_pushed'):
  require(dev.adb('shell','sha256sum',d['transfer']).split()[0]==d['transfer_sha256'],'transfer differs');dev.adb('shell','rm',d['transfer'])
 require(dev.app('cat',LOCK+'/owner')==d['token'],'lock owner changed');dev.app('rm',LOCK+'/owner');dev.app('rmdir',LOCK);d['cleanup_complete']=True

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--inputs',type=Path,required=True);ap.add_argument('--inputs-sha256',required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--execute',action='store_true');a=ap.parse_args()
 require(sha(a.inputs)==a.inputs_sha256,'inputs changed');c=read(a.inputs);p=proof(c,a.execute)
 plan={'scope':c['scope'],'runtime_commit':COMMIT,'candidate_APK':p['APK'],'serial':c['serial'],'launch':command(c['serial'],a.output),'directories_CAS':DIRS,'original_overlay_and_shaders_and_system_driver_retained':True,'original_progress_saved':True,'mode':'initial .75 adaptive (no fixed flag), systemVulkan sharedOFF legacyASTCOFF; not .60 matched perfAB','showmap':'one existing standard command after the same2walk route, no CPU/GPU windows, help+returncode persisted; unsupported/permission errors not success','default_offline':not a.execute}
 if not a.execute:print(json.dumps(plan,indent=2));return
 os.environ['PATH']=str(R/'.local/android-sdk/platform-tools')+':'+os.environ['PATH']
 a.output.mkdir(parents=True,exist_ok=False);out=a.output;token=uuid.uuid4().hex;dev=base.old.common.Device(c['serial']);d={**plan,'token':token,'owner_sha256':hashlib.sha256((token+'\n').encode()).hexdigest(),'stage':'files/runtime/.stock-mali-773-full4-light-'+token,'transfer':'/data/local/tmp/stock-mali-773-full4-light-'+token+'.tar','cleanup_errors':[],'phase':'preflight'};save=lambda:journal(out/'result.json',d);save();claimed=False
 def interrupt(sig,frame):
  signal.signal(signal.SIGTERM,signal.SIG_IGN);signal.signal(signal.SIGINT,signal.SIG_IGN);raise RuntimeError('cooperative owner cancellation: save2/stop/restore only')
 signal.signal(signal.SIGTERM,interrupt);signal.signal(signal.SIGINT,interrupt)
 try:
  d['baseline_before']=baseline(dev,p);require(base.no_layers(d['baseline_before']['layers']),'external GPU layer active');d['old_dirs']={label:inventory(dev,path)for label,path in DIRS.items()};dev.stopped();save()
  require(inventory(dev,d['stage'])is None,'stage already exists');dev.app('mkdir',LOCK);d['lock_claimed']=True;dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',LOCK+'/owner',token);dev.app('mkdir',d['stage']);claimed=True;dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',d['stage']+'/'+OWNER,token);dev.app('mkdir',d['stage']+'/incoming',d['stage']+'/backup');save()
  owner=out/'owner';owner.write_text(token+'\n');payload=out/'candidate-modules.tar'
  with tarfile.open(payload,'x')as tar:
   for n in p['modules']:tar.add(Path(p['bundle'])/n,arcname='modules/'+n,recursive=False)
   for label in DIRS:tar.add(owner,arcname=label+'/'+OWNER,recursive=False)
  d['transfer_sha256']=sha(payload);dev.adb('push',str(payload),d['transfer']);d['transfer_pushed']=True;save();require(dev.adb('shell','sha256sum',d['transfer']).split()[0]==d['transfer_sha256'],'pushed payload differs');dev.app('tar','xf',d['transfer'],'-C',d['stage']+'/incoming')
  for label,target in DIRS.items():
   if d['old_dirs'][label]is not None:rename_dir(dev,target,d['stage']+'/backup/'+label,d['old_dirs'][label])
   candidate=inventory(dev,d['stage']+'/incoming/'+label);owned(candidate,d,label,p);rename_dir(dev,d['stage']+'/incoming/'+label,target,candidate)
  d['phase']='published';save();installer.install(dev,Path(p['APK']['path']),p['APK']['sha256'],out/'install-candidate.log',require,PKG)
  launch(dev,d,p,out);d['phase']='actual-stock-Mali773-fullASTC4-lightweight-memory-control-complete-not-thermal-or-6GB';save()
 except BaseException as e:d['phase']='failed-preserved';d['error']=repr(e)
 finally:
  if claimed:
   try:restore(dev,d,p,out)
   except BaseException as e:d['cleanup_errors'].append(repr(e))
  elif d.get('lock_claimed'):
   try:
    dev.stopped();require(dev.app('cat',LOCK+'/owner')==token,'partial lock owner changed');dev.app('rm',LOCK+'/owner');dev.app('rmdir',LOCK)
   except BaseException as e:d['cleanup_errors'].append(repr(e))
  save()
 print(json.dumps({k:v for k,v in d.items()if k not in ('baseline_before','old_dirs')},indent=2));raise SystemExit(0 if not d.get('error')and not d['cleanup_errors']else 1)
if __name__=='__main__':main()
