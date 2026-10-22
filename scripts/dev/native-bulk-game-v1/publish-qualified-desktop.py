#!/usr/bin/env python3
"""Q-local matched installation; offline unless --execute. Never adopts a live game.
Original test runner still restores its original state. This separate publisher
uses its unchanged proof, installer and directory-CAS primitives, then validates
two ordinary launches. No sampling, target function calls, or game rewrites.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, shlex, subprocess, sys, tarfile, uuid
HERE=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('accepted_desktop_runner',HERE/'run-game.py')
r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
m=r.m;installer=r.installer;require=r.require;sha=r.sha;read=r.read;pinned=r.pinned
R=r.R;PKG=r.PKG
PROFILE='files/runtime/launch-profile.properties'
RECEIPT='files/runtime/launch-profile-receipt.json'
FIXED={'schema':'1','vulkan_provider':'adreno','shared_guest_heap':'true',
 'pool_window_bytes':'1048576','shared_lazy_windows':'true','shared_image_raw_observe':'false',
 'driver_span_reference':'true','pixel_local':'true','pixel_local_shared_ds':'false'}
IDENTITY_KEYS={'name','apk_sha256','device_fingerprint','driver_sha256','modules_manifest_sha256',
 'shader_chain_sha256','native_table','native_table_sha256','native_pack_sha256','native_producer_sha256'}
PROFILE_KEYS=set(FIXED)|IDENTITY_KEYS|{'receipt_sha256'}


def row(path):
 p=Path(path).resolve();return {'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size}


def properties(values):
 require(set(values)==PROFILE_KEYS,'exact schema1 desktop profile keys required')
 require(all(isinstance(v,str) and v and not any(c in v for c in '\\\r\n\x00') and v==v.strip()
             for v in values.values()),'profile values must be single-line unescaped strings')
 require(0<len(values['name'].encode('utf-16-le'))//2<=64,'profile name exceeds Java64 UTF16-unit bound')
 require(all(values[k]==v for k,v in FIXED.items()),'unqualified desktop cold policy')
 for k in [k for k in values if k.endswith('_sha256')]:
  require(re.fullmatch('[0-9a-f]{64}',values[k]) is not None,'invalid profile hash '+k)
 n=Path(values['native_table']);require(not n.is_absolute() and '..' not in n.parts
    and str(n)=='native-one-resources/native-layouts.bin','native table differs from published inventory')
 data=('\n'.join(k+'='+values[k] for k in sorted(values))+'\n').encode('utf-8')
 require(len(data)<=16384,'desktop profile exceeds Java schema budget');return data


def proof(path,digest):
 require(sha(path)==digest,'publication input changed');a=read(path)
 require(set(a)=={'schema','qualified_inputs','delivery_receipt','output','execution_ready'}
         and a['schema']==1 and type(a['execution_ready']) is bool,'publication input fields')
 qp=pinned(a['qualified_inputs']);p=r.proof(qp,a['qualified_inputs']['sha256'])
 d=read(pinned(a['delivery_receipt']));required={'status','serial','device_fingerprint','runtime',
  'canonical','raw','provider','composition','qualified_inputs','controlled_game_qualification',
  'Q_local_publish_allowed','global_default_enable','historical_asset_receipts_unchanged','profile'}
 require(required<=set(d) and d['status']=='ROOT_REVIEWED_Q_LOCAL_DESKTOP_DELIVERY_CANDIDATE'
   and d['serial']==p['serial']=='10CG570J59001U1' and d['runtime']==p['runtime_commit']
   and d['Q_local_publish_allowed'] is True and d['global_default_enable'] is False
   and d['historical_asset_receipts_unchanged'] is True,'new exact Q-local delivery authorization required')
 for key,actual in [('canonical',p['canonical']),('raw',p['raw']),('provider',p['driver']),
                    ('qualified_inputs',a['qualified_inputs'])]:
  require(d[key]['sha256']==actual['sha256'] and pinned(d[key])==pinned(actual),'delivery identity differs '+key)
 phase=p['phases'][1];require(phase['kind']=='native' and d['composition']==phase['composition'],
    'delivery must retain qualified full2838 composition')
 controlled=read(pinned(d['controlled_game_qualification']))
 require(controlled.get('runtime')==p['runtime_commit'] and controlled.get('actual_saved_stopped_original_restored') is True,
    'delivery requires root-reviewed completed original-game qualification')
 require(p['baseline_data']['chain']=='9507875007c6' and p.get('_asset_validation'),
    'preserved original950 shader and original full-asset gate required')
 baseline_shaders=r.expected_baseline(p,'files/runtime/shaders')
 require(baseline_shaders and '.chain' in baseline_shaders,'complete original shader inventory required')
 v=d['profile'];require(isinstance(v,dict) and set(v)==PROFILE_KEYS-{'receipt_sha256'},'root profile selection fields')
 values={**v,'receipt_sha256':a['delivery_receipt']['sha256']}
 expected={'apk_sha256':p['new_apk'],'device_fingerprint':d['device_fingerprint'],
  'driver_sha256':p['driver']['sha256'],'modules_manifest_sha256':p['modules']['rewrite-manifest.json'],
  'shader_chain_sha256':baseline_shaders['.chain'],'native_table_sha256':phase['native_table']['sha256'],
  'native_pack_sha256':phase['pack_sha'],'native_producer_sha256':phase['producer_sha']}
 require(all(values[k]==v for k,v in expected.items()),'profile identity differs from authenticated inputs')
 data=properties(values)
 canonical=read(pinned(p['canonical']));signer=canonical['production_signer_SHA256']
 tool=R/'.local/android-sdk/build-tools/36.0.0/apksigner'
 cert=subprocess.check_output([str(tool),'verify','--verbose','--print-certs',p['apk']],text=True,stderr=subprocess.STDOUT,timeout=30)
 require(signer in cert and 'Verified using v3 scheme (APK Signature Scheme v3): true' in cert,
   'canonical APK signer verification failed')
 require(all(Path(n).parts[0] not in ('save','saves','shared_prefs')for n in p['modules']), 'save collision')
 return dict(outer=a,p=p,delivery=d,phase=phase,profile_values=values,profile_bytes=data,
             original_shader_inventory=baseline_shaders,signer_output=cert)


def move_file(dev,source,target,expected):
 """Only missing primitive: digest-CAS of one stopped-app private regular file."""
 dev.stopped()
 for name in (source,target):
  require((name.startswith('files/runtime/') or name=='cache/'+m.OWNER) and '..' not in Path(name).parts,'unsafe publication file')
  dev.app('sh','-c','test ! -L "$1"','sh',name)
 require(dev.digest(source)==expected and dev.digest(target) is None,'file CAS conflict')
 dev.app('sh','-c','set -eu; test -z "$(pidof "$4" || true)"; test -f "$1"; test ! -L "$1"; test ! -e "$2"; test "$(sha256sum "$1" | cut -d" " -f1)" = "$3"; mv "$1" "$2"',
         'sh',source,target,expected,PKG)
 require(dev.digest(target)==expected and dev.digest(source) is None,'file CAS verification failed')


def state(dev):
 return json.loads(dev.app('tail','-n','1','files/runtime/game-state.jsonl'))


def save_owned(dev,own,out):
 current=dev.adb('shell','pidof '+PKG+' || true')
 if not current:return {'already_exited_no_restart':True}
 require(own and current==own,'unowned/replacement process: preserve installation')
 command=[sys.executable,str(R/'scripts/dev/android-game-background-save.py'),'--serial',dev.command[-1],
          '--report',str(out/'fresh-save.json')]
 with (out/'fresh-save.log').open('w')as log:subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=45)
 saved=read(out/'fresh-save.json')
 require(saved['fresh_original_lifecycle_save'] is True and saved['save_commits']==2
   and saved['pid']==int(own) and dev.adb('shell','pidof '+PKG+' || true')==own,'fresh save2 ownership failed')
 dev.adb('shell','am','force-stop',PKG);dev.stopped();return saved


def ordinary_round(dev,x,out,ordinal):
 out.mkdir();p=x['p'];own=None;result={'ordinal':ordinal,'no_Intent_extras':True}
 try:
  dev.stopped()
  command=['shell','am','start','-W','-a','android.intent.action.MAIN','-c','android.intent.category.LAUNCHER','-n',PKG+'/.MainActivity']
  result['launch_argv']=command;result['am_output']=dev.adb(*command)
  current=dev.adb('shell','pidof '+PKG+' || true');own=m.claim_pid(result,current,None)
  require(own is not None,'ordinary desktop startup did not create owned process')
  observe=[sys.executable,str(m.SOURCE/'scripts/dev/android-game-launch.py'),'--attach','--serial',p['serial'],
      '--timeout','90','--report',str(out/'attach.json')]
  with (out/'attach.log').open('w')as log:subprocess.run(observe,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=100)
  seen=read(out/'attach.json');require(seen['pid']==int(own) and seen['original_hud'] is True,
     'ordinary desktop process did not reach original HUD')
  java=dev.adb('logcat','-d','--pid='+own,'-s','D2RLaunchProfile:I','*:S')
  (out/'launch-profile-logcat.txt').write_text(java)
  require('Desktop launch: '+x['profile_values']['name'] in java
    and 'Desktop launch profile rejected' not in java,'ordinary Java profile consumption not observed')
  current=state(dev);native=dev.app('cat','files/runtime/native.log');(out/'native.log').write_text(native)
  (out/'state.json').write_text(json.dumps(current,indent=2)+'\n')
  require(current.get('valid') and current.get('world_input_ready') and current['guest_heap']['shared'] is True
    and current['pool_windows']['window_bytes']==1048576 and current['shared_lazy_windows']['active'] is True
    and current['shared_image_raw_observe']['enabled_policy'] is False
    and current['driver_constants']['span_reference'] is True and current['pixel_local']['enabled'] is True
    and current['pixel_local']['shared_readonly_ds'] is False,'desktop selected cold policy differs')
  require('[vulkan] device=Turnip Adreno (TM) 735 ' in native and '[vulkan loader] custom=' in native
    and '[native resources] fixed table installed,' in native and 'FATAL' not in native
    and '[shared pool] immutable startup shared;' in native,'desktop provider/shared/native adoption failed')
  result.update(PID=int(own),original_HUD=True,shared_native_policy_observed=True)
 finally:
  result['lifecycle']=save_owned(dev,own,out)
  (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 return result


def publish(x,out,dev,install=installer.install,game_round=ordinary_round):
 """Separate publication; original comparison-controller finally is untouched."""
 p=x['p'];phase=x['phase'];token=uuid.uuid4().hex
 stage='files/runtime/.desktop-publication-'+token
 d={'token':token,'stage':'prepared','cleanup_errors':[],'old_dirs':{},'published':[],
    'old_files':{},'published_files':{},'owner_removed':[],
    'owner_sha256':hashlib.sha256((token+'\n').encode()).hexdigest(),
    'private_backup':stage,'rounds':[],'committed':False}
 def save():m.journal(out/'result.json',d)
 def own(label,values):
  if label in d['owner_removed'] and values is not None:values={**values,m.OWNER:d['owner_sha256']}
  return r.owned(label,values,d,p,phase)
 def original_dirs():
  for label,target in r.DIRS:
   require(m.inventory(dev,target)==r.expected_baseline(p,target),'original directory changed '+label)
 dev.identity(p['old']);original_dirs()
 require(m.inventory(dev,'shared_prefs')==r.expected_baseline(p,'shared_prefs'),'original user preferences changed')
 require(dev.adb('shell','getprop','ro.build.fingerprint')==x['delivery']['device_fingerprint'],
    'qualified Android fingerprint changed')
 require(dev.adb('shell','settings','get','global','enable_gpu_debug_layers') in ('null','0'),
    'GPU debug layers active')
 require(m.inventory(dev,'files/runtime/shaders')==x['original_shader_inventory'],
    'original950 shader inventory changed')
 for name in (PROFILE,RECEIPT):
  dev.app('sh','-c','test ! -L "$1"','sh',name);d['old_files'][name]=dev.digest(name)
 for label,target in r.DIRS:d['old_dirs'][label]=m.inventory(dev,target)
 dev.app('mkdir',m.LOCK)
 dev.app('sh','-c','printf "%s\n" "$2" > "$1"','sh',m.LOCK+'/owner',token)
 save();created=False
 try:
  dev.app('sh','-c','test ! -e "$1"','sh',stage)
  dev.app('mkdir','-p',stage+'/incoming',stage+'/backup',stage+'/after-failed',stage+'/markers')
  created=True
  owner=out/'owner';owner.write_text(token+'\n')
  profile=out/'launch-profile.properties';profile.write_bytes(x['profile_bytes'])
  payload=out/'publication.tar'
  with tarfile.open(payload,'x',dereference=True)as tar:
   tar.add(p['driver_path'],arcname='driver/libvulkan_freedreno.so',recursive=False)
   for n in p['modules']:tar.add(Path(p['raw_bundle'])/n,arcname='modules/'+n,recursive=False)
   for n in sorted(phase['overlay']):tar.add(Path(phase['overlay_root'])/n,arcname='overlay/'+n,recursive=False)
   tar.add(phase['native_table']['path'],arcname='native/native-layouts.bin',recursive=False)
   tar.add(profile,arcname='policy/profile',recursive=False)
   tar.add(x['outer']['delivery_receipt']['path'],arcname='policy/receipt',recursive=False)
   for label,_ in r.DIRS:tar.add(owner,arcname=label+'/'+m.OWNER,recursive=False)
  remote='/data/local/tmp/d2r-desktop-publication-'+token+'.tar'
  d['transfer']={'path':remote,'sha256':sha(payload)};save()
  dev.adb('shell','test','!','-e',remote);dev.adb('push',str(payload),remote)
  require(dev.digest(remote)==sha(payload),'publication transfer changed')
  dev.app('tar','xf',remote,'-C',stage+'/incoming')
  for label,target in r.DIRS:
   incoming=stage+'/incoming/'+label
   require(own(label,m.inventory(dev,incoming)),'incoming resource mismatch '+label)
   if d['old_dirs'][label] is not None:m.rename_dir(dev,target,stage+'/backup/'+label,d['old_dirs'][label])
   d.setdefault('backed_up',[]).append(label);save()
   m.rename_dir(dev,incoming,target,m.inventory(dev,incoming));d['published'].append(label);save()
  # Preserve learned original PSO files, never old logs or saved-game files.
  if d['old_dirs']['temp']:
   for n,h in d['old_dirs']['temp'].items():
    if not n.startswith('vulkan-pso/'):continue
    src=stage+'/backup/temp/'+n;dest='files/runtime/temp/'+n
    require(dev.digest(src)==h,'original PSO seed changed')
    dev.app('mkdir','-p',str(Path(dest).parent));dev.app('cp',src,dest)
    require(dev.digest(dest)==h,'original PSO seed copy changed')
  install(dev,Path(p['apk']),p['new_apk'],out/'install-candidate.log',require,PKG)
  dev.identity(p['new'])
  # Receipt first, profile last: no reader can select a half-published profile.
  for name,leaf,h in [(RECEIPT,'receipt',x['outer']['delivery_receipt']['sha256']),
                      (PROFILE,'profile',sha(profile))]:
   old=d['old_files'][name]
   if old is not None:move_file(dev,name,stage+'/backup/'+leaf,old)
   d.setdefault('backed_up_files',[]).append(name);save()
   move_file(dev,stage+'/incoming/policy/'+leaf,name,h);d['published_files'][name]=h;save()
  d['stage']='validating-two-ordinary-cold-starts';save()
  for ordinal in (1,2):
   d['rounds'].append(game_round(dev,x,out/('desktop-'+str(ordinal)),ordinal));save()
   dev.identity(p['new'])
  require(m.inventory(dev,'files/runtime/shaders')==x['original_shader_inventory'],
     'ordinary startup modified original950 shader bytes')
  require(m.inventory(dev,'shared_prefs')==r.expected_baseline(p,'shared_prefs'),'desktop validation changed user preferences')
  for label,target in r.DIRS:
   inv=m.inventory(dev,target);require(own(label,inv),'unowned published output '+label)
   move_file(dev,target+'/'+m.OWNER,stage+'/markers/'+label,d['owner_sha256'])
   d['owner_removed'].append(label);save()
  dev.identity(p['new'])
  inventories={target:m.inventory(dev,target)for _,target in r.DIRS}
  inventories['files/runtime/shaders']=x['original_shader_inventory']
  inventories['shared_prefs']=r.expected_baseline(p,'shared_prefs')
  inventories={target:None if inv is None else {target+'/'+name:h for name,h in inv.items()}
               for target,inv in inventories.items()}
  location=dev.adb('shell','pm','path',PKG).removeprefix('package:')
  require(location.startswith('/') and '\n' not in location,'delivered APK location differs')
  baseline={'serial':p['serial'],'package':PKG,'game_PID_absent':True,'APK_sha256':p['new_apk'],
    'model':p['baseline_data']['model'],'platform':p['baseline_data']['platform'],'APK_path':location,
    'host_ELF_SHA256':p['new_elf'],'chain':p['baseline_data']['chain'],
    'profile':row(profile),'publication_receipt':x['outer']['delivery_receipt'],
    'private_rollback_backup':stage,'original_APK_backup':row(p['old_apk']),'inventories':inventories,
    'desktop_rounds':d['rounds'],'no_sustained_whole_game_or_performance_claim':True}
  m.journal(out/'delivered-baseline.json',baseline)
  d['committed']=True;d['stage']='Q-local-desktop-published-saved2-stopped';save()
 except BaseException as error:
  d['error']=repr(error);save()
  # Never terminate/adopt a replacement PID, or issue a second unresolved install.
  try:
   require(not installer.UNRESOLVED,'installer unresolved: retain backups without second install')
   dev.stopped()
   for name,leaf in [(PROFILE,'profile'),(RECEIPT,'receipt')]:
    if name in d['published_files']:
     move_file(dev,name,stage+'/after-failed/'+leaf,d['published_files'][name])
    if name in d.get('backed_up_files',[]) and d['old_files'][name] is not None:
     move_file(dev,stage+'/backup/'+leaf,name,d['old_files'][name])
   if created:
    for label,target in reversed(r.DIRS):
     if label in d['published']:
      inv=m.inventory(dev,target);require(own(label,inv),'unowned failure output '+label)
      m.rename_dir(dev,target,stage+'/after-failed/'+label,inv)
     if label in d.get('backed_up',[]) and d['old_dirs'][label] is not None:
      m.rename_dir(dev,stage+'/backup/'+label,target,d['old_dirs'][label])
   loc=dev.adb('shell','pm','path',PKG).removeprefix('package:')
   installed=dev.adb('shell','sha256sum',loc).split()[0]
   require(installed in (p['old']['apk'],p['new_apk']),'unknown APK: preserve transaction')
   if installed==p['new_apk']:install(dev,Path(p['old_apk']),p['old']['apk'],out/'restore-original.log',require,PKG)
   dev.identity(p['old']);original_dirs()
   require(m.inventory(dev,'files/runtime/shaders')==x['original_shader_inventory'],
      'original shader inventory changed: preserve unknown writes')
   require(all(dev.digest(n)==h for n,h in d['old_files'].items()),'old desktop profile differs')
   d['original_restored']=True;save()
  except BaseException as cleanup:
   d['cleanup_errors'].append(repr(cleanup));save()
 finally:
  # The backup directory is deliberately retained for explicit rollback.
  try:
   require(d['committed'] or d.get('original_restored'),'unresolved publication: retain owned lock')
   dev.stopped();require(dev.app('cat',m.LOCK+'/owner')==token,'publication lock owner changed')
   dev.app('rm',m.LOCK+'/owner');dev.app('rmdir',m.LOCK);d['lock_released']=True
  except BaseException as cleanup:d['cleanup_errors'].append(repr(cleanup))
  transfer=d.get('transfer')
  if transfer:
   try:
    require(dev.digest(transfer['path'])==transfer['sha256'],'transfer ownership changed')
    dev.adb('shell',shlex.join(['sh','-c','test "$(sha256sum "$1" | cut -d" " -f1)" = "$2" && rm "$1"',
                             'sh',transfer['path'],transfer['sha256']]))
    d['transfer_removed']=True
   except BaseException as cleanup:d['cleanup_errors'].append(repr(cleanup))
  save()
 return d


def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--inputs',type=Path,required=True)
 a.add_argument('--inputs-sha256',required=True);a.add_argument('--report',type=Path,required=True)
 a.add_argument('--execute',action='store_true');args=a.parse_args();x=proof(args.inputs,args.inputs_sha256)
 out=args.report.parent.resolve();require(out.is_relative_to(R/'.local/logs')
  and str(out)==x['outer']['output'] and args.report.name=='result.json','publication report/output mismatch')
 if not args.execute:
  print(json.dumps({'offline':True,'runtime':x['p']['runtime_commit'],'profile':x['profile_values'],
    'two_ordinary_cold_starts':True,'original_test_restore_unchanged':True,
    'execution_ready':x['outer']['execution_ready'],'no_FPS_or_thermal_claim':True},indent=2));return
 require(x['outer']['execution_ready'] is True,'new actual delivery qualification required before mutations')
 require(not out.exists(),'publication output must be unique')
 out.mkdir(parents=True);(out/'apksigner.txt').write_text(x['signer_output'])
 result=publish(x,out,m.common.Device(x['p']['serial']))
 require(result['committed'] and not result['cleanup_errors'],'publication failed; inspect retained backups and journal')

if __name__=='__main__':main()
