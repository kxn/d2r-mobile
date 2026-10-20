#!/usr/bin/env python3
"""Package an already built, byte-identical native fixture with a separate app ID."""
import argparse,hashlib,json,subprocess,zipfile,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
P=argparse.ArgumentParser();P.add_argument('--inputs',type=Path,required=True);A=P.parse_args()
OUT=ROOT/'.local/build/android-native-family-fixture'
SDK=ROOT/'.local/android-sdk';TOOLS=SDK/'build-tools/36.0.0'
BASE=ROOT/'.local/build/android-apk/d2r-native-tlsdesc-debug.apk'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads(BASE.with_suffix('.apk.build.json').read_text())
assert sha(BASE)==manifest['apk_sha256']
for name,value in manifest['native_library_sha256'].items():
    with zipfile.ZipFile(BASE) as z:assert hashlib.sha256(z.read('lib/arm64-v8a/'+name)).hexdigest()==value
OUT.mkdir(parents=True,exist_ok=True)
unsigned=OUT/'unsigned.apk';aligned=OUT/'aligned.apk';apk=OUT/'native-family-fixture.apk'
commands=[]
# Direct PanVK ICD cannot compose Android VVL; omit only that unused optional
# layer, recording its original bytes explicitly. Six host/loader libs stay exact.
omitted={"lib/arm64-v8a/libVkLayer_khronos_validation.so"}
def run(*args):
    args=list(map(str,args));commands.append(args);return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT)
# aapt2 keeps the original Java package-qualified Activity when renaming the
# manifest package. The compiled manifest below is verified before signing.
run(TOOLS/'aapt2','link','-I',SDK/'platforms/android-36/android.jar','--manifest',
    ROOT/'dm/android/AndroidManifest.xml','--rename-manifest-package','com.d2rmobile.nativefamilyfixture',
    '-o',unsigned,ROOT/'.local/build/android-apk/resources.zip')
with zipfile.ZipFile(BASE) as source,zipfile.ZipFile(unsigned,'a',compression=zipfile.ZIP_DEFLATED) as target:
    present=set(target.namelist())
    for entry in source.infolist():
        if entry.filename in present or entry.filename.startswith('META-INF/') or entry.filename in omitted:continue
        target.writestr(entry,source.read(entry.filename))
xml=run(TOOLS/'aapt2','dump','xmltree',unsigned,'--file','AndroidManifest.xml')
(OUT/'AndroidManifest.xmltree.txt').write_text(xml)
assert 'com.d2rmobile.nativefamilyfixture' in xml
assert 'com.d2rmobile.nativeport.MainActivity' in xml
assert '".MainActivity"' not in xml
run(TOOLS/'zipalign','-f','-P','16','4',unsigned,aligned)
run(TOOLS/'apksigner','sign','--ks',ROOT/'.local/build/android-apk/debug.keystore',
    '--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android',
    '--out',apk,aligned)
run(TOOLS/'apksigner','verify','--verbose',apk)
with zipfile.ZipFile(BASE) as source,zipfile.ZipFile(apk) as target:
    same={entry.filename:hashlib.sha256(target.read(entry.filename)).hexdigest()
          for entry in source.infolist() if entry.filename not in ('AndroidManifest.xml','resources.arsc')
          and not entry.filename.startswith('META-INF/') and entry.filename not in omitted}
    for name,value in same.items():assert hashlib.sha256(source.read(name)).hexdigest()==value,name
    for name,value in manifest['native_library_sha256'].items():assert same['lib/arm64-v8a/'+name]==value
    native_names={n for n in target.namelist() if n.startswith('lib/arm64-v8a/') and n.endswith('.so')}
    assert native_names=={'lib/arm64-v8a/'+n for n in manifest['native_library_sha256']}
    assert len(native_names)==6
result={'package':'com.d2rmobile.nativefamilyfixture','activity':'com.d2rmobile.nativeport.MainActivity',
    'source_revision':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
    'apk_sha256':sha(apk),'base_apk_sha256':sha(BASE),'manifest_xmltree_sha256':sha(OUT/'AndroidManifest.xmltree.txt'),
    'base_build_manifest_sha256':sha(BASE.with_suffix('.apk.build.json')),'packager_sha256':sha(Path(__file__)),
    'optional_validation_layer_omitted_explicitly':sorted(omitted),
    'native_library_sha256':manifest['native_library_sha256'],'base_payload_entries_verified_identical':same,
    'JNI_class_package_unchanged':'com.d2rmobile.nativeport','contains_game_binary':False,
    'build_commands':commands,'installed':False}
inputs=json.loads(A.inputs.read_text())
assert inputs['driver_sha256']=='6849875ce017b19a8913ed0fb3b9a1edcbfb13b44a25ad32d3bfbec7d1602338'
assert set(inputs['resources'])=={'resource4','resource6'}
resources=OUT/'resources';shutil.rmtree(resources,ignore_errors=True);resources.mkdir()
owned={}
for name,row in inputs['resources'].items():
    assert set(row['files'])=={'native-layouts.bin','native-resource.bin','resource-input.bin'}
    assert row['producer_sha256']=='043d009065b301a7cb05bab2c0d9517feb39f9464fca013929f100fe34802fe2'
    for leaf,item in row['files'].items():
        src=Path(item['path']);assert sha(src)==item['sha256']
        dst=resources/name/leaf;dst.parent.mkdir(exist_ok=True);shutil.copy2(src,dst)
        owned[name+'/'+leaf]=sha(dst)
shaders=OUT/'vulkan-fixture';shutil.rmtree(shaders,ignore_errors=True);shaders.mkdir()
for name,item in inputs['shaders'].items():
    assert not Path(name).is_absolute() and '..' not in Path(name).parts
    src=Path(item['path']);assert sha(src)==item['sha256']
    dst=shaders/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
shutil.copy2(A.inputs,OUT/'fixture-inputs.json')
result.update(fixture_inputs_sha256=sha(A.inputs),owned_resource_inputs=owned,
              driver_sha256=inputs['driver_sha256'],shader_files={n:r['sha256'] for n,r in inputs['shaders'].items()},
              ordinary_UID_executed=False,new_original_NVNGPU_executed=False,default_enabled=False)
apk.with_suffix('.apk.build.json').write_text(json.dumps(result,indent=2)+'\n')
print(apk)
