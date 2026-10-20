#!/usr/bin/env python3
"""Compile/run owned memory-SVC tests; cross-build ARM64 without executing it."""
import argparse, hashlib, json, os, pathlib, shutil, subprocess
P=argparse.ArgumentParser();P.add_argument('--output',type=pathlib.Path,required=True);P.add_argument('--ndk',type=pathlib.Path);a=P.parse_args()
root=pathlib.Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
def record(p):
 p=pathlib.Path(p);return {'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
commands=[]
def run(cmd,name):
 commands.append(cmd)
 with (out/name).open('w') as f:subprocess.run(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT,check=True)
source=root/'dm/host/tests/guest_heap/backing.c'
flags=['-std=gnu11','-O2','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-format','-I'+str(root/'dm/host')]
run([shutil.which('cc'),*flags,str(source),'-pthread','-o',str(out/'backing-test')],'host-build.log')
# Expected SIGSEGV/SIGABRT children are byte/protection/failure assertions, not
# crash evidence from a game. Avoid core dump artifacts in this owned process.
import resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
run([str(out/'backing-test')],'host.log')
artifacts=[record(out/'backing-test'),record(out/'host.log')]
if a.ndk:
 clang=a.ndk.resolve()/'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang'
 ndkflags=[f if f!='-O2' else '-O3' for f in flags]
 run([str(clang),*ndkflags,str(source),'-pthread','-Wl,-z,max-page-size=16384','-o',str(out/'backing-arm64')],'android-build.log')
 artifacts.append(record(out/'backing-arm64'))
inputs=[source,root/'dm/host/hle_svc.c',root/'dm/host/guest_heap_backing.inc',root/'dm/host/guest_heap_startup.h',root/'dm/host/dm_guest_heap.h',pathlib.Path(__file__).resolve()]
report={'status':'passed','commands':commands,'inputs':list(map(record,inputs)),'artifacts':artifacts,'scope':'Owned host real memory SVC/VM calls only. ARM artifact cross-built, not executed. No Android/game/GPU or PFN/COW proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
