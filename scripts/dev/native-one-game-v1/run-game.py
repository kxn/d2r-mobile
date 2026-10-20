#!/usr/bin/env python3
"""Offline by default. One reversible original-game Q735 native-material test.
No profiling, timing, CPU pair, game patch, driver-system update or raw RomFS copy.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,shlex,subprocess,sys,tarfile,uuid,zipfile
R=Path('/home/dl/d2r-mobile');HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('native_one_transaction',HERE/'transaction.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
sha=m.sha;read=m.read;require=m.require;PKG=m.PKG
spec=importlib.util.spec_from_file_location('native_one_installer',HERE/'installer.py');installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
COMMIT='bb4ac548cbb3858ed6cee01d3319b26e50088f5e'
READY=R/'.local/logs/native-image-window-lane-bb4ac54-canonical-source-ready-agent.json';READY_SHA='277ba1d3c22df08d8d92bfda70675b95c0cccde0a2af73b7ac6f7f2abc1aac5a'
RAW=R/'.local/logs/native-image-window-bb4ac54-raw-nso-root-ready.json';RAW_SHA='f830e6e33e80dc91c04825a1c97d1524563ede7128d288392624fae37ecdf5d7'
OVERLAY_READY=R/'.local/logs/turnip-one-native-overlay-ba0b943-source-ready.json';OVERLAY_READY_SHA='42dbad2a6a69a838f2844314300d1157654d078e786ffa651af6e6ce14bdf2a9'
NATIVE=R/'.local/build/turnip-one-native-overlay-final-v2';UI=NATIVE/'romfs_overlay';TABLE=NATIVE/'files/native-layouts.bin'
APK=R/'.local/build/native-image-window-lane-bb4ac54-canonical-source/canonical.apk';OLDAPK=R/'.local/logs/turnip-surface-wsi-appuid-v1/original.apk'
NEWAPK='ab2a3e4e6e7a41f3f38a74ffc97c313fcff0d93c3c8128737be4f4fca765d11c';NEWELF='dc4442c34f873745c2ea376b796a2c1678f7a58d9ef8ffaf01104be0784abde0'
m.DRIVER=R/'.local/build/turnip-host-image-v1/source-ready-freeze/driver/libvulkan_freedreno.so';m.SO='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'
TABLE_DIR='files/runtime/native-one-resources';ABS_TABLE='/data/user/0/'+PKG+'/'+TABLE_DIR+'/native-layouts.bin'
DIRS=[('driver',m.DRIVER_DIR),('temp','files/runtime/temp'),('cache','cache'),('modules','files/runtime/modules'),('overlay',m.OVERLAY),('native',TABLE_DIR)]
SOURCE_TOOLS=R/'.local/build/shared-import-windows-eddb4ca-cpu-cost-v1-tools'
EXPECTED_FIVE={'ctrl_touch','touch_strings.tsv','data/.build.config','data/00-00000007.data','data/00-00000008.data'}
def proof():
 pins={READY:READY_SHA,RAW:RAW_SHA,OVERLAY_READY:OVERLAY_READY_SHA,m.BASELINE:'310ac564e7b3ce322cf68215682e64b8d702204e7f8a2b51742074cff5f89136',m.RESOURCE_PROOF:'76438192aedb73d5457901f4284581d1db6ff1d9db3aafbc3e80bd429d2ab1e5',APK:NEWAPK,OLDAPK:m.APK,m.DRIVER:m.SO,R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1/result.json':'5b1a31881829ea34796e51601a2f29e29e191960521df60006177f6435943953'}
 for p,h in pins.items():require(sha(p)==h,'frozen input changed '+str(p))
 prior=read(R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1/result.json');require(prior['cleanup_complete']and prior['original_APK_ELF_modules29_UI4_driver_cache_temp_chain_restored']and not prior['cleanup_errors'],'reference transaction incomplete')
 ready=read(READY);raw=read(RAW);base=read(m.BASELINE);overlay=read(OVERLAY_READY);composition=read(NATIVE/'composition.json')
 require(ready['commit']==raw['source_commit']==COMMIT,'source revision mismatch');require(ready['canonical_native_library_sha256']['libdmhost_android.so']==NEWELF,'ELF declaration mismatch')
 require(sha(NATIVE/'composition.json')==overlay['composition_sha256'],'composition changed')
 delta=overlay['deploy_overlay_files_only'];require(set(delta)==EXPECTED_FIVE,'only exact five overlay delta allowed')
 for n,h in delta.items():require((UI/n).is_file()and sha(UI/n)==h,'native overlay delta changed '+n)
 require(sha(TABLE)==composition['native_table_sha256'] and composition['driver_sha256']==m.SO and composition['native_payload_bytes']==135168 and composition['unchanged_cache_entries']==31071,'native table/driver/one material proof')
 files=raw['28_deployment_file_hashes'];require(len(files)==28,'raw complete28')
 for n,h in files.items():require(sha(Path(raw['bundle'])/n)==h,'raw payload changed '+n)
 with zipfile.ZipFile(APK)as z:
  for n,h in ready['canonical_native_library_sha256'].items():require(hashlib.sha256(z.read('lib/arm64-v8a/'+n)).hexdigest()==h,'sixlib mismatch '+n)
 oldmods={Path(n).name:{'sha256':h}for n,h in base['inventories']['files/runtime/modules'].items()if not n.endswith('/.d2r-deploy-managed')}
 old={'apk':m.APK,'elf':m.ELF,'chain':m.CHAIN,'module_artifacts':oldmods};new={'apk':NEWAPK,'elf':NEWELF,'chain':m.CHAIN,'module_artifacts':{n:{'sha256':h}for n,h in files.items()}}
 dep=[Path(__file__),HERE/'transaction.py',HERE/'installer.py',HERE/'identity.py',m.COMMON,m.SOURCE/'scripts/dev/android-game-launch.py',R/'scripts/dev/android-game-background-save.py',R/'scripts/dev/android-game-walk-to.py',SOURCE_TOOLS/'run-game.py',SOURCE_TOOLS/'reused-transaction.py',NATIVE/'composition.json',TABLE]
 return {'old':old,'new':new,'raw':raw,'baseline':base,'overlay':delta,'table':composition,'inputs':{**{str(p):h for p,h in pins.items()},**{str(p):sha(p)for p in dep}}}
def fixture_gate(path,digest):
 require(path is not None and digest=='755c634376a6bdebde5168bbf0b7cc3e1694c8398bd2f8940a7a51790f8dd9ca','--execute needs exact root-pinned actual AA combined fixture acceptance')
 require(sha(path)==digest,'root fixture acceptance changed');j=read(path)
 require(j.get('status')=='ACTUAL_AA_ASTC_NATIVE_WINDOW_ORIGINAL_NVN_ACCEPTED_NOT_GAME'and j.get('runtime')==COMMIT and j.get('driver_sha256')==m.SO and j.get('private_driver_live_maps_identity_verified')is True and j.get('original_unchanged_and_stopped')is True and j.get('all_cleanup_verified')is True,'root fixture receipt wrong lane or cleanup')
 native=j.get('native',{});lane=j.get('lane',{})
 require(native.get('result')=='passed'and native.get('checks')==372 and native.get('full_all_mips')is True and native.get('tail_packaged_and_B2T')is True and native.get('shared_gpu_gate')is True and lane.get('native_allocation_lane')is True and lane.get('window_two_unions')is True and lane.get('no_payload_copy')is True and lane.get('last_release_collected')is True and lane.get('import_bytes')==135168,'actual AA+ASTC physical fixture evidence incomplete')
 report=Path(j['actual_result']);require(sha(report)==j['actual_result_sha256'],'actual fixture report changed')
 return {str(path):digest,str(report):j['actual_result_sha256']}
def debugger_gate(path,digest):
 require(path is not None and digest is not None and sha(path)==digest,'--execute needs frozen official LLDB helper sourceReady + SHA')
 j=read(path);require(j.get('capture_protocol')=='native-bind-transfer-fragment-v2','old debugger does not capture actual FRAGMENT use');require(j.get('runtime_commit')==COMMIT and j.get('runtime_ELF_sha256')==NEWELF,'LLDB helper wrong target ELF')
 files=j['files'];require(files and all(sha(Path(x['path']))==x['sha256']for x in files),'LLDB helper bytes changed');server=j['server'];require(sha(Path(server['path']))==server['sha256'],'LLDB server bytes changed')
 run=Path(j['run_debugger']);require(run.name=='run-debugger.py'and any(x['path']==str(run)for x in files),'helper entry not frozen')
 return run,server,{str(path):digest,**{x['path']:x['sha256']for x in files},server['path']:server['sha256']}
def install(dev,apk,expected,out):
 installer.install(dev,apk,expected,out,require,PKG)
def current_owned(label,inv,d,p):
 if inv is None or inv.get(m.OWNER)!=d['owner_sha256']:return False
 if label=='driver':return inv.get('libvulkan_freedreno.so')==m.SO and all(n in(m.OWNER,'libvulkan_freedreno.so')or n.startswith(('shader-cache/','mesa_shader_cache/','.cache/'))for n in inv)
 if label=='modules':return inv=={m.OWNER:d['owner_sha256'],**{n:v['sha256']for n,v in p['new']['module_artifacts'].items()}}
 if label=='overlay':return inv=={m.OWNER:d['owner_sha256'],**p['overlay']}
 if label=='native':return inv=={m.OWNER:d['owner_sha256'],'native-layouts.bin':p['table']['native_table_sha256']}
 return True # Original temp/cache retained atomically; new ones have this token.
def accepted_debugger(d):
 j=d.get('debugger_result',{});return j.get('native_binding_accepted')is True and j.get('owned_debugger_cleanup_complete')is True and j.get('transfer_and_fragment_proven')is True

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--execute',action='store_true');a.add_argument('--report',type=Path,required=True)
 a.add_argument('--actual-fixture-root-review',type=Path);a.add_argument('--actual-fixture-root-review-sha256');a.add_argument('--debugger-source-ready',type=Path);a.add_argument('--debugger-source-ready-sha256');arg=a.parse_args();p=proof()
 out=arg.report.parent.resolve();require(out.is_relative_to(R/'.local/logs'),'private outputs only');out.mkdir(parents=True,exist_ok=True)
 plan={'offline':True,'experiment':'root-authorized single reversible validation; overlay deploy_allowed=false is preserved, not production deployment','source_inputs':p['inputs'],'candidate':p['new'],'driver':m.SO,'stages':['one sharedON/window16MiB fixed3D.75 target60 original ARM64 native material'],'output':'2750x1260','overlay_exact_five':p['overlay'],'native_table':p['table']['native_table_sha256'],'native_payload_bytes':135168,'table_device_path':ABS_TABLE,'raw_RomFS_deployment':False,'profiling':False,'self_timing':False,'requires':'root actual AA+ASTC combined fixture receipt and frozen official LLDB entry/success-use helper; installed table alone cannot pass','claims':'one real material functional admission only; no full-game performance/heat/6GB claim'}
 if not arg.execute:print(json.dumps(plan,ensure_ascii=False,indent=2));return
 p['inputs'].update(fixture_gate(arg.actual_fixture_root_review,arg.actual_fixture_root_review_sha256));debugger,server,deps=debugger_gate(arg.debugger_source_ready,arg.debugger_source_ready_sha256);p['inputs'].update(deps)
 require(not arg.report.exists(),'no retry same transaction');dev=m.common.Device('10CG570J59001U1');dev.identity(p['old'])
 require(dev.adb('shell','getprop','ro.product.model')==p['baseline']['model'],'physical Q735 model changed')
 token=uuid.uuid4().hex;d={'token':token,'serial':'10CG570J59001U1','controller_sha':sha(__file__),'source_inputs':p['inputs'],'cleanup_errors':[],'stage':'prepared','owner_sha256':hashlib.sha256((token+'\n').encode()).hexdigest(),'old_dirs':{},'plan':plan}
 stage='files/runtime/.native-one-'+token;d['private_stage']=stage;d['transfer']='/data/local/tmp/d2r-native-one-'+token+'.tar'
 def write():m.journal(arg.report,d)
 def original_check():
  dev.identity(p['old'])
  for label,target in DIRS:
   expected=None if label=='native'else{n.removeprefix(target+'/'):h for n,h in p['baseline']['inventories'][target].items()}
   require(m.inventory(dev,target)==expected,'original directory mismatch '+label)
 original_check();require(m.inventory(dev,'shared_prefs')is None,'unexpected prefs preserve');require(dev.adb('shell','settings','get','global','enable_gpu_debug_layers')in('null','0'),'GPU layers enabled')
 require(not any(re.match(r'\[debug\.mesa\.[^]]*\]: \[.+\]',x)for x in dev.adb('shell','getprop').splitlines()),'Mesa overrides active')
 for label,target in DIRS:d['old_dirs'][label]=m.inventory(dev,target)
 dev.app('mkdir',m.LOCK);dev.app('sh','-c','printf "%s\\n" "$2" > "$1"','sh',m.LOCK+'/owner',token);write();created=False
 try:
  dev.app('sh','-c','test ! -e "$1"','sh',stage);dev.app('mkdir','-p',stage+'/incoming',stage+'/backup');created=True
  owner=out/'owner';owner.write_text(token+'\n');payload=out/'owned.tar'
  with tarfile.open(payload,'x',dereference=True)as tar:
   tar.add(m.DRIVER,arcname='driver/libvulkan_freedreno.so',recursive=False)
   for n in p['new']['module_artifacts']:tar.add(Path(p['raw']['bundle'])/n,arcname='modules/'+n,recursive=False)
   for n in sorted(p['overlay']):tar.add(UI/n,arcname='overlay/'+n,recursive=False)
   tar.add(TABLE,arcname='native/native-layouts.bin',recursive=False);tar.add(Path(server['path']),arcname='temp/d2r-lldb-server',recursive=False)
   for label,target in DIRS:tar.add(owner,arcname=label+'/'+m.OWNER,recursive=False)
  owner.unlink();d['tar_sha256']=sha(payload);write();dev.adb('push',str(payload),d['transfer']);require(dev.digest(d['transfer'])==d['tar_sha256'],'tar transfer changed')
  dev.app('tar','xf',d['transfer'],'-C',stage+'/incoming');dev.app('chmod','700',stage+'/incoming/temp/d2r-lldb-server')
  for label,target in DIRS:
   if d['old_dirs'][label]is not None:m.rename_dir(dev,target,stage+'/backup/'+label,d['old_dirs'][label]);d.setdefault('backed_up',[]).append(label);write()
   inv=m.inventory(dev,stage+'/incoming/'+label);require(current_owned(label,inv,d,p),'incoming changed '+label);m.rename_dir(dev,stage+'/incoming/'+label,target,inv);d.setdefault('published',[]).append(label);write()
  install(dev,APK,NEWAPK,out/'install-candidate.log');d['candidate_installed']=True;write();dev.identity(p['new'])
  m.NATIVE_TABLE_LOCAL=TABLE;m.NATIVE_COMPOSITION=p['table'];m.DEBUGGER=debugger;m.NATIVE_TABLE=ABS_TABLE;m.PACK_SHA=p['table']['native_pack_content_sha256'];m.PRODUCER_SHA=p['table']['producer_source_sha256']
  phaseout=out/'native16';phaseout.mkdir();phase={**p['new'],'shared_import_window_bytes':16777216,'serial':d['serial'],'remote_screenshot':'/data/local/tmp/d2r-native-one-'+token+'.png'};d['native_phase']=phase;write()
  try:m.wait_game(dev,phaseout/'transaction.json',phase,phaseout)
  finally:write()
  require(phase.get('original_HUD_reached')and phase.get('fresh_save_then_stopped'),'game did not reach original HUD/fresh save2')
  require(accepted_debugger(phase),'native binding not proven: inspect official LLDB result; missing hits are inconclusive, not runtime failure')
  d['stage']='one-real-native-material-original-game-transfer-fragment-visual-captured';write()
 except BaseException as e:d['error']=repr(e);write()
 finally:
  try:
   require(not installer.UNRESOLVED,'owned install result unresolved: preserve backups and journal; do not restore or send another install')
   dev.stopped();require(dev.app('cat',m.LOCK+'/owner')==token,'lock owner changed')
   if created:
    for label,target in reversed(DIRS):
     backup=stage+'/backup/'+label;stored=m.inventory(dev,backup);old=d['old_dirs'][label]
     if stored is None:
      if old is None:
       inv=m.inventory(dev,target)
       if inv is not None:require(current_owned(label,inv,d,p),'unowned new dir '+label);m.rename_dir(dev,target,stage+'/after-'+label,inv)
       continue
      require(m.inventory(dev,target)==old,'missing backup '+label);continue
     require(stored==old,'backup mutated '+label);inv=m.inventory(dev,target)
     if inv is not None:require(current_owned(label,inv,d,p),'unowned current dir '+label);m.rename_dir(dev,target,stage+'/after-'+label,inv)
     m.rename_dir(dev,backup,target,stored)
   loc=dev.adb('shell','pm','path',PKG).removeprefix('package:');apksha=dev.adb('shell','sha256sum',loc).split()[0];require(apksha in(NEWAPK,m.APK),'unknown APK preserve')
   if apksha==NEWAPK:install(dev,OLDAPK,m.APK,out/'restore-original-apk.log')
   original_check();require(m.inventory(dev,'shared_prefs')is None,'unexpected prefs preserve');d['original_APK_modules29_UI4_driver_cache_temp_chain_restored']=True;write()
   if created:
    allowed=[stage+'/incoming/'+x+'/'for x,_ in DIRS]+[stage+'/after-'+x+'/'for x,_ in DIRS];files=dev.app('find',stage,'-type','f').splitlines();require(all(n.startswith(tuple(allowed))for n in files),'unknown staging file preserve')
    for start in range(0,len(files),40):
     args=[]
     for n in files[start:start+40]:args.extend([n,dev.digest(n)])
     dev.app('sh','-c','set -eu; test -z "$(pidof "$1" || true)"; shift; while test "$#" -gt 0; do test "$(sha256sum "$1" | cut -d" " -f1)" = "$2"; rm "$1"; shift 2; done','sh',PKG,*args)
    for n in sorted(dev.app('find',stage,'-type','d').splitlines(),key=lambda s:(s.count('/'),s),reverse=True):dev.app('rmdir',n)
   if dev.digest(d['transfer'])is not None:require(dev.digest(d['transfer'])==d['tar_sha256'],'transfer mutated');dev.adb('shell','rm',d['transfer'])
   dev.app('rm',m.LOCK+'/owner');dev.app('rmdir',m.LOCK);d['cleanup_complete']=True;write()
  except BaseException as e:d['cleanup_errors'].append(repr(e));write()
 print(json.dumps({'stage':d['stage'],'error':d.get('error'),'cleanup_errors':d['cleanup_errors'],'restored':d.get('original_APK_modules29_UI4_driver_cache_temp_chain_restored')},indent=2));require(not d.get('error')and not d['cleanup_errors']and d.get('cleanup_complete'),'one-material transaction incomplete; journal preserved')
if __name__=='__main__':main()
