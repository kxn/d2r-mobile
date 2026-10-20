#!/usr/bin/env python3
"""Repackage a source-closed official VVL private-driver probe APK as an independent ordinary-UID oracle."""
import argparse,hashlib,json,re,shutil,subprocess,zipfile
from pathlib import Path
P=argparse.ArgumentParser()
P.add_argument('--base-apk',type=Path,required=True)
P.add_argument('--output',type=Path,required=True)
P.add_argument('--fixture',type=Path,required=True)
P.add_argument('--probe-source-ready',type=Path,required=True)
P.add_argument('--probe-source-revision',required=True)
A=P.parse_args();ROOT=Path(__file__).resolve().parents[2]
SDK=ROOT/'.local/android-sdk';TOOLS=SDK/'build-tools/36.0.0'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
sha_bytes=lambda data:hashlib.sha256(data).hexdigest()
base_manifest=A.base_apk.with_suffix('.apk.build.json')
m=json.loads(base_manifest.read_text())
assert m['fixed_shared_pools_compiled'] is True and m['contains_game_binary'] is False
layer=m['official_validation_layer'];assert layer and sha(ROOT/layer['path'])==layer['sha256']
all_libs={**m['native_library_sha256'],'libVkLayer_khronos_validation.so':layer['sha256']}
assert sha(A.base_apk)==m['apk_sha256'] and len(m['native_library_sha256'])==6
# Reject source/binary mismatches before packaging. Canonical build records all
# APK producer inputs, including Java and the actual NC GPU fixture.
for name,digest in m['source_inputs'].items():
 path=Path(name);path=path if path.is_absolute() else ROOT/path
 assert path.is_file() and sha(path)==digest,str(path)
assert re.fullmatch('[0-9a-f]{40}',A.probe_source_revision)
subprocess.run(['git','-C',str(ROOT),'merge-base','--is-ancestor',A.probe_source_revision,'HEAD'],check=True)
ready=json.loads(A.probe_source_ready.read_text());assert isinstance(ready,dict) and ready
with zipfile.ZipFile(A.base_apk) as z:
 for name,digest in m['native_library_sha256'].items():
  assert sha_bytes(z.read('lib/arm64-v8a/'+name))==digest
A.output.mkdir(parents=True,exist_ok=False)
commands=[]
def run(*args):
 command=list(map(str,args));commands.append(command)
 return subprocess.check_output(command,text=True,stderr=subprocess.STDOUT)
# Only manifest identity/resources.arsc and signatures may change. Game/driver
# sources and all six runtime libraries plus the pinned official VVL are copied byte-for-byte.
omitted=set()
unsigned=A.output/'unsigned.apk';aligned=A.output/'aligned.apk';apk=A.output/'candidate.apk'
resource_zip=A.base_apk.parent/'resources.zip';assert resource_zip.is_file()
run(TOOLS/'aapt2','link','-I',SDK/'platforms/android-36/android.jar','--manifest',
 ROOT/'dm/android/AndroidManifest.xml','--rename-manifest-package','com.d2rmobile.panvklayervvlfixture',
 '-o',unsigned,resource_zip)
with zipfile.ZipFile(A.base_apk) as source,zipfile.ZipFile(unsigned,'a',compression=zipfile.ZIP_DEFLATED) as target:
 present=set(target.namelist())
 for item in source.infolist():
  if item.filename in present or item.filename.startswith('META-INF/') or item.filename in omitted:continue
  target.writestr(item,source.read(item.filename))
xml=run(TOOLS/'aapt2','dump','xmltree',unsigned,'--file','AndroidManifest.xml')
(A.output/'AndroidManifest.xmltree.txt').write_text(xml)
assert 'com.d2rmobile.panvklayervvlfixture' in xml and 'com.d2rmobile.nativeport.MainActivity' in xml
assert '".MainActivity"' not in xml
run(TOOLS/'zipalign','-f','-P','16','4',unsigned,aligned)
run(TOOLS/'apksigner','sign','--ks',ROOT/'.local/build/android-apk/debug.keystore',
 '--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',apk,aligned)
verified=run(TOOLS/'apksigner','verify','--verbose','--print-certs',apk)
(A.output/'apksigner.txt').write_text(verified)
with zipfile.ZipFile(A.base_apk) as source,zipfile.ZipFile(apk) as target:
 same={item.filename:sha_bytes(target.read(item.filename)) for item in source.infolist()
       if item.filename not in ('AndroidManifest.xml','resources.arsc') and not item.filename.startswith('META-INF/') and item.filename not in omitted}
 for name,digest in same.items():assert sha_bytes(source.read(name))==digest,name
 for name,digest in all_libs.items():assert same['lib/arm64-v8a/'+name]==digest
 assert {n for n in target.namelist() if n.startswith('lib/arm64-v8a/') and n.endswith('.so')}=={'lib/arm64-v8a/'+n for n in all_libs}
fixture={}
(A.output/'vulkan-fixture').mkdir()
for item in sorted(A.fixture.iterdir()):
 assert item.is_file() and not item.is_symlink() and '/' not in item.name and item.name not in ('.','..')
 fixture[item.name]=sha(item);shutil.copy2(item,A.output/'vulkan-fixture'/item.name)
assert fixture
(A.output/'fixture-hashes.json').write_text(json.dumps(fixture,indent=2)+'\n')
shutil.copy2(A.probe_source_ready,A.output/'probe-source-ready.json')
result={'package':'com.d2rmobile.panvklayervvlfixture','activity':'com.d2rmobile.nativeport.MainActivity',
 'fixture_kind':'official-VVL-private-PanVK-entry-v1','source_revision':run('git','-C',ROOT,'rev-parse','HEAD').strip(),
 'probe_source_revision':A.probe_source_revision,'probe_source_ready_sha256':sha(A.probe_source_ready),
 'fixture_hashes_sha256':sha(A.output/'fixture-hashes.json'),'apk_sha256':sha(apk),
 'base_apk_sha256':sha(A.base_apk),'base_build_manifest_sha256':sha(base_manifest),
 'packager_sha256':sha(__file__),'native_library_sha256':all_libs,
 'official_validation_layer':layer,'optional_validation_layer_omitted_explicitly':[],
 'base_payload_entries_verified_identical':same,'contains_game_binary':False,
 'build_commands':commands,'installed':False}
apk.with_suffix('.apk.build.json').write_text(json.dumps(result,indent=2)+'\n')
print(apk)
