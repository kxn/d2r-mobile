#!/usr/bin/env python3
"""Only repackage six canonical original022 source-built SOs in an owned test APK.
No original game Activity/DEX/assets/modules, runtime patch, measurement or driver change.
"""
import argparse,hashlib,json,subprocess,zipfile,shutil
from pathlib import Path
P=argparse.ArgumentParser();P.add_argument('--base-apk',type=Path,required=True);P.add_argument('--fixture',type=Path,required=True);P.add_argument('--inputs',type=Path,required=True);P.add_argument('--driver',type=Path,required=True);P.add_argument('--output',type=Path,required=True);A=P.parse_args()
R=Path(__file__).resolve().parents[2];SDK=R/'.local/android-sdk';T=SDK/'build-tools/36.0.0';JAR=SDK/'platforms/android-36/android.jar';SRC=R/'dm/android/tests/panvk-bc3';PKG='com.d2rmobile.panvkbc3fixture'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();digest=lambda b:hashlib.sha256(b).hexdigest()
assert not subprocess.check_output(['git','-C',R,'status','--porcelain','--untracked-files=no'],text=True),'Freeze source first'
revision=subprocess.check_output(['git','-C',R,'rev-parse','HEAD'],text=True).strip();m=json.loads(A.base_apk.with_suffix('.apk.build.json').read_text())
assert m['panvk_bc3_fixture_compiled'] and m['tlsdesc'] and not m['contains_game_binary']and m['native_jobs']==2
assert sha(A.base_apk)==m['apk_sha256']and len(m['native_library_sha256'])==6
for n,h in m['source_inputs'].items():assert sha(R/n)==h,n
DRIVER='4e7b29e71cb4b205b55929f58c88e3d6521b19c02e0c0a2372b0672d5648a9e0';assert sha(A.driver)==DRIVER
DATA='731917e28cd937c095c972dc0f0b65a98fcefb64f2d278cd8b67a4805b23d1ab';assert sha(A.inputs/'bc3-capture.bin')==DATA
inputs=json.loads((A.inputs/'manifest.json').read_text());assert inputs['dataset_sha256']==DATA and inputs['regions']==52 and len(inputs['images'])==7 and inputs['payload_bytes']==371920
assert sha(R/'scripts/dev/prepare-panvk-bc3-capture-fixture.py')==inputs['producer_sha256']
assert not A.output.exists();A.output.mkdir(parents=True);commands=[]
def run(*args):
 c=list(map(str,args));commands.append(c);return subprocess.check_output(c,text=True,stderr=subprocess.STDOUT)
classes=A.output/'classes';dex=A.output/'dex';classes.mkdir();dex.mkdir()
run('javac','--release','8','-classpath',JAR,'-d',classes,*sorted(SRC.rglob('*.java')))
run(T/'d8','--lib',JAR,'--min-api','29','--output',dex,*sorted(classes.rglob('*.class')))
unsigned=A.output/'unsigned.apk';aligned=A.output/'aligned.apk';apk=A.output/'candidate.apk'
run(T/'aapt2','link','-I',JAR,'--manifest',SRC/'AndroidManifest.xml','-o',unsigned)
with zipfile.ZipFile(A.base_apk)as z,zipfile.ZipFile(unsigned,'a',compression=zipfile.ZIP_DEFLATED)as out:
 elf=z.read('lib/arm64-v8a/libdmhost_android.so')
 assert b'Java_com_d2rmobile_panvkbc3fixture_MainActivity_nativeRun'in elf and b'original022-BC3-capture-focused'in elf
 out.write(dex/'classes.dex','classes.dex')
 for n,h in m['native_library_sha256'].items():
  b=z.read('lib/arm64-v8a/'+n);assert digest(b)==h;out.writestr('lib/arm64-v8a/'+n,b)
 out.write(R/'dm/third_party/opus/COPYING','assets/licenses/opus-1.5.2.txt')
xml=run(T/'aapt2','dump','xmltree',unsigned,'--file','AndroidManifest.xml');(A.output/'manifest.xmltree.txt').write_text(xml)
assert PKG in xml and PKG+'.MainActivity'in xml and 'com.d2rmobile.nativeport'not in xml
run(T/'zipalign','-f','-P','16','4',unsigned,aligned)
run(T/'apksigner','sign','--ks',R/'.local/build/android-apk/debug.keystore','--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',apk,aligned)
(A.output/'signature.txt').write_text(run(T/'apksigner','verify','--verbose','--print-certs',apk));run(T/'zipalign','-c','-P','16','4',apk)
with zipfile.ZipFile(apk)as z:
 assert all(digest(z.read('lib/arm64-v8a/'+n))==h for n,h in m['native_library_sha256'].items())
 allowed={'AndroidManifest.xml','classes.dex','resources.arsc','assets/licenses/opus-1.5.2.txt',*('lib/arm64-v8a/'+n for n in m['native_library_sha256'])}
 assert all(n.startswith('META-INF/')or n in allowed for n in z.namelist())
shaders={};(A.output/'vulkan-fixture').mkdir()
for key in ['e5888d20bd2a77b9','c113a6c86bb7e554']:
 for ext in ['meta','spv']:
  p=A.fixture/(key+'.'+ext);assert p.is_file();shaders[p.name]=sha(p);shutil.copy2(p,A.output/'vulkan-fixture'/p.name)
shutil.copy2(A.inputs/'bc3-capture.bin',A.output/'bc3-capture.bin');shutil.copy2(A.inputs/'manifest.json',A.output/'dataset-source.json')
(A.output/'driver').mkdir();shutil.copy2(A.driver,A.output/'driver/libvulkan_panfrost.so')
result={'source_revision':revision,'package':PKG,'activity':PKG+'.MainActivity','apk_sha256':sha(apk),'base_apk_sha256':sha(A.base_apk),'base_manifest_sha256':sha(A.base_apk.with_suffix('.apk.build.json')),'native_library_sha256':m['native_library_sha256'],'driver_sha256':DRIVER,'dataset_sha256':DATA,'dataset_source_sha256':sha(A.output/'dataset-source.json'),'shader_files':shaders,'source_inputs':{str(p.relative_to(R)):sha(p)for p in [Path(__file__).resolve(),R/'scripts/dev/prepare-panvk-bc3-capture-fixture.py',*sorted(SRC.rglob('*'))]if p.is_file()},'build_commands':commands,'contains_game_binary':False,'original_app_DEX_assets_packaged':False,'shared_heap':False,'TRACE':False,'external_profile_disables_custom_timestamps':True,'installed':False,'GPU_acceptance':False,'scope':'Existing original022 NVN functional subset:52 known captured BC3 uploads/7images,all371731 FS pixels and371920 raw BC bytes; no v7 object/lifetime claim'}
apk.with_suffix('.apk.build.json').write_text(json.dumps(result,indent=2)+'\n');print(apk)
