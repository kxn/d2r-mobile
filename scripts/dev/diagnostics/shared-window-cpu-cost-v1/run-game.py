#!/usr/bin/env python3
"""Default-offline reversible modern Turnip shared OFF/ON original-game transaction."""
from pathlib import Path
import argparse,importlib.util,json,hashlib,subprocess,sys,time,tarfile,uuid,zipfile,re,shlex
R=Path('/home/dl/d2r-mobile'); HERE=Path(__file__).parent
spec=importlib.util.spec_from_file_location('reused_q_transaction',HERE/'reused-transaction.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
sha=m.sha; require=m.require;read=m.read;PKG=m.PKG
READY=R/'.local/logs/shared-import-windows-eddb4ca-canonical-source-ready-agent.json';RAW=R/'.local/logs/shared-import-windows-eddb4ca-raw-nso-root-ready.json'
APK=R/'.local/build/shared-import-windows-eddb4ca-canonical-source/canonical.apk';OLDAPK=R/'.local/logs/turnip-surface-wsi-appuid-v1/original.apk'
NEWAPK='056d2373244fb38730bfb0b8db14e615f36a041eee1b9e512165364f22c22dfd';NEWELF='52ddc501fa0639f4644b846e7504220c59568d65e8c24013969745dc284ceb52'
DIRS=[('driver',m.DRIVER_DIR),('temp','files/runtime/temp'),('cache','cache'),('modules','files/runtime/modules')]
def proof():
 prior=R/'.local/logs/shared-import-windows-eddb4ca-q-pair-v1/result.json';require(sha(prior)=='3066698d3df6892d9d9b3dbbf210d8274506b22ed267ab90184bfbb8440bde66','preserved original transaction changed');previous=read(prior);require(previous['cleanup_complete']and previous['original_APK_ELF_modules29_UI4_driver_cache_temp_chain_restored']and previous['cleanup_errors']==[] and previous['phases']['whole']['fresh_save_then_stopped'],'completed whole baseline not restored')
 require(sha(READY)=='e442ab9a5b3b078fbb4f84086af14c85f30dacb25a7d2ed76d2a0f087266d894','canonical report changed')
 require(sha(RAW)=='6c6ea1d6012a00ab1e3525b1a4167bf846f5dc4da7a0e453fa455773e1b239a0','raw ready changed')
 require(sha(m.BASELINE)=='310ac564e7b3ce322cf68215682e64b8d702204e7f8a2b51742074cff5f89136','baseline changed')
 require(sha(APK)==NEWAPK and sha(OLDAPK)==m.APK and sha(m.DRIVER)==m.SO,'APK/driver pins')
 r=read(RAW);b=read(m.BASELINE);ready=read(READY);require(ready['canonical_native_library_sha256']['libdmhost_android.so']==NEWELF,'host ELF declaration differs');require(ready['commit']==r['source_commit']=='eddb4ca860d3d60c2d4524b9a697cd79ed2a100a','source mismatch')
 files=r['28_deployment_file_hashes'];require(len(files)==28,'raw complete28')
 for n,h in files.items():require(sha(Path(r['bundle'])/n)==h,'raw payload changed '+n)
 with zipfile.ZipFile(APK)as z:
  for n,h in ready['canonical_native_library_sha256'].items():require(hashlib.sha256(z.read('lib/arm64-v8a/'+n)).hexdigest()==h,'APK sixlib mismatch '+n)
 ui=read(m.RESOURCE_PROOF);require(sha(m.RESOURCE_PROOF)=='76438192aedb73d5457901f4284581d1db6ff1d9db3aafbc3e80bd429d2ab1e5' and ui['changed_texture_or_cache_entries']==[] and ui['texture_and_desc_cache_CFT_entries_verified_identical']==31093,'original BC/UI4 proof')
 oldmods={Path(n).name:{'sha256':h}for n,h in b['inventories']['files/runtime/modules'].items()if not n.endswith('/.d2r-deploy-managed')}
 old={'apk':m.APK,'elf':m.ELF,'chain':m.CHAIN,'module_artifacts':oldmods}
 new={'apk':NEWAPK,'elf':NEWELF,'chain':m.CHAIN,'module_artifacts':{n:{'sha256':h}for n,h in files.items()}}
 return dict(old=old,new=new,raw=r,baseline=b,inputs={str(p):sha(p)for p in [Path(__file__),HERE/'reused-transaction.py',READY,RAW,m.BASELINE,m.RESOURCE_PROOF,APK,OLDAPK,m.DRIVER,m.COMMON,R/'scripts/dev/capture-android-scheduler.py',HERE/'approve-install.py',m.SOURCE/'scripts/dev/android-game-launch.py',R/'scripts/dev/android-game-background-save.py',HERE/'capture-official-cpu.py',R/'scripts/dev/android-game-walk-to.py']})
def install(dev,apk,expected,out):
 dev.stopped()
 with out.open('w')as f:subprocess.run(dev.command+['install','-r','-d','--no-incremental',str(apk)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=100)
 loc=dev.adb('shell','pm','path',PKG).removeprefix('package:');require(dev.adb('shell','sha256sum',loc).split()[0]==expected,'installed APK digest mismatch')
def current_owned(label,inv,d,p):
 if inv is None:return False
 if inv.get(m.OWNER)!=d['owner_sha256']:return False
 if label=='driver':return inv=={m.OWNER:d['owner_sha256'],'libvulkan_freedreno.so':m.SO} or (inv.get('libvulkan_freedreno.so')==m.SO and all(n in (m.OWNER,'libvulkan_freedreno.so') or n.startswith(('shader-cache/','mesa_shader_cache/','.cache/'))for n in inv))
 if label=='modules':return inv=={m.OWNER:d['owner_sha256'],**{n:v['sha256']for n,v in p['new']['module_artifacts'].items()}}
 return True # only new token-owned temp/cache directories; retain original counterparts atomically.
def main():
 a=argparse.ArgumentParser();a.add_argument('--execute',action='store_true');a.add_argument('--completion-trace',action='store_true',help='append existing official Perfetto8s scheduler+APP-marker window in each phase');a.add_argument('--report',type=Path,required=True);arg=a.parse_args();p=proof();m.COMPLETION_TRACE=arg.completion_trace;out=arg.report.parent.resolve();require(out.is_relative_to(R/'.local/logs'),'private outputs');out.mkdir(parents=True,exist_ok=True)
 if not arg.execute:print(json.dumps({'offline':True,'source_inputs':p['inputs'],'candidate':p['new'],'driver':m.SO,'same_resource_chain':m.CHAIN,'phases':['sharedON-whole0','sharedON-window16777216'],'tools':['official Simpleperf per-thread stat8s','official Simpleperf user99Hz DWARF8s'],'route_each_point_timeout_seconds':15,'no_new_memory_capture':True,'optional_completion_trace':arg.completion_trace,'completion_trace_helper':str(R/'scripts/dev/capture-android-scheduler.py'),'completion_wait_scope':('official Perfetto scheduler+existing APP markers in a separate8s window; not pure GPU/criticalpath' if arg.completion_trace else 'taskclock/stacks only; no offCPU-wait duration without Perfetto'),'original_BC_UI4':True},indent=2));return
 require(not arg.report.exists(),'no retry same transaction');dev=m.common.Device('10CG570J59001U1');dev.identity(p['old'])
 token=uuid.uuid4().hex;d={'token':token,'serial':'10CG570J59001U1','controller_sha':sha(__file__),'source_inputs':p['inputs'],'cleanup_errors':[],'stage':'prepared','phases':{},'owner_sha256':hashlib.sha256((token+'\n').encode()).hexdigest()}
 stage='files/runtime/.turnip-shared-'+token;d['private_stage']=stage;d['transfer']='/data/local/tmp/d2r-turnip-shared-'+token+'.tar';d['old_dirs']={}
 def write():m.journal(arg.report,d)
 def original_check():
  dev.identity(p['old']);require(m.inventory(dev,m.OVERLAY)=={n.removeprefix(m.OVERLAY+'/'):h for n,h in p['baseline']['inventories'][m.OVERLAY].items()},'original resource mismatch')
 original_check();require(m.inventory(dev,'shared_prefs')is None,'unexpected prefs; preserve');require(dev.adb('shell','settings','get','global','enable_gpu_debug_layers')in('null','0'),'GPU layers enabled')
 require(not any(re.match(r'\[debug\.mesa\.[^]]*\]: \[.+\]',x)for x in dev.adb('shell','getprop').splitlines()),'Mesa overrides active')
 for label,target in DIRS:
  inv=m.inventory(dev,target);expected={n.removeprefix(target+'/'):h for n,h in p['baseline']['inventories'][target].items()};require(inv==expected,'old directory differs '+label);d['old_dirs'][label]=inv
 dev.app('mkdir',m.LOCK);dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',m.LOCK+'/owner',token);write()
 created=False
 try:
  dev.app('sh','-c','test ! -e "$1"','sh',stage);dev.app('mkdir','-p',stage+'/incoming',stage+'/backup');created=True
  owner=out/'owner';owner.write_text(token+'\n');payload=out/'owned.tar'
  with tarfile.open(payload,'x')as tar:
   tar.add(m.DRIVER,arcname='driver/libvulkan_freedreno.so',recursive=False)
   for n in p['new']['module_artifacts']:tar.add(Path(p['raw']['bundle'])/n,arcname='modules/'+n,recursive=False)
   for label,target in DIRS:tar.add(owner,arcname=label+'/'+m.OWNER,recursive=False)
  owner.unlink();d['tar_sha256']=sha(payload);write();dev.adb('push',str(payload),d['transfer']);require(dev.digest(d['transfer'])==d['tar_sha256'],'tar transfer')
  dev.app('tar','xf',d['transfer'],'-C',stage+'/incoming')
  for label,target in DIRS:
   m.rename_dir(dev,target,stage+'/backup/'+label,d['old_dirs'][label]);d.setdefault('backed_up',[]).append(label);write()
   inv=m.inventory(dev,stage+'/incoming/'+label);require(current_owned(label,inv,d,p),'incoming mismatch');m.rename_dir(dev,stage+'/incoming/'+label,target,inv);d.setdefault('published',[]).append(label);write()
  d['install_intent']=True;write();install(dev,APK,NEWAPK,out/'install-candidate.log');d['candidate_installed']=True;write();dev.identity(p['new'])
  for window in (0,16777216):
   mode=True;m.WINDOW_BYTES=window
   name='window16'if window else'whole';phaseout=out/name;phaseout.mkdir();m.MODE=mode
   phase={**p['new'],'shared_import_window_bytes':window,'serial':d['serial'],'remote_screenshot':'/data/local/tmp/d2r-turnip-shared-'+token+'-'+name+'.png'};d['phases'][name]=phase;write()
   try:m.wait_game(dev,phaseout/'transaction.json',phase,phaseout)
   finally:write()
   require(phase.get('original_HUD_reached')and phase.get('fresh_save_then_stopped'),'game did not reach HUD/save2 '+name)
   remote=phase['remote_screenshot'];image=phaseout/'HUD.png';dev.adb('pull',remote,str(image));require(sha(image)==phase['screenshot_sha256'],'screenshot pull changed');dev.adb('shell',shlex.join(['sh','-c','test "$(sha256sum "$1" | cut -d" " -f1)" = "$2" && rm "$1"','sh',remote,phase['screenshot_sha256']]))
   # Cold private driver/cache outputs for each phase, never clear original backups.
   if not window:
    # Same APK/raw/driver/resources; only immutable import window size changes.
    for label,target in DIRS[:3]:
     inv=m.inventory(dev,target);require(current_owned(label,inv,d,p),'phase outputs changed');m.rename_dir(dev,target,stage+'/off-'+label,inv)
     dev.app('mkdir',target);dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',target+'/'+m.OWNER,token)
     if label=='driver':dev.app('cp',stage+'/off-driver/libvulkan_freedreno.so',target+'/libvulkan_freedreno.so');require(dev.digest(target+'/libvulkan_freedreno.so')==m.SO,'same driver reset')
    d['stopped_cooldown_seconds']=45;write();time.sleep(45)
  d['stage']='shared-whole-window-official-CPU-cost-pair-passed';write()
 except BaseException as e:d['error']=repr(e);write()
 finally:
  try:
   dev.stopped();require(dev.app('cat',m.LOCK+'/owner')==token,'lock owner')
   if created:
    for label,target in reversed(DIRS):
     backup=stage+'/backup/'+label;stored=m.inventory(dev,backup)
     if stored is None:require(m.inventory(dev,target)==d['old_dirs'][label],'missing backup '+label);continue
     require(stored==d['old_dirs'][label],'backup mutated '+label)
     inv=m.inventory(dev,target)
     if inv is not None:require(current_owned(label,inv,d,p),'unowned current dir '+label);m.rename_dir(dev,target,stage+'/after-'+label,inv)
     m.rename_dir(dev,backup,target,stored)
   loc=dev.adb('shell','pm','path',PKG).removeprefix('package:');apksha=dev.adb('shell','sha256sum',loc).split()[0]
   require(apksha in (NEWAPK,m.APK),'unknown APK; do not replace')
   if apksha==NEWAPK:install(dev,OLDAPK,m.APK,out/'restore-original-apk.log')
   original_check();require(all(m.inventory(dev,target)==d['old_dirs'][label]for label,target in DIRS),'original dirs not exact');require(m.inventory(dev,'shared_prefs')is None,'unexpected prefs preserve')
   d['original_APK_ELF_modules29_UI4_driver_cache_temp_chain_restored']=True;write()
   if created:
    # Remove only token-owned retired directories after original restoration and SHA CAS each leaf.
    allowed=[stage+'/incoming/'+x+'/'for x,_ in DIRS]+[stage+'/after-'+x+'/'for x,_ in DIRS]+[stage+'/off-'+x+'/'for x,_ in DIRS[:3]]
    files=dev.app('find',stage,'-type','f').splitlines();require(all(n.startswith(tuple(allowed))for n in files),'unknown staging file')
    for start in range(0,len(files),40):
     batch=files[start:start+40];args=[]
     for n in batch:args.extend([n,dev.digest(n)])
     dev.app('sh','-c','set -eu; test -z "$(pidof "$1" || true)"; shift; while test "$#" -gt 0; do test "$(sha256sum "$1" | cut -d" " -f1)" = "$2"; rm "$1"; shift 2; done','sh',PKG,*args)
    for n in sorted(dev.app('find',stage,'-type','d').splitlines(),key=lambda s:(s.count('/'),s),reverse=True):dev.app('rmdir',n)
   if dev.digest(d['transfer'])is not None:require(dev.digest(d['transfer'])==d['tar_sha256'],'transfer mutated');dev.adb('shell','rm',d['transfer'])
   dev.app('rm',m.LOCK+'/owner');dev.app('rmdir',m.LOCK);d['cleanup_complete']=True;write()
  except BaseException as e:d['cleanup_errors'].append(repr(e));write()
 print(json.dumps({'stage':d['stage'],'error':d.get('error'),'cleanup_errors':d['cleanup_errors'],'restored':d.get('original_APK_ELF_modules29_UI4_driver_cache_temp_chain_restored')},indent=2))
 require(not d.get('error')and not d['cleanup_errors']and d.get('cleanup_complete'),'actual pair incomplete; see journal')
if __name__=='__main__':main()
