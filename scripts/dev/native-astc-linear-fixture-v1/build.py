#!/usr/bin/env python3
"""Offline finite producer GPU oracle APK build. No ADB, game or GPU execution."""
import argparse,hashlib,json,os,shutil,subprocess,zipfile
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];HOST=Path('/home/dl/d2r-mobile')
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def record(p):p=Path(p).resolve();return dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size)
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--hook-apk',type=Path,default=HOST/'.local/build/native-image-only-app-0a54ef2-canonical-source/canonical.apk');a=p.parse_args();assert not a.output.exists();a.output.mkdir(parents=True);out=a.output.resolve();ndk=HOST/'.local/android-sdk/ndk/28.2.13676358';SDK=HOST/'.local/android-sdk';tools=SDK/'build-tools/36.0.0';jar=SDK/'platforms/android-36/android.jar';commands=[]
def run(args,name):
 cmd=list(map(str,args));commands.append(cmd)
 with(out/name).open('w')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
assert sha(a.hook_apk)=='19b70f6d85c34dcd0859f931460d3e8175f86d1bfb829a639e5d3e79d6075e3e'
run(['cmake','-S',HERE,'-B',out/'native','-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-29','-DCMAKE_BUILD_TYPE=RelWithDebInfo'],'configure.txt')
run(['cmake','--build',out/'native','--parallel','2'],'build.txt')
classes=out/'classes';dex=out/'dex';classes.mkdir();dex.mkdir()
run(['javac','--release','8','-classpath',jar,'-d',classes,*sorted((HERE/'java').rglob('*.java'))],'javac.txt')
run([tools/'d8','--lib',jar,'--min-api','29','--output',dex,*sorted(classes.rglob('*.class'))],'d8.txt')
unsigned=out/'unsigned.apk';aligned=out/'aligned.apk';apk=out/'candidate.apk'
run([tools/'aapt2','link','-I',jar,'--manifest',HERE/'AndroidManifest.xml','-o',unsigned],'aapt2.txt')
libs={};matrix=out/'native/libnative_astc_linear_matrix.so';libs[matrix.name]=sha(matrix)
with zipfile.ZipFile(unsigned,'a',compression=zipfile.ZIP_DEFLATED)as z,zipfile.ZipFile(a.hook_apk)as hooks:
 z.write(dex/'classes.dex','classes.dex');z.write(matrix,'lib/arm64-v8a/'+matrix.name)
 for n in ['libadrenotools.so','libfile_redirect_hook.so','libgsl_alloc_hook.so','libhook_impl.so','libmain_hook.so']:
  data=hooks.read('lib/arm64-v8a/'+n);libs[n]=hashlib.sha256(data).hexdigest();z.writestr('lib/arm64-v8a/'+n,data)
run([tools/'zipalign','-f','-P','16','4',unsigned,aligned],'zipalign.txt')
key=HOST/'.local/build/android-apk/debug.keystore'
run([tools/'apksigner','sign','--ks',key,'--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',apk,aligned],'sign.txt')
run([tools/'apksigner','verify','--verbose','--print-certs',apk],'signature.txt');run([tools/'zipalign','-c','-P','16','4',apk],'alignment.txt')
with zipfile.ZipFile(apk)as z:
 assert all(hashlib.sha256(z.read('lib/arm64-v8a/'+n)).hexdigest()==h for n,h in libs.items())
 assert not any(n.endswith('.nso')for n in z.namelist());assert len([n for n in z.namelist()if n.endswith('.so')])==6
sources=[p for p in HERE.rglob('*')if p.is_file()and '__pycache__'not in p.parts]
sources += [ROOT/'scripts/dev/native-image-payload'/n for n in ['fixture.c','native_import.inc','resource_bake.inc','resource_batch.inc','resource_format.h','sample.comp','native_linear_roundtrip.inc']]
report={'status':'OFFLINE_GPU_ORACLE_APK_BUILT_NOT_DEVICE_ACCEPTED','package':'com.d2rmobile.nativeastclinearfixture','activity':'com.d2rmobile.nativeastclinearfixture.MainActivity','APK':record(apk),'native_libraries':libs,'hook_APK':record(a.hook_apk),'source_revision':subprocess.check_output(['git','-C',ROOT,'rev-parse','HEAD'],text=True).strip(),'sources':{str(x.relative_to(ROOT)):record(x)for x in sources},'commands':commands,'source_shapes':[[1,1,1],[5,7,3],[63,65,7],[64,64,7],[95,97,7],[128,128,8],[128,256,9],[256,256,9]],'formats':[157,158,165,166],'expected_driver_SHA256':'f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce','producer_source_sha256':(out/'native/producer-source-sha256.txt').read_text().strip(),'expected_actual_layout_UUID':'ab91c540847448360a44496b50045159','contains_game_binary':False,'GPU_used':False,'deploy_allowed':False};(out/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['APK']))
