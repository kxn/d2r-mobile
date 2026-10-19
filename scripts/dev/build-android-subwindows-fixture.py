#!/usr/bin/env python3
"""Package an already built, byte-identical native fixture with a separate app ID."""
import hashlib,json,subprocess,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'.local/build/android-subwindows-fixture'
SDK=ROOT/'.local/android-sdk';TOOLS=SDK/'build-tools/36.0.0'
BASE=ROOT/'.local/build/android-apk/d2r-native-tlsdesc-debug.apk'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads(BASE.with_suffix('.apk.build.json').read_text())
assert sha(BASE)==manifest['apk_sha256']
for name,value in manifest['native_library_sha256'].items():
    with zipfile.ZipFile(BASE) as z:assert hashlib.sha256(z.read('lib/arm64-v8a/'+name)).hexdigest()==value
OUT.mkdir(parents=True,exist_ok=True)
unsigned=OUT/'unsigned.apk';aligned=OUT/'aligned.apk';apk=OUT/'subwindows-fixture.apk'
commands=[]
# Direct PanVK ICD cannot compose Android VVL; omit only that unused optional
# layer, recording its original bytes explicitly. Six host/loader libs stay exact.
omitted={"lib/arm64-v8a/libVkLayer_khronos_validation.so"}
def run(*args):
    args=list(map(str,args));commands.append(args);return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT)
# aapt2 keeps the original Java package-qualified Activity when renaming the
# manifest package. The compiled manifest below is verified before signing.
run(TOOLS/'aapt2','link','-I',SDK/'platforms/android-36/android.jar','--manifest',
    ROOT/'dm/android/AndroidManifest.xml','--rename-manifest-package','com.d2rmobile.borrowedsubwindowsfixture',
    '-o',unsigned,ROOT/'.local/build/android-apk/resources.zip')
with zipfile.ZipFile(BASE) as source,zipfile.ZipFile(unsigned,'a',compression=zipfile.ZIP_DEFLATED) as target:
    present=set(target.namelist())
    for entry in source.infolist():
        if entry.filename in present or entry.filename.startswith('META-INF/') or entry.filename in omitted:continue
        target.writestr(entry,source.read(entry.filename))
xml=run(TOOLS/'aapt2','dump','xmltree',unsigned,'--file','AndroidManifest.xml')
(OUT/'AndroidManifest.xmltree.txt').write_text(xml)
assert 'com.d2rmobile.borrowedsubwindowsfixture' in xml
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
result={'package':'com.d2rmobile.borrowedsubwindowsfixture','activity':'com.d2rmobile.nativeport.MainActivity',
    'source_revision':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
    'apk_sha256':sha(apk),'base_apk_sha256':sha(BASE),'manifest_xmltree_sha256':sha(OUT/'AndroidManifest.xmltree.txt'),
    'base_build_manifest_sha256':sha(BASE.with_suffix('.apk.build.json')),'packager_sha256':sha(Path(__file__)),
    'optional_validation_layer_omitted_explicitly':sorted(omitted),
    'native_library_sha256':manifest['native_library_sha256'],'base_payload_entries_verified_identical':same,
    'JNI_class_package_unchanged':'com.d2rmobile.nativeport','contains_game_binary':False,
    'build_commands':commands,'installed':False}
apk.with_suffix('.apk.build.json').write_text(json.dumps(result,indent=2)+'\n')
print(apk)
