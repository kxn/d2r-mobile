#!/usr/bin/env python3
"""Build only debug loader glue; package existing official Arm Android replay."""
from pathlib import Path
import argparse,subprocess,hashlib,json,zipfile,shutil
R=Path('/home/dl/d2r-mobile');W=Path(__file__).resolve().parents[2];P=argparse.ArgumentParser();P.add_argument('--output',type=Path,required=True);a=P.parse_args();D=a.output.resolve();D.mkdir(parents=True,exist_ok=False)
SRC=W/'scripts/dev/experimental-panvk-host-import/gfxr-replay';SDK=R/'.local/android-sdk';T=SDK/'build-tools/36.0.0';NDK=SDK/'ndk/28.2.13676358';BIN=NDK/'toolchains/llvm/prebuilt/linux-x86_64/bin';V=R/'.local/tools/gfxreconstruct-source/external/Vulkan-Headers/include';ARM=R/'.local/tools/Arm_Performance_Studio_2026.5/frame_advisor/bin/android/replay-release.apk';BASE=R/'.local/worktrees/vulkan-color-depth-comparison-v1/.local/build/android-apk/d2r-native-tlsdesc-debug.apk'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();row=lambda p:{'path':str(p),'sha256':sha(p),'bytes':Path(p).stat().st_size};commands=[]
def run(*args):
 cmd=list(map(str,args));commands.append(cmd);return subprocess.check_output(cmd,stderr=subprocess.STDOUT,text=True)
assert sha(ARM)=='e44a9dea3eb2933051e9d1bafb1c1c6a476d980b910447b75988cca80ed8c127';assert sha(BASE)=='cc56a9849b28ec338a4a9a1941b9933cac2be689e31c116a0ea48ab175656adc'
(D/'libs').mkdir();(D/'classes').mkdir();(D/'dex').mkdir()
run('cc','-std=c11','-O2','-I'+str(V),SRC/'test_dependency.c','-o',D/'test_dependency');(D/'dependency-test.txt').write_text(run(D/'test_dependency'))
run(BIN/'aarch64-linux-android29-clang','-std=c11','-O2','-g','-MD','-MF',D/'bridge.d','-fPIC','-fvisibility=hidden','-shared','-Werror','-Wall','-Wextra','-Wno-unused-parameter','-Wl,--no-undefined','-I'+str(V),SRC/'replay_bridge.c','-o',D/'libs/libpanvk_replay_bridge.so','-ldl','-llog')
with zipfile.ZipFile(ARM)as z:(D/'libs/libgfxrecon-replay.so').write_bytes(z.read('lib/arm64-v8a/libgfxrecon-replay.so'))
with zipfile.ZipFile(BASE)as z:
 for n in ['libadrenotools.so','libfile_redirect_hook.so','libgsl_alloc_hook.so','libhook_impl.so','libmain_hook.so','libVkLayer_khronos_validation.so']:(D/'libs'/n).write_bytes(z.read('lib/arm64-v8a/'+n))
run('javac','-source','8','-target','8','-classpath',SDK/'platforms/android-36/android.jar','-d',D/'classes',SRC/'ReplayActivity.java')
classfile=D/'classes/com/d2rmobile/panvkgfxrfixture/ReplayActivity.class';run(T/'d8','--min-api','29','--lib',SDK/'platforms/android-36/android.jar','--output',D/'dex',classfile)
unsigned=D/'unsigned.apk';run(T/'aapt2','link','-I',SDK/'platforms/android-36/android.jar','--manifest',SRC/'AndroidManifest.xml','-o',unsigned)
with zipfile.ZipFile(unsigned,'a',compression=zipfile.ZIP_DEFLATED)as z:
 z.write(D/'dex/classes.dex','classes.dex')
 for p in sorted((D/'libs').glob('*.so')):z.write(p,'lib/arm64-v8a/'+p.name)
run(T/'zipalign','-f','-P','16','4',unsigned,D/'aligned.apk');run(T/'apksigner','sign','--ks',R/'.local/build/android-apk/debug.keystore','--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',D/'candidate.apk',D/'aligned.apk')
(D/'apksigner.txt').write_text(run(T/'apksigner','verify','--verbose','--print-certs',D/'candidate.apk'));xml=run(T/'aapt2','dump','xmltree',D/'candidate.apk','--file','AndroidManifest.xml');(D/'manifest.txt').write_text(xml);assert 'com.d2rmobile.panvkgfxrfixture.ReplayActivity'in xml and 'com.d2rmobile.nativeport'not in xml
libs={p.name:sha(p)for p in sorted((D/'libs').glob('*.so'))};assert len(libs)==8
with zipfile.ZipFile(D/'candidate.apk')as z:assert {Path(n).name:hashlib.sha256(z.read(n)).hexdigest()for n in z.namelist()if n.startswith('lib/arm64-v8a/')}==libs
deps=(D/'bridge.d').read_text().replace('\\\n',' ').split(': ',1)[1].split(); dependency_rows=[row(Path(n))for n in sorted(set(deps))]; tool_rows=[row(BIN/'clang'),row(T/'aapt2'),row(T/'d8'),row(T/'apksigner'),row(T/'zipalign'),row(SDK/'platforms/android-36/android.jar')]
report={'actual_compile_inputs':dependency_rows,'actual_tools':tool_rows,'status':'built debug-only official-replay ordinaryUID APK; not executed','package':'com.d2rmobile.panvkgfxrfixture','activity':'com.d2rmobile.panvkgfxrfixture.ReplayActivity','APK':row(D/'candidate.apk'),'native_libraries8':libs,'official_SDK_APK':row(ARM),'original_c613_hooks_VVL_APK':row(BASE),'sources':[row(p)for p in sorted(SRC.iterdir())if p.is_file()]+[row(Path(__file__))],'commands':commands,'SDK_replay_native_unchanged':True,'game_modules_or_renderer_included':False,'loader_semantics':'Existing DRIVER_CUSTOM standard Android loader, noGPU_MAPPING_IMPORT; exactprivateinode+ID20 or fail, never fallback; one required extension dependency only; no recordedpNext/feature edits.'}
(D/'candidate.apk.build.json').write_text(json.dumps(report,indent=2)+'\n');print(D/'candidate.apk')
