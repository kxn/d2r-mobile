#!/usr/bin/env python3
"""Read-only original-app identity verification for the existing fixture controller."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
if a.output.exists():raise ValueError('New report path required')
folder=ROOT/'.local/build/native-touch-x18-three-path-scalar-10d5e13';manifest=folder/'rewrite-manifest.json'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(manifest)=='85af76c16cfec2930217c80e211e04c01f960edf69029a6f8d47a498335a973f'
d=json.loads(manifest.read_text());expected={}
for group in [d['artifacts'],*[module['artifacts'] for module in d['modules'].values()]]:
 for name,row in group.items():
  assert re.fullmatch('[A-Za-z0-9_.-]+',name) and name not in expected
  f=folder/name;assert f.stat().st_size==row['bytes'] and sha(f)==row['sha256'],name
  expected[name]=dict(row,path=str(f))
expected['rewrite-manifest.json']={'bytes':manifest.stat().st_size,'sha256':sha(manifest),'path':str(manifest)}
assert 28<=len(expected)<=64
B=[str(ROOT/'.local/android-sdk/platform-tools/adb'),'-s',a.serial];pkg='com.d2rmobile.nativeport'
def adb(*v):return subprocess.check_output(B+list(v),text=True,timeout=30).strip()
assert not adb('shell','pidof '+pkg+' || true'),'Original game running; preserve it'
remote=['files/runtime/modules/'+name for name in expected]
actual={line.split(None,1)[1].strip():line.split(None,1)[0] for line in adb('exec-out','run-as',pkg,'sha256sum',*remote).splitlines()}
assert actual=={'files/runtime/modules/'+name:row['sha256'] for name,row in expected.items()},'Original module hashes differ'
path=adb('shell','pm','path',pkg).removeprefix('package:');apk_sha=adb('shell','sha256sum',path).split()[0]
assert apk_sha=='39197b1cbc6cbde76b35ccb461b756c6c69e4e3bd4132d0f868024283837f8c3'
anchors=['files/runtime/launch-profile.properties','files/runtime/launch-profile-receipt.json','files/runtime/shaders/.chain','files/runtime/native-one-resources/native-layouts.bin']
anchor_sha={line.split(None,1)[1].strip():line.split(None,1)[0] for line in adb('exec-out','run-as',pkg,'sha256sum',*anchors).splitlines()}
assert anchor_sha[anchors[0]]=='4ccef600b589a77318881d357d97931ffec86a7f8056fa976f618c914ec04aeb'
assert anchor_sha[anchors[1]]=='5b28574aa1e00fa705a5603a9d723c7ab3bc6bc4ab9d7bf73434e3d1239be7d3'
chain=adb('exec-out','run-as',pkg,'cat','files/runtime/shaders/.chain')
receipt={'schema':1,'serial':a.serial,'package':pkg,'verified_on_device':True,'read_only':True,'canonical_directory':str(folder),'canonical_manifest_sha256':sha(manifest),'artifact_count':len(expected),'artifacts':expected,'actual_remote_sha256':actual,'APK_path':path,'APK_sha256':apk_sha,'anchors_sha256':anchor_sha,'shaderchain':chain,'game_PID_absent':True,'verifier_sha256':sha(Path(__file__))}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({k:v for k,v in receipt.items() if k not in ('artifacts','actual_remote_sha256')},indent=2))
