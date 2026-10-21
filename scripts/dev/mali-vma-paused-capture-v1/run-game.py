#!/usr/bin/env python3
"""One reversible stock-Mali VMA builtin dump at original suspend. Offline by default."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, shlex, signal, subprocess, sys, tarfile, time, uuid, zipfile
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport';HERE=Path(__file__).resolve().parent
SOURCE=R/'.local/worktrees/vma-one-shot-dump-noembed-v2';COMMIT='7225d7f7ca6d435d59a3cfb102eabbc59a619592'
def module(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
base=module('accepted_mali_CAS',R/'.local/worktrees/panvk-minegraph-static-hardware-game-v4/scripts/dev/run-panvk-minegraph-game.py')
installer=module('accepted_install',R/'.local/worktrees/native-candidate-short-guest-v1/scripts/dev/native-one-game-v1/installer.py')
require=base.require;sha=base.sha;read=base.read;journal=base.journal;inventory=base.inventory;rename_dir=base.rename_dir
DIRS={'modules':'files/runtime/modules','temp':'files/runtime/temp','cache':'cache','native':'files/runtime/native-one-resources'}
OWNER='.stock-mali-candidate-owner';LOCK=base.LOCK;OLDAPK=R/'.local/build/image-binding-022d625-canonical-source/canonical.apk'
PREFLIGHT=R/'.local/logs/mali-022d625-fixed060-official-dependencies-v2/preflight.json'
PREFLIGHT_SHA='eaab451ed22034404d71bfdce84efe7e690765f55c5a79d4706b9e75964c4f00'
SCOPE='stock-Mali-VMA-builtin-paused-fullASTC4'
def record(p):p=Path(p);return {'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size}
def checked(r):require(sha(r['path'])==r['sha256'],'pinned file changed: '+r['path']);return read(r['path'])
def proof(config,execute=False):
 require(config['runtime_commit']==COMMIT and config['scope']==SCOPE,'wrong candidate scope')
 require(config['scale_initial']==.75 and config['dynamic_3d']is True and config['provider']=='system'and config['shared']is False and config['legacy_ASTC_copy']is False,'wrong runtime mode')
 require(re.fullmatch(r'10\.10\.10\.44:[0-9]+',config['serial']),'not Mali lease address')
 require(config.get('artifact_records_ready')is True,'DRAFT: root-reviewed canonical/raw inputs required')
 c=checked(config['canonical_ready']);b=checked(config['raw_ready']);review=checked(config['root_artifact_review'])
 require(review['runtime']==COMMIT,'root canonical/raw review changed')
 require(c['commit']==b['source_commit']==COMMIT,'source identity')
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
  checked(config['root_tool_review'])
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
 # One existing standard tool invocation while the same original process stays paused; no sampling loop.
 result={'PID':pid,'tool':'/system/bin/showmap','attempted':False};helpq=subprocess.run(dev.command+['shell','/system/bin/showmap','--help'],capture_output=True,text=True,timeout=15)
 (out/'showmap-help.txt').write_text(helpq.stdout+helpq.stderr);result['help_returncode']=helpq.returncode
 if not all(k in helpq.stdout+helpq.stderr for k in ('-a','-v','-o','json')):result['status']='actualunsupported';(out/'showmap-result.json').write_text(json.dumps(result,indent=2)+'\n');return
 result['tool_sha256']=dev.adb('shell','sha256sum','/system/bin/showmap').split()[0];require(dev.adb('shell','pidof '+PKG+' || true')==str(pid),'PID changed before paused showmap');result['attempted']=True
 cmd=dev.command+['shell',shlex.join(['run-as',PKG,'/system/bin/showmap','-a','-v','-o','json',str(pid)])];q=subprocess.run(cmd,capture_output=True,text=True,timeout=20);(out/'official-showmap.json').write_text(q.stdout);(out/'official-showmap.stderr.txt').write_text(q.stderr);valid_json=False
 if q.returncode==0:
  try:json.loads(q.stdout);valid_json=True
  except ValueError:pass
 result.update(command=cmd,returncode=q.returncode,valid_JSON=valid_json,status='actual-complete'if q.returncode==0 and valid_json else'actualpermission-or-tool-failure');(out/'showmap-result.json').write_text(json.dumps(result,indent=2)+'\n')
def original_save(dev,pid,d,out,invoke):
 require(not d.get('lifecycle_save_attempted'),'do not resume/retry a failed or completed lifecycle save')
 d['lifecycle_save_attempted']=True
 invoke('fresh-save',[sys.executable,str(R/'scripts/dev/android-game-background-save.py'),'--serial',d['serial'],'--report',str(out/'fresh-save.json')],45)
 s=read(out/'fresh-save.json');require(s['pid']==pid and s['fresh_original_lifecycle_save']and s['save_commits']==2,'fresh original save2 required')
 require(dev.adb('shell','pidof '+PKG+' || true')==str(pid),'PID changed during save')
 d['fresh_save2_confirmed']=True
 return s
def vma_pause_capture(dev,pid,d,out,invoke):
 remote='files/runtime/temp/vulkan-vma.json';part=remote+'.part'
 d['actual_paused_VMA_capture_complete']=False
 d['VMA_capture_status']='incomplete until original completion, suspend, same-PID official outputs and exact JSON transfer are verified'
 same=lambda:require(dev.adb('shell','pidof '+PKG+' || true')==str(pid),'PID changed during VMA pause capture')
 same();require(dev.digest(remote)is None and dev.digest(part)is None,'new owned temp unexpectedly contains a previous VMA output')
 before=dev.app('cat','files/runtime/native.log')
 require('Android native start pid='+str(pid)+'\n'in before,'native log PID differs before explicit request')
 offset=len(before);(out/'before-vma-request-native.log').write_text(before)
 # Activity is already foreground. Only the one-shot extra is passed; no startup options, new job or GPU wait.
 reply=dev.adb('shell','am','start','-W','-n',PKG+'/.MainActivity','--ez','vma_dump_on_suspend','true')
 (out/'vma-request-am-start.txt').write_text(reply);same();d['vma_request_sent_once']=True
 saved=original_save(dev,pid,d,out,invoke)
 confirmation=saved['confirmation']
 require(re.search(r'Android background save asked=1 commits=2(?:\s|$)',confirmation),'original fresh save2 confirmation malformed')
 # Observe existing lifecycle output only. No device timer/sampler or additional GPU synchronization.
 deadline=time.monotonic()+20
 while True:
  same();native=dev.app('cat','files/runtime/native.log')
  require(native.startswith(before),'native log reset or changed prefix during suspend')
  tail=native[offset:];requests=list(re.finditer(r'\[vulkan VMA dump\] explicit request accepted=([01]); consumes at next normal suspend',tail))
  require(len(requests)<=1,'multiple explicit VMA requests in one owned run')
  if requests:require(requests[0].group(1)=='1','explicit VMA request rejected')
  failed=re.search(r'\[vulkan VMA dump\] failed:[^\n]*',tail)
  completion=re.search(r'\[vulkan VMA dump\] complete=([01]) errno=([0-9]+) file=([^\n]+?) builtin_detailed_map=1 post_drain_before_this_job_trim=1 earlier_tail_FRAME_trim_not_excluded=1;',tail)
  suspended=tail.find('[nvn] 图形挂起：在途 GPU 工作已清空',completion.end())if completion else -1
  (out/'vma-pause-native-tail.log').write_text(tail)
  if failed:raise RuntimeError('original VMA dump incomplete: '+failed.group(0))
  if completion:
   require(requests and requests[0].start()<completion.start(),'VMA completion precedes this explicit request')
   save_at=tail.rfind(confirmation)
   require(requests[0].end()<=save_at<completion.start(),'VMA completion does not follow this exact original save2 confirmation')
   require(completion.group(1)=='1'and completion.group(2)=='0','original VMA output failed: '+completion.group(0))
   require(completion.group(3).endswith('/files/runtime/temp/vulkan-vma.json'),'VMA fixed private path differs')
   if suspended>completion.end():break
  require(time.monotonic()<deadline,'incomplete VMA dump/original graphics suspend; no retry')
  time.sleep(.2)
 d['original_graphics_suspend_confirmed']=True
 d['memory_stage']='Same normal pause: VMA post-drain/pre-this-IDLE_TRIM; meminfo/showmap after original graphics-suspend marker and this trim; not simultaneous, not foreground peak'
 same();(out/'official-paused-dumpsys-meminfo.txt').write_text(dev.adb('shell','dumpsys','meminfo',str(pid)));same()
 showmap(dev,pid,out);same()
 # Preserve exact original VMA bytes, including trailing newline. No wrapping, filtering or statistic reconstruction.
 digest=dev.digest(remote);require(digest is not None,'VMA completion did not leave its fixed output')
 cmd=dev.command+['exec-out','run-as',PKG,'cat',remote]
 q=subprocess.run(cmd,capture_output=True,check=True,timeout=20);same()
 require(hashlib.sha256(q.stdout).hexdigest()==digest==dev.digest(remote),'VMA output changed during transfer')
 require(isinstance(json.loads(q.stdout),dict),'original VMA output is not a JSON object')
 target=out/'vulkan-vma.json'
 with target.open('xb')as f:f.write(q.stdout)
 d['VMA_original_JSON']={**record(target),'remote':remote,'remote_sha256':digest,'pull_command':cmd,'content':'unmodified builtin detailed VMA JSON; Usage/Budget estimates; no recomputed statistics'}
 d['actual_paused_VMA_capture_complete']=True
 d['VMA_capture_status']='complete original JSON; paused stage only, not foreground/thermal/6GB evidence'
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
  else:raise RuntimeError('outside verified camp bounds: no route/VMA success claimed')
  after=snapshot(dev,own,out/'after-route.json');require(d['route_completed']==2 and after['area']==1,'both original camp walks required')
  q=subprocess.run(dev.command+['exec-out','screencap','-p'],capture_output=True,check=True,timeout=15);require(q.stdout.startswith(b'\x89PNG\r\n\x1a\n'),'screenshot notPNG');(out/'HUD.png').write_bytes(q.stdout)
  d['actual_original_game_HUD_reached']=True;d['visual_review_pending']=True
  vma_pause_capture(dev,own,d,out,invoke)
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
   if not d.get('lifecycle_save_attempted'):original_save(dev,own,d,out,invoke)
   require(d.get('fresh_save2_confirmed'),'failed lifecycle save: preserve running owner and transaction')
   same();dev.adb('shell','am','force-stop',PKG);dev.stopped();d['fresh_save2_then_stopped']=True
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
 plan={'scope':c['scope'],'runtime_commit':COMMIT,'candidate_APK':p['APK'],'serial':c['serial'],'launch':command(c['serial'],a.output),'directories_CAS':DIRS,'original_overlay_and_shaders_and_system_driver_retained':True,'original_progress_saved':True,'mode':'initial .75 adaptive (no fixed flag), systemVulkan sharedOFF legacyASTCOFF; not .60 matched perfAB','showmap':'one standard command after original save2 and original graphics-suspend marker; paused post-trim, VMA is earlier pre-this-job-trim; cannot subtract or claim same instant','default_offline':not a.execute}
 if not a.execute:print(json.dumps(plan,indent=2));return
 os.environ['PATH']=str(R/'.local/android-sdk/platform-tools')+':'+os.environ['PATH']
 a.output.mkdir(parents=True,exist_ok=False);out=a.output;token=uuid.uuid4().hex;dev=base.old.common.Device(c['serial']);d={**plan,'token':token,'owner_sha256':hashlib.sha256((token+'\n').encode()).hexdigest(),'stage':'files/runtime/.stock-mali-vma-paused-'+token,'transfer':'/data/local/tmp/stock-mali-vma-paused-'+token+'.tar','cleanup_errors':[],'phase':'preflight'};save=lambda:journal(out/'result.json',d);save();claimed=False
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
  launch(dev,d,p,out);d['phase']='actual-stock-Mali-VMA-paused-memory-complete-not-thermal-or-6GB';save()
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
