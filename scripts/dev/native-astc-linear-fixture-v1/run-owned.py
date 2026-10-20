#!/usr/bin/env python3
"""One independent package and cold finite GPU oracle. Default OFFLINE. No game writes."""
import argparse,hashlib,json,os,re,shlex,signal,subprocess,time,uuid,zipfile,tarfile
from pathlib import Path
from types import SimpleNamespace
import owned_installer
HOST=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeastclinearfixture';GAME='com.d2rmobile.nativeport'
DRIVER='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce';LAYOUT='ab91c540847448360a44496b50045159';SHAPES=[[1,1,1],[5,7,3],[63,65,7],[64,64,7],[95,97,7],[128,128,8],[128,256,9],[256,256,9]];FORMATS={157,158,165,166}
def require(c,m):
 if not c:raise AssertionError(m)
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def validate_rows(rows,selector):
 selected=list(range(8))if selector==-1 else[selector]
 starts=[x for x in rows if x.get('native_linear_matrix_start')];term=[x for x in rows if x.get('native_linear_matrix_terminal')]
 require(len(starts)==len(term)==1,'exact start/terminal');start,end=starts[0],term[0]
 require(start['uid']>=10000 and start['pid']==end['pid']and start['uid']==end['uid'],'ordinary first PID identity')
 require(not start['real_resource_bake']and not end['real_resource_bake']and end['GPU_sampling_requested'],'sampling mode actually executed')
 require(start['shape_selector']==selector and end['rc']==0 and end['completed_shapes']==len(selected),'finite shape completion')
 identities=[x for x in rows if x.get('native_device_identity')];require(len(identities)==len(selected),'actual identity per shape')
 for x in identities:require(x['layout_UUID']==LAYOUT and x['driverID']==18 and x['vendorID']==0x5143,'actual driver identity')
 require(len({(x['device_UUID'],x['driver_UUID'],x['layout_UUID'])for x in identities})==1,'identical device/driver/layout across shapes')
 groups={};current=None
 for x in rows:
  if x.get('native_linear_shape_begin'):
   current=x['index'];require(current in selected and current not in groups,'unexpected/repeated shape');require([x['width'],x['height'],x['levels']]==SHAPES[current],'shape recipe');groups[current]=[]
  elif current is not None:groups[current].append(x)
 require(set(groups)==set(selected),'full selected coverage')
 for index,part in groups.items():
  w,h,levels=SHAPES[index];done=[x for x in part if x.get('native_linear_shape_complete')];require(len(done)==1 and done[0]['index']==index and done[0]['formats']==4,'complete four formats')
  samples=[x for x in part if x.get('native_all_base_mips_sampled')];copies=[x for x in part if x.get('native_all_mip_buffer_copy')];imports=[x for x in part if x.get('native_import_sampled')]
  require(len(samples)==32 and len(copies)==len(imports)==16,'4formats x2patterns x2lifecycles, before+after GPUcopy sampling')
  plain=[x for x in part if x.get('native_plain_hostcopy_roundtrip')];old=[x for x in part if x.get('native_old_AA_opaque_identity_refused')]
  require(len(plain)==8 and len(old)==4 and {x['format']for x in old}==FORMATS and all(x['production_table_rejection_proven']is False for x in old),'plain hostcopy and exact old-AA opaque negatives; no production-table claim')
  for x in plain:require(x['format']in FORMATS and x['levels']==levels and x['every_mip_compressed_blocks_exact'],'plain hostcopy allmips')
  for fmt in FORMATS:
   events=[('S'if x.get('native_all_base_mips_sampled')else 'C'if x.get('native_all_mip_buffer_copy')else 'I')for x in part if x.get('format')==fmt and any(x.get(k)for k in ('native_all_base_mips_sampled','native_all_mip_buffer_copy','native_import_sampled'))]
   require(events==['S','C','S','I']*4,'native payload sampled BEFORE and AFTER GPU upload for each version/lifecycle')
   require(sum(x['format']==fmt for x in samples)==8,'sample format coverage');require({(x['lifecycle'],x['pattern'])for x in imports if x['format']==fmt}=={(0,0),(0,1),(1,0),(1,1)},'lifetime/version coverage')
  for x in samples:require([x['width'],x['height'],x['levels']]==[w,h,levels]and x['base_views']==levels and x['allpixel_assertions_retained'],'every mip/baseMip allpixel')
  for x in copies:require([x['width'],x['height'],x['levels']]==[w,h,levels]and x['B2T_T2B_blocks_exact'],'all mip compressed GPU transfer')
  for x in imports:require(x['format']in FORMATS and x['memory_flags']&4 and x['memory_flags']&8 and not x['extra_image_allocation'],'cached coherent native import')
 require(not start['game_modules_executed']and not end['game_modules_executed'],'no game')
 return {'selected_shapes':selected,'identity':identities[0],'ordinary_UID':end['uid'],'PID':end['pid'],'nearest_GPU_allpixel_per_mip_and_baseMip':True,'all_mip_buffer_roundtrip':True,'two_patterns_two_lifetimes':True,'opaque_whole_payload_hash_and_guard':True,'validation':'disabled_explicitly_not_VVL_coverage','linear_trilinear_filtering_proven':False,'real_NVN_late_Buffer_companion_proven':False}
def validate_resource_rows(rows,pid):
 starts=[x for x in rows if x.get('native_linear_matrix_start')];ends=[x for x in rows if x.get('native_linear_matrix_terminal')];ids=[x for x in rows if x.get('native_device_identity')]
 require(len(starts)==len(ends)==len(ids)==1,'real resource cold start/end/identity')
 require(starts[0]['real_resource_bake']and ends[0]['real_resource_bake']and not ends[0]['GPU_sampling_requested'],'producer boundary')
 require(starts[0]['pid']==ends[0]['pid']==pid and starts[0]['uid']==ends[0]['uid']>=10000 and ends[0]['rc']==0,'actual producer PID')
 require(ids[0]['layout_UUID']==LAYOUT and ids[0]['driverID']==18 and ids[0]['vendorID']==0x5143,'actual new driver query')
 finish=[x for x in rows if x.get('test')=='native-host-image-payload'];require(len(finish)==1 and finish[0]['mode']=='--resource-bake'and finish[0]['result']=='passed'and finish[0]['formats']==1,'original resource bake terminal')
 return {'PID':pid,'ordinary_UID':ends[0]['uid'],'actual_identity':ids[0],'producer_only_no_GPU_sampling':True,'real_NVN_late_Buffer_companion_proven':False}
P=argparse.ArgumentParser(description=__doc__);P.add_argument('--serial',required=True);P.add_argument('--artifact',type=Path,required=True);P.add_argument('--driver',type=Path,required=True);P.add_argument('--baseline',type=Path,required=True);P.add_argument('--original-apk',type=Path,required=True);P.add_argument('--shape',type=int,choices=list(range(-1,8)),default=-1);P.add_argument('--output',type=Path,required=True);P.add_argument('--mode',choices=['matrix','resource-bake'],default='matrix');P.add_argument('--resource-input',type=Path);P.add_argument('--matrix-acceptance',type=Path);P.add_argument('--execute',action='store_true');a=P.parse_args()
m=json.loads((a.artifact/'build.json').read_text());apk=a.artifact/'candidate.apk';require(m['package']==PKG and sha(apk)==m['APK']['sha256'],'APK identity');require(sha(a.driver)==DRIVER==m['expected_driver_SHA256'],'driver identity');require(m['source_shapes']==SHAPES and set(m['formats'])==FORMATS,'matrix declaration')
with zipfile.ZipFile(apk)as z:
 require(len(m['native_libraries'])==6,'six exact fixture/hooks libraries')
 for n,h in m['native_libraries'].items():require(hashlib.sha256(z.read('lib/arm64-v8a/'+n)).hexdigest()==h,'SO identity')
baseline=json.loads(a.baseline.read_text());require(baseline['serial']==a.serial and baseline['package']==GAME and baseline['game_PID_absent']and baseline['no_device_mutation'],'readonly original baseline');require(sha(a.original_apk)==baseline['APK_sha256'],'exact original APK');inventories=baseline['inventories'];require(len(inventories['files/runtime/modules'])>=28,'original modules bound');expected={}
for folder,items in inventories.items():
 require((folder.startswith('files/')or folder in('cache','shared_prefs'))and '..'not in Path(folder).parts,'baseline folder')
 if items is not None:
  require(isinstance(items,dict),'baseline inventory schema')
  for name,h in items.items():require(name.startswith(folder+'/')and '..'not in Path(name).parts and re.fullmatch('[0-9a-f]{64}',h),'baseline file identity');expected[name]=h
resource_hashes={}
if a.mode=='resource-bake':
 require(a.shape==7 and a.resource_input is not None and a.matrix_acceptance is not None,'resource mode requires exact firepit input and actual matrix receipt')
 resource_hashes={'resource-input.bin':'d078631c7797b699200230a09533c9ad394a40fb9244fe0f75d905974ca49354','resource-source.json':'5d2e767a618a805d412f86c70be6f3c926897e5085621461390386f368e6d269'}
 for n,h in resource_hashes.items():require(sha(a.resource_input/n)==h,'original real resource input changed')
 matrix=json.loads(a.matrix_acceptance.read_text());require(matrix['stage']=='actual-native-ASTC-fully-linear-API-matrix-passed-not-game'and not matrix['cleanup_errors']and matrix['original_unchanged']and matrix['driver_sha256']==DRIVER,'actual clean matrix receipt required');validate_rows(matrix['actual_rows'],-1)
plan={'scope':'ONE cold independent producer API matrix, no original game writes; real NVN companion remains separate mandatory acceptance','serial':a.serial,'package':PKG,'APK_sha256':sha(apk),'driver_sha256':DRIVER,'layout_UUID_required_actual_query':LAYOUT,'selected_shapes':list(range(8))if a.shape==-1 else[a.shape],'functional_bound_seconds':180,'launches_allowed':1,'original_baseline':str(a.baseline),'original_baseline_sha256':sha(a.baseline),'original_APK_sha256':sha(a.original_apk),'controller_sha256':sha(__file__),'installer_sha256':sha(owned_installer.__file__),'mode':a.mode,'real_resource_inputs':resource_hashes,'matrix_acceptance_sha256':sha(a.matrix_acceptance)if a.matrix_acceptance else None,'device_access':bool(a.execute),'performance_claim':False,'game_acceptance':False}
if not a.execute:print(json.dumps(plan,indent=2));raise SystemExit(0)
require(not a.output.exists(),'new owned output required');a.output.mkdir(parents=True);B=[str(HOST/'.local/android-sdk/platform-tools/adb'),'-s',a.serial];token=uuid.uuid4().hex;tmp='/data/local/tmp/d2r-native-linear-'+token;result={**plan,'stage':'preflight','owned_tmp':tmp,'cleanup_errors':[]};first=None;installed=False;installation_requested=False;made_tmp=False;launch=None;original_before=None;crashing=False;ambiguous=False

def save():(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
def ad(*args,check=True,timeout=20):
 p=subprocess.run(B+list(map(str,args)),capture_output=True,timeout=timeout);err=p.stderr.decode(errors='replace');require(p.returncode==0 or not check,'ADB '+repr(args)+': '+err);return p.stdout.decode(errors='replace').strip()
def process(n):return ad('shell','pidof',n,check=False)
def exact_fixture():
 path=ad('shell','pm','path',PKG,check=False).removeprefix('package:')
 if not path:return False
 require(path.startswith('/')and '\n'not in path,'fixture APK location');require(ad('shell','sha256sum',path).split()[0]==sha(apk),'unclaimed fixture APK');return True
def snapshot():
 require(not process(GAME),'original game running: preserve it');path=ad('shell','pm','path',GAME).removeprefix('package:');require(path.startswith('/')and '\n'not in path,'original APK location');require(ad('shell','sha256sum',path).split()[0]==baseline['APK_sha256'],'original APK drift');got={}
 for folder,items in inventories.items():
  if items is None:require(ad('shell','run-as',GAME,'test','!','-e',folder)=='','absent baseline path');continue
  code='if test -d "$1"; then find "$1" -type f -exec sha256sum {} \\;; else exit 3; fi'
  raw=ad('exec-out','run-as',GAME,'sh','-c',shlex.quote(code),'sh',shlex.quote(folder))
  local={}
  for line in raw.splitlines():
   h,name=line.split(None,1);require(re.fullmatch('[0-9a-f]{64}',h),'inventory digest');local[name]=h
  require(local==items,'original inventory drift:'+folder);got[folder]=local
 require(ad('exec-out','run-as',GAME,'cat','files/runtime/shaders/.chain')==baseline['chain'],'original shader chain drift');return {'APK':baseline['APK_sha256'],'inventories':got,'chain':baseline['chain'],'PID_absent':True}
def stopped():require(not process(GAME)and not process(PKG),'installer process precondition')
def observe():
 global first,ambiguous
 raw=process(PKG);result['last_PID_raw']=raw
 if raw:
  require(raw.isdecimal(),'ambiguous PID');
  if first is None:first=raw;result['first_PID']=int(raw)
  elif first!=raw:ambiguous=True;save();raise RuntimeError('replacement PID: preserve it')
 save();return raw
def stop_owned():
 if not installed:return
 raw=process(PKG)
 if not raw:return
 if crashing or ambiguous:
  deadline=time.monotonic()+30
  while raw and time.monotonic()<deadline:time.sleep(.5);raw=process(PKG)
  require(not raw,'crashing/unclaimed PID did not exit: preserve it');return
 require(first is not None and raw==first,'unclaimed fixture PID');require(exact_fixture(),'fixture identity lost');ad('shell','am','force-stop',PKG);require(not process(PKG),'fixture stop failed')
def interrupt(sig,frame):raise KeyboardInterrupt('owned transaction interrupted')
signal.signal(signal.SIGTERM,interrupt);signal.signal(signal.SIGINT,interrupt);save()
try:
 original_before=snapshot();result['original_before']=original_before;save();require(not ad('shell','pm','path',PKG,check=False)and not process(PKG),'existing package must not be adopted')
 layers=ad('shell','settings','get','global','enable_gpu_debug_layers');target=ad('shell','settings','get','global','gpu_debug_app');require(not(layers=='1'and target==PKG),'external layers target fixture');result['external_layers']={'enabled':layers,'target':target}
 ad('shell','test','!','-e',tmp);ad('shell','mkdir',tmp);made_tmp=True;ad('shell',shlex.join(['sh','-c','printf "%s\\n" "$2" > "$1"','sh',tmp+'/owner',token]));installation_requested=True
 owned_installer.install(SimpleNamespace(command=B,stopped=stopped),apk,sha(apk),a.output/'install.txt',require,PKG);installed=True;require(exact_fixture()and not process(PKG),'fresh exact fixture installation')
 ad('push',a.driver,tmp+'/libvulkan_freedreno.so');ad('shell','run-as',PKG,'mkdir','-p','files/runtime/driver');ad('shell','run-as',PKG,'cp',tmp+'/libvulkan_freedreno.so','files/runtime/driver/libvulkan_freedreno.so');require(ad('exec-out','run-as',PKG,'sha256sum','files/runtime/driver/libvulkan_freedreno.so').split()[0]==DRIVER,'private driver SHA')
 aliases={}
 for path in ['/data/user/0/'+PKG+'/files/runtime/driver/libvulkan_freedreno.so','/data/data/'+PKG+'/files/runtime/driver/libvulkan_freedreno.so']:
  raw=ad('exec-out','run-as',PKG,'stat','-c','%d:%i:%s',path);require(re.fullmatch('[0-9]+:[0-9]+:[0-9]+',raw),'driver stat');dev,inode,size=map(int,raw.split(':'));require(size==a.driver.stat().st_size and inode>0,'driver bytes/inode');aliases[path]={'major':os.major(dev),'minor':os.minor(dev),'inode':inode,'size':size}
 require(len({tuple(v.values())for v in aliases.values()})==1,'driver aliases differ');result['driver_aliases']=aliases
 if resource_hashes:
  ad('shell','run-as',PKG,'mkdir','files/runtime/resource4')
  for name,h in resource_hashes.items():
   ad('push',a.resource_input/name,tmp+'/'+name);ad('shell','run-as',PKG,'cp',tmp+'/'+name,'files/runtime/resource4/'+name);require(ad('exec-out','run-as',PKG,'sha256sum','files/runtime/resource4/'+name).split()[0]==h,'owned resource input bytes')
 args=['shell','am','start','-W','-n',PKG+'/'+m['activity'],'--es','fixture_directory','/data/user/0/'+PKG+'/files/runtime','--ei','shape',str(a.shape),'--es','mode',a.mode]
 result['launch_command']=B+args;result['stage']='one-cold-launch';save()
 with(a.output/'launch.txt').open('w')as f:launch=subprocess.Popen(B+args,stdout=f,stderr=subprocess.STDOUT)
 deadline=time.monotonic()+180;rows=[];log=''
 while True:
  current=observe();log=ad('exec-out','run-as',PKG,'tail','-c','262144','files/runtime/native-linear-matrix.log',check=False)
  (a.output/'native-linear-matrix.log').write_text(log);rows=[]
  for line in log.splitlines():
   if line.startswith('{'):
    try:rows.append(json.loads(line))
    except json.JSONDecodeError:pass
  if re.search(r'MATRIX_FATAL|CHECK line|VVL |VUID-|Fatal signal',log):crashing=True;raise RuntimeError('raw native/API failure; no retry')
  terminal=[x for x in rows if x.get('native_linear_matrix_terminal')]
  if terminal:require(current and first==current,'terminal cannot replace first PID ownership');break
  require(not(first is not None and not current),'first PID exited without terminal');require(time.monotonic()<deadline,'finite functional timeout: no retry');time.sleep(.5)
 maps=ad('exec-out','run-as',PKG,'cat','/proc/'+first+'/maps');(a.output/'maps.txt').write_text(maps);result['actual_rows']=rows;save();proof=validate_rows(rows,a.shape)if a.mode=='matrix'else validate_resource_rows(rows,int(first));require(proof['PID']==int(first),'actual PID differs');mapped=[]
 for line in maps.splitlines():
  fields=line.split(None,5)
  if len(fields)==6 and fields[5]in aliases:
   major,minor=map(lambda s:int(s,16),fields[3].split(':'));v=aliases[fields[5]];require((major,minor,int(fields[4]))==(v['major'],v['minor'],v['inode']),'actual mapped driver inode differs');mapped.append(line)
 require(mapped and not any(s in maps for s in ['libVkLayer_','renderdoc','libvulkan_panfrost']),'actual candidate driver mapping/layers');require(ad('exec-out','run-as',PKG,'sha256sum','files/runtime/driver/libvulkan_freedreno.so').split()[0]==DRIVER,'driver bytes changed during run');result['proof']=proof;result['actual_driver_mappings']=mapped;save();stop_owned();result['fixture_PID_absent']=True
 # Pull bounded evidence only after GPU process stopped. Exclude driver/cache.
 selected=list(range(8))if a.shape==-1 else[a.shape];names=['native-linear-matrix.log']+(['resource4']if a.mode=='resource-bake'else['shape-%u-%ux%u'%(i,*SHAPES[i][:2])for i in selected]);remote='files/runtime/native-linear-evidence.tar';ad('shell','run-as',PKG,'tar','-cf',remote,'-C','files/runtime',*names)
 size=int(ad('exec-out','run-as',PKG,'stat','-c','%s',remote));require(0<size<=32<<20,'bounded evidence archive');digest=ad('exec-out','run-as',PKG,'sha256sum',remote).split()[0]
 with(a.output/'native-linear-evidence.tar').open('wb')as f:subprocess.run(B+['exec-out','run-as',PKG,'cat',remote],stdout=f,check=True,timeout=60)
 require(sha(a.output/'native-linear-evidence.tar')==digest,'evidence transfer SHA');result['evidence_archive']={'bytes':size,'sha256':digest};result['stage']='actual-native-ASTC-fully-linear-API-matrix-passed-not-game'
 if a.mode=='resource-bake':
  with tarfile.open(a.output/'native-linear-evidence.tar')as tar:
   allowed={'native-linear-matrix.log','resource4','resource4/resource-input.bin','resource4/resource-source.json','resource4/native-resource.bin','resource4/native-resource.json','resource4/native-resource-layout.bin'}
   for member in tar.getmembers():
    name=member.name.rstrip('/');require(name in allowed and not(member.issym()or member.islnk())and(member.isfile()or member.isdir()),'unexpected resource evidence member')
    if member.isfile()and name.startswith('resource4/'):
     dst=a.output/name;dst.parent.mkdir(exist_ok=True);dst.write_bytes(tar.extractfile(member).read())
  metadata=json.loads((a.output/'resource4/native-resource.json').read_text());require(metadata['complete']and metadata['driverID']==18 and metadata['layout_UUID']==LAYOUT and metadata['producer_source_sha256']==m['producer_source_sha256'],'new actual producer identity')
  require(metadata['recipe']['VkFormat']==158 and metadata['recipe']['width']==metadata['recipe']['height']==256 and metadata['recipe']['mipLevels']==9 and metadata['physical_image_bytes']==94208,'real firepit query')
  require(metadata['original_driver_roundtrip_compressed_blocks_exact']and not metadata['GPU_sampling_executed'],'producer proof boundary')
  result['actual_resource_query']={'path':str(a.output/'resource4/native-resource.json'),'sha256':sha(a.output/'resource4/native-resource.json')};result['baked_outputs']={n:sha(a.output/'resource4'/n)for n in ['resource-input.bin','resource-source.json','native-resource.bin','native-resource.json','native-resource-layout.bin']};result['stage']='actual-native-ASTC-fully-linear-real-firepit-producer-passed-not-NVN'
except BaseException as e:result.update(stage='failed-preserved',error=repr(e))
finally:
 try:
  if launch is not None and launch.poll()is None:launch.terminate();launch.wait(timeout=5)
  if installation_requested and not installed:installed=exact_fixture()
  if installed:
   stop_owned();require(not process(PKG),'fixture not stopped');result['fixture_PID_absent']=True;require(exact_fixture(),'owned APK changed');ad('uninstall',PKG);require(not ad('shell','pm','path',PKG,check=False),'fixture uninstall');result['fixture_uninstalled']=True
  if made_tmp:
   require(ad('shell','sha256sum',tmp+'/owner').split()[0]==hashlib.sha256((token+'\n').encode()).hexdigest(),'tmp ownership changed');names=ad('shell','find',tmp,'-type','f').splitlines();require(set(names)<={tmp+'/owner',tmp+'/libvulkan_freedreno.so',*(tmp+'/'+n for n in resource_hashes)},'unowned temporary file');require(not ad('shell','find',tmp,'-type','l'),'tmp symlink');require(ad('shell','find',tmp,'-type','d').splitlines()==[tmp],'unowned tmp subdirectory');
   for name in names:
    h=ad('shell','sha256sum',name).split()[0];require(h==(resource_hashes[Path(name).name]if Path(name).name in resource_hashes else DRIVER if name.endswith('.so')else hashlib.sha256((token+'\n').encode()).hexdigest()),'temporary bytes changed');ad('shell',shlex.join(['sh','-c','test "$(sha256sum "$1" | cut -d" " -f1)" = "$2" && rm "$1"','sh',name,h]));
   ad('shell','rmdir',tmp);ad('shell','test','!','-e',tmp);result['fixture_tmp_absent']=True
  if original_before is not None:result['original_after']=snapshot();require(result['original_after']==original_before,'original changed');result['original_unchanged']=True
 except BaseException as e:result['cleanup_errors'].append(repr(e))
 save()
print(json.dumps(result));raise SystemExit(0 if result['stage']in('actual-native-ASTC-fully-linear-API-matrix-passed-not-game','actual-native-ASTC-fully-linear-real-firepit-producer-passed-not-NVN')and not result['cleanup_errors']else 1)
