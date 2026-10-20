from pathlib import Path
import subprocess,hashlib,json,re,shutil,datetime
B=Path(__file__).resolve().parent;R=B.parent.parent.parent;S=B/'source';T=B/'android-bionic';OLD=B.parent/'panvk-minegraph-703db0e-native-hardware-v2'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def rec(p):p=Path(p).resolve();return {'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size}
def run(*a):return subprocess.check_output([str(v)for v in a],text=True)
p=R/'.local/logs/panvk-minegraph-703db0e-static-hardware-v2-source-ready-agent.json';assert sha(p)=='edcbcc0f465f712c889e51daf64b767b6b136699a47f5c464d78e048d62b9930';d=json.loads(p.read_text());identity=json.loads((B/'source-identity-v2.json').read_text());old=json.loads(Path(d['source_identity']['path']).read_text());changed=[]
for row,prev in zip(identity['files'],old['files'],strict=True):
 assert row['path']==prev['path'];f=S/row['path'];a=row['actual'];assert (f.is_symlink()and str(f.readlink())==a['target'])if a['kind']=='symlink'else(f.is_file()and not f.is_symlink()and sha(f)==a['sha256'])
 if row!=prev:changed.append(row['path'])
assert changed==['src/panfrost/genxml/decode_common.c'];assert identity['tracked_entries']==len(identity['files'])==13512
for row in old['files']:
 f=OLD/'source'/row['path'];a=row['actual'];assert (f.is_symlink()and str(f.readlink())==a['target'])if a['kind']=='symlink'else(f.is_file()and sha(f)==a['sha256'])
assert sha(d['driver']['path'])==d['driver']['sha256']=='df9ba8326b353812bbe36b3306f9ce6c833764c2538e85cacf91a12ccd9566a9'
assert (T/'src/git_sha1.h').read_text()=='#define MESA_GIT_SHA1 " (git-703db0ebb9)"'
raw=run('ninja','-C',T,'-t','deps');(B/'android-compiler-deps.txt').write_text(raw);paths=set()
for line in raw.splitlines():
 if line.startswith('    '):
  x=Path(line.strip());paths.add((x if x.is_absolute()else T/x).resolve())
missing=[str(f)for f in sorted(paths)if not f.is_file()];assert not missing,missing
closure={'schema':1,'actual_ninja_dependency_count':len(paths),'missing':missing,'files':[rec(f)for f in sorted(paths)]};(B/'android-compiler-dependency-closure.json').write_text(json.dumps(closure,indent=2)+'\n')
for r in [d['host_codegen'],d['actual_compiler_dependencies']['host']]:assert sha(r['path'])==r['sha256']
for f in json.loads(Path(d['actual_compiler_dependencies']['host']['path']).read_text())['files']:assert sha(f['path'])==f['sha256']
for f in json.loads(Path(d['host_codegen']['path']).read_text())['tools']:assert sha(f['path'])==f['sha256']
llvm=R/'.local/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin';so=T/'src/panfrost/vulkan/libvulkan_panfrost.so';elf=run(llvm/'llvm-readelf','-h','-d','-n','--dyn-syms',so);(B/'ELF.txt').write_text(elf);needed=re.findall(r'\(NEEDED\).*?\[(.*?)\]',elf);assert needed==d['needed']and'AArch64'in elf
assert all(re.search(r'GLOBAL[^\n]*\b'+e+r'\b',elf)for e in d['ICD_exports_verified']);assert not re.search(r'UND[^\n]*hw_get_module',elf)
(B/'android-link-and-build-commands.txt').write_text(run('ninja','-C',T,'-t','commands'));(B/'dist').mkdir(exist_ok=True);shutil.copy2(so,B/'dist/libvulkan_panfrost.so')
report={**d,'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'OFFICIAL_PANDECODE_FILE_OUTPUT_RESTORED_DIAGNOSTIC_ONLY_NOT_DEVICE_ACCEPTED','source_root':str(S),'source_identity':rec(B/'source-identity-v2.json'),'build_only_adaptation':rec(B/'only-native-pandecode-file.patch'),'generated_version':rec(T/'src/git_sha1.h'),'actual_compiler_dependencies':{'host':d['actual_compiler_dependencies']['host'],'android':rec(B/'android-compiler-dependency-closure.json')},'driver':rec(B/'dist/libvulkan_panfrost.so'),'ELF':rec(B/'ELF.txt'),'build_id':re.findall(r'Build ID: (\w+)',elf),'needed':needed,'only_delta_from_full_v1_driver_source':['src/android_stub/meson.build','src/panfrost/genxml/decode_common.c'],'only_delta_from_static_hardware_v2_driver_source':changed,'parent_static_hardware_ready':rec(p),'parent_static_hardware_driver_unchanged':rec(d['driver']['path']),'diagnostic_only':True,'diagnostic_semantics':'Restores only pre-existing commented native Mesa filename/suffix/fopen path and error log. stderr branch and failure fallback remain. No rendering/BC/CSF/sync or game changes. Runtime PANVK_DEBUG=dump changes mapping/cache and decoder mprotect behavior; never a performance or uninstrumented stability result.','host_open_check':[rec(B/n)for n in ['test-native-pandecode-file.c','test-native-pandecode-file','host-open-check.txt']],'build_evidence':{n:rec(B/n)for n in ['setup-argv.json','android-setup-console.txt','android-build-console.txt','android-link-and-build-commands.txt','freeze-build-v2.py']},'new_driver_actual_loading_pending':True,'device_access_performed':False,'actual_GPU_execution_accepted':False,'real_game_accepted':False,'performance_accepted':False,'production_deploy_allowed':False}
report['target']={**d['target'],'meson_buildoptions':rec(T/'meson-info/intro-buildoptions.json'),'cross_file':rec(B/'android-native-api30.cross.ini')}
p=R/'.local/logs/panvk-minegraph-pandecode-file-v1-source-ready-agent-v2.json';p.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(rec(p)));print(json.dumps(report['driver']))
