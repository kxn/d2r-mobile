#!/usr/bin/env python3
"""Build an isolated AA185 + native HOST ASTC fully-linear experiment. No ADB/GPU.
Requires the already frozen verified AA185 source bundle; does not fetch games,
install a driver, alter frozen sources, bake textures or validate hardware.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
HERE=Path(__file__).resolve().parent
ROOT=Path('/home/dl/d2r-mobile')
AA_READY_SHA='bcd937be017082ffd900030c37c64a7106701f49205e416a44cb4255c9ec0f7b'
PATCH_SHA='9b2248475036a802a0300dc39c4be79b9755f25c8c598f5fc32b0690474fef8a'
AA_DRIVER_SHA='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def rec(p):p=Path(p).resolve();return{'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size}
def require(v,msg):
 if not v:raise RuntimeError(msg)
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work-dir',type=Path,required=True);p.add_argument('--aa-ready',type=Path,default=ROOT/'.local/build/turnip-host-image-v1/source-ready-freeze/turnip-host-image-v1-source-ready.json');p.add_argument('--aa-source',type=Path);p.add_argument('--ndk',type=Path,default=ROOT/'.local/android-sdk/ndk/28.2.13676358');p.add_argument('--meson',default=str(ROOT/'.local/tools/adreno-native-research/venv/bin/meson'));p.add_argument('--build-tools-bin',type=Path,default=ROOT/'.local/tools/adreno-native-research/host-tools/usr/bin');p.add_argument('-j',type=int,default=2);a=p.parse_args();require(1<=a.j<=2,'use1or2jobs');require(not a.work_dir.exists(),'new independent output required');require(sha(a.aa_ready)==AA_READY_SHA,'AA ready identity differs');aa=json.loads(a.aa_ready.read_text());require(sha(aa['driver']['path'])==AA_DRIVER_SHA,'frozen AA driver changed');ledger_path=a.aa_ready.parent/'entire-mesa-source-ledger.json';require(sha(ledger_path)==aa['source_closure']['entire_source_ledger_sha256'],'AA ledger changed');ledger=json.loads(ledger_path.read_text());source=(a.aa_source or Path(aa['source_repo'])).resolve();patch=HERE/'native-host-astc-whole-linear-final.patch';require(sha(patch)==PATCH_SHA,'experiment patch changed')
for n,r in ledger.items():
 f=source/n;require((f.is_symlink()and str(f.readlink())==r['target'])if r['kind']=='symlink'else(f.is_file()and not f.is_symlink()and sha(f)==r['sha256']),'AA source changed:'+n)
require(sha(aa['build_manifest']['path'])==aa['build_manifest']['sha256'],'AA build manifest changed');base_build=json.loads(Path(aa['build_manifest']['path']).read_text());require('28.2.13676358'in(a.ndk/'source.properties').read_text(),'unexpected NDK');a.work_dir.mkdir(parents=True);out=a.work_dir.resolve();S=out/'mesa-26.2.5';shutil.copytree(source,S,symlinks=True,ignore=shutil.ignore_patterns('.git','__pycache__'))
with(out/'patch-console.txt').open('w')as f:subprocess.run(['patch','--batch','--forward','--no-backup-if-mismatch','-p1','-i',str(patch)],cwd=S,stdout=f,stderr=subprocess.STDOUT,check=True)
changed=[n for n,r in ledger.items()if r['kind']=='file'and sha(S/n)!=r['sha256']];require(sorted(changed)==['src/freedreno/vulkan/tu_device.cc','src/freedreno/vulkan/tu_image.cc'],'unexpected production source delta')
cross=Path(aa['build_manifest']['path']).parent.parent.parent/'android-aarch64.ini';require(cross.is_file()and sha(cross)==base_build['cross_file_sha256'],'AA cross file absent/changed');original_ndk=str(ROOT/'.local/android-sdk/ndk/28.2.13676358');text=cross.read_text().replace(original_ndk,str(a.ndk.resolve()));(out/'android-aarch64.ini').write_text(text)
env=os.environ.copy();env['PATH']=str(a.build_tools_bin.resolve())+':'+str(Path(a.meson).resolve().parent)+':'+env['PATH'];env['BISON_PKGDATADIR']=str(a.build_tools_bin.resolve().parent/'share/bison');env['M4']=str(a.build_tools_bin.resolve()/'m4');env['PYTHONDONTWRITEBYTECODE']='1';env['MESA_GIT_SHA1_OVERRIDE']=PATCH_SHA[:10]
cmd=[a.meson,'setup',str(out/'mesa-kgsl-build'),str(S),'--cross-file',str(out/'android-aarch64.ini'),*base_build['meson_flags']]
with(out/'setup-console.txt').open('w')as f:subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
with(out/'build-console.txt').open('w')as f:subprocess.run(['ninja','-C',str(out/'mesa-kgsl-build'),'-j'+str(a.j),'src/freedreno/vulkan/libvulkan_freedreno.so'],env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
(out/'driver').mkdir();shutil.copy2(out/'mesa-kgsl-build/src/freedreno/vulkan/libvulkan_freedreno.so',out/'driver/libvulkan_freedreno.so');report={'status':'EXPERIMENTAL_BUILD_ONLY_NO_DEVICE_ACCEPTANCE','AA_ready':rec(a.aa_ready),'AA_driver':AA_DRIVER_SHA,'experiment_patch':rec(patch),'builder':rec(Path(__file__)),'changed_production_files':changed,'setup_argv':cmd,'MESA_GIT_SHA1_OVERRIDE':PATCH_SHA[:10],'parallel_jobs':a.j,'driver':rec(out/'driver/libvulkan_freedreno.so'),'deploy_allowed':False,'GPU_used':False};(out/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['driver']))
