#!/usr/bin/env python3
"""Freeze this thin official-debugger adapter and exact local tool inputs; no ADB."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

R=Path('/home/dl/d2r-mobile');HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
def row(p):return {'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
files={}
for name in ['run-debugger.py','capture_counters.py','test_guards.py','freeze-source.py','README.md']:
 p=out/name;shutil.copy2(HERE/name,p);files[name]=row(p)
tools=[R/'.local/tools/lldb19/usr/lib/llvm-19/bin/lldb',R/'.local/tools/lldb19/usr/lib/x86_64-linux-gnu/liblldb-19.so.1',R/'.local/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/19/lib/linux/aarch64/lldb-server',R/'.local/android-sdk/platform-tools/adb',Path(shutil.which('python3')).resolve()]
manifest={'schema':1,'scope':'Thin reuse of5264 officialowneddebugger; no runtimebuild/targetexecution/deviceacceptance','files':files,'tools':[row(p)for p in tools],'source_revision':subprocess.check_output(['git','-C',str(HERE),'rev-parse','HEAD'],text=True).strip(),'source_clean':not subprocess.check_output(['git','-C',str(HERE),'status','--porcelain'],text=True).strip()}
p=out/'tool-inputs.json';p.write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(row(p)))
