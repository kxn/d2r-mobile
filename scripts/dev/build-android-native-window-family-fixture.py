#!/usr/bin/env python3
"""Package the original six source-closed SOs with ONE owned test-only Activity.

No game modules, assets, original MainActivity, renderer or measurement code.
The original shared-window/NVN oracle remains linked unchanged in the host SO.
"""
import argparse,hashlib,json,re,shutil,subprocess,zipfile
from pathlib import Path
P=argparse.ArgumentParser()
P.add_argument('--base-apk',type=Path,required=True)
P.add_argument('--output',type=Path,required=True)
P.add_argument('--fixture',type=Path,required=True)
P.add_argument('--runtime-source-ready',type=Path,required=True)
P.add_argument('--driver',type=Path,required=True)
P.add_argument('--resources',type=Path,required=True)
A=P.parse_args();ROOT=Path(__file__).resolve().parents[2]
SDK=ROOT/'.local/android-sdk';TOOLS=SDK/'build-tools/36.0.0';JAR=SDK/'platforms/android-36/android.jar'
PKG='com.d2rmobile.nativewindowfamilyfixture';SOURCE=ROOT/'dm/android/tests/native-window-family'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
sha_bytes=lambda b:hashlib.sha256(b).hexdigest()
DRIVER='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'
assert sha(A.driver)==DRIVER,'Requires the actual validated AA185 native image driver'
manifest=A.base_apk.with_suffix('.apk.build.json');m=json.loads(manifest.read_text())
assert m['contains_game_binary'] is False and m['tlsdesc'] is True
for k in ('fixed_shared_pools_compiled','shared_import_windows_compiled','native_window_app_fixture_compiled','native_resources_compiled'):assert m[k] is True,k
assert sha(A.base_apk)==m['apk_sha256'] and len(m['native_library_sha256'])==6
assert m['native_previous_writer_ASTC_oracle_compiled'] is True and m['native_image_only_ASTC_oracle_compiled'] is True
with zipfile.ZipFile(A.base_apk) as base_contents:
 assert all(marker in base_contents.read('lib/arm64-v8a/libdmhost_android.so') for marker in (b'native-previous-writer-ASTC-pending-FS',b'native-image-only-original-NVN')), 'Actual new ASTC body absent: old family-only ELF rejected'

for name,digest in m['source_inputs'].items():
 p=Path(name);p=p if p.is_absolute() else ROOT/p
 assert p.is_file() and sha(p)==digest,str(p)
ready=json.loads(A.runtime_source_ready.read_text());assert ready
assert subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain','--untracked-files=no'],text=True)=='','Freeze tracked source before canonical packaging'
revision=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
assert re.fullmatch('[0-9a-f]{40}',revision) and m['source_revision']==revision
with zipfile.ZipFile(A.base_apk) as z:
 for name,digest in m['native_library_sha256'].items():assert sha_bytes(z.read('lib/arm64-v8a/'+name))==digest
assert not A.output.exists(),'Never overwrite an accepted archive'
A.output.mkdir(parents=True)
commands=[]
def run(*args):
 c=list(map(str,args));commands.append(c)
 return subprocess.check_output(c,text=True,stderr=subprocess.STDOUT)
classes=A.output/'classes';dex=A.output/'dex';classes.mkdir();dex.mkdir()
run('javac','--release','8','-classpath',JAR,'-d',classes,*sorted(SOURCE.rglob('*.java')))
run(TOOLS/'d8','--lib',JAR,'--min-api','29','--output',dex,*sorted(classes.rglob('*.class')))
unsigned=A.output/'unsigned.apk';aligned=A.output/'aligned.apk';apk=A.output/'candidate.apk'
run(TOOLS/'aapt2','link','-I',JAR,'--manifest',SOURCE/'AndroidManifest.xml','-o',unsigned)
with zipfile.ZipFile(A.base_apk) as source,zipfile.ZipFile(unsigned,'a',compression=zipfile.ZIP_DEFLATED) as target:
 target.write(dex/'classes.dex','classes.dex')
 for name in m['native_library_sha256']:
  leaf='lib/arm64-v8a/'+name;target.writestr(leaf,source.read(leaf))
 # Original pinned dependency license only; no original app assets or DEX.
 target.write(ROOT/'dm/third_party/opus/COPYING','assets/licenses/opus-1.5.2.txt')
xml=run(TOOLS/'aapt2','dump','xmltree',unsigned,'--file','AndroidManifest.xml')
(A.output/'AndroidManifest.xmltree.txt').write_text(xml)
assert PKG in xml and PKG+'.MainActivity' in xml and 'com.d2rmobile.nativeport' not in xml
run(TOOLS/'zipalign','-f','-P','16','4',unsigned,aligned)
run(TOOLS/'apksigner','sign','--ks',ROOT/'.local/build/android-apk/debug.keystore','--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',apk,aligned)
(A.output/'apksigner.txt').write_text(run(TOOLS/'apksigner','verify','--verbose','--print-certs',apk))
run(TOOLS/'zipalign','-c','-P','16','4',apk)
with zipfile.ZipFile(apk) as z:
 for name,digest in m['native_library_sha256'].items():assert sha_bytes(z.read('lib/arm64-v8a/'+name))==digest
 expected={'AndroidManifest.xml','classes.dex','assets/licenses/opus-1.5.2.txt',*('lib/arm64-v8a/'+n for n in m['native_library_sha256'])}
 assert {n for n in z.namelist() if not n.startswith('META-INF/')} <= expected|{'resources.arsc'},z.namelist()
shaders={};(A.output/'vulkan-fixture').mkdir()
for item in sorted(A.fixture.iterdir()):
 assert item.is_file() and not item.is_symlink()
 shaders[item.name]=sha(item);shutil.copy2(item,A.output/'vulkan-fixture'/item.name)
assert shaders and any(n.endswith('.spv') for n in shaders)
(A.output/'fixture-hashes.json').write_text(json.dumps(shaders,indent=2)+'\n')
shutil.copy2(A.runtime_source_ready,A.output/'runtime-source-ready.json')
shutil.copy2(manifest,A.output/'base-apk.build.json')
resources={};(A.output/'resource4').mkdir()
required={'native-layouts.bin':'d61c38b5bcbac469f9a111e6b239fada4b7fb2c5b86f33aec3810661215b8e55',
 'native-resource.bin':'de206d467fc9dd74de9e957e432cf2470512dc6825caceff40d89a7260e5e418',
 'resource-input.bin':'d078631c7797b699200230a09533c9ad394a40fb9244fe0f75d905974ca49354'}
# Identity/source bytes are the prior accepted AA185 real firepit, no rebake.
for name,digest in required.items():
 src=A.resources/name
 assert src.is_file() and not src.is_symlink() and sha(src)==digest,name
 shutil.copy2(src,A.output/'resource4'/name);resources[name]=digest
assert (A.output/'resource4/native-resource.bin').stat().st_size==135168
table=(A.output/'resource4/native-layouts.bin').read_bytes()
assert table[64:96].hex()==DRIVER
pack=table[32:64].hex();producer=table[96:128].hex()
assert pack=='9fb5a9d015b60a9857620419ae4fd70a4704786398a2ebaaf0f4f60f8cd72144'
assert producer=='34ec895c3699da37f59b5af0a057ae0555934692917edd9bc62a775a9847a29f'
result={'owned_resource_inputs':resources,'native_resource_pack_sha256':pack,'native_resource_producer_sha256':producer,
 'native_payload_bytes':135168,'package':PKG,'activity':PKG+'.MainActivity','fixture_kind':'native-image-only-original-ASTC-family-v1','previous_writer_ASTC_fixture':True,'native_image_only_fixture':True,
 'source_revision':revision,'apk_sha256':sha(apk),'base_apk_sha256':sha(A.base_apk),
 'base_build_manifest_sha256':sha(manifest),'runtime_source_ready_sha256':sha(A.runtime_source_ready),
 'native_library_sha256':m['native_library_sha256'],'shader_files':shaders,
 'fixture_hashes_sha256':sha(A.output/'fixture-hashes.json'),'driver_sha256':DRIVER,
 'source_inputs':{str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__).resolve(),*sorted(SOURCE.rglob('*'))] if p.is_file()},
 'build_commands':commands,'contains_game_binary':False,'original_game_classes_assets_packaged':False,
 'cold_mode':{'provider':'adreno','shared_heap':True,'shared_pool_windows':65536,'async':True,'defer_publication':False,'validation':'disabled-explicitly'},
 'requires_actual_ASTC_GPU':True,'installed':False,'performance_claim':False}
apk.with_suffix('.apk.build.json').write_text(json.dumps(result,indent=2)+'\n')
print(apk)
