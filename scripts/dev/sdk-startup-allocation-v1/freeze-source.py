#!/usr/bin/env python3
"""Freeze tools-only early capture and its existing official tool dependencies."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
R=Path('/home/dl/d2r-mobile');HERE=Path(__file__).resolve().parent
FILES=('run-debugger.py','sdk_startup_allocations.py','sdk_free_resident.py','correlate.py')
TOOLS=(R/'.local/tools/lldb19/usr/lib/llvm-19/bin/lldb',R/'.local/tools/lldb19/usr/lib/x86_64-linux-gnu/liblldb-19.so.1',
 R/'.local/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/19/lib/linux/aarch64/lldb-server',
 R/'.local/android-sdk/ndk/28.2.13676358/prebuilt/linux-x86_64/bin/ndkgdb.pyz',R/'.local/android-sdk/platform-tools/adb',
 Path('/usr/lib/jvm/java-21-openjdk-amd64/bin/jdb'),Path('/usr/lib/jvm/java-21-openjdk-amd64/bin/java'),
 Path('/usr/lib/jvm/java-21-openjdk-amd64/lib/modules'),Path(sys.executable).resolve())
def rec(p):
 p=Path(p).resolve()
 with p.open('rb') as f:d=hashlib.file_digest(f,'sha256').hexdigest()
 return dict(path=str(p),sha256=d,bytes=p.stat().st_size)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 source=Path(subprocess.check_output(['git','-C',str(HERE),'rev-parse','--show-toplevel'],text=True).strip())
 commit=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
 if subprocess.check_output(['git','-C',str(source),'status','--porcelain']):p.error('freeze only clean committed source')
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);files={};sources={}
 for name in FILES:
  f=HERE/name;b=subprocess.check_output(['git','-C',str(source),'show',commit+':'+str(f.relative_to(source))])
  if hashlib.sha256(b).hexdigest()!=rec(f)['sha256']:raise RuntimeError('tracked source byte mismatch '+name)
  if name=='sdk_free_resident.py' and rec(f)['sha256']!='82ab628b0fad3f298c0a334ae911abb0eafafd686fd6ff37c8cb8da83dee6817':raise RuntimeError('original44c decoder changed')
  shutil.copy2(f,out/name);sources[name]=rec(f);files[name]=rec(out/name)
 j=dict(schema=1,source_pinned=True,source_commit=commit,source_root=str(source),source_files=sources,files=files,
        tools=[rec(q) for q in TOOLS],freeze_producer=rec(__file__),
        scope='Official LLDB original SDK allocation return provenance, bounded conditional software traps; no CPU profile/no inferior calls/no memory writes beyond official breakpoints',
        provenance={'runner_base':'9d8ea6f751e5b1eb6aebcec9ee9af43b70121e6f','owned_cleanup':'44c1d4e6982a06d4a7c87fb937e2b30ee4484e60',
                    'SDK_decoder_unchanged':True,'NDK_JDWP':'Original NDK28 start_jdb: SocketAttach, readiness print token, 0.3s settling delay, exit'},
        controller_source={str(q.relative_to(source)):rec(q) for q in (source/'scripts/dev/native-bulk-game-v1/run-game.py',source/'scripts/dev/android-game-launch.py')})
 (out/'tool-inputs.json').write_text(json.dumps(j,indent=2)+'\n');print(json.dumps(rec(out/'tool-inputs.json')))
if __name__=='__main__':main()
