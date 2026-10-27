#!/usr/bin/env python3
"""Reuse the original ordinary-UID fixture entry, allowing its existing import modes."""
import argparse,difflib,hashlib,json,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--producer',type=Path,required=True);a=p.parse_args()
out=a.producer.resolve();source=out/'source';ready=json.loads((out/'source-ready.json').read_text());diff=[]
changes={
 'dm/host/android/jni.c':('  if(strcmp(kind,"--resource-bake") && strcmp(kind,"--resource-bake-batch"))','  if(strcmp(kind,"--resource-bake") && strcmp(kind,"--resource-bake-batch") &&\n     strcmp(kind,"--native-bake") && strcmp(kind,"--native-import"))'),
 'dm/android/java/com/d2rmobile/nativeport/MainActivity.java':('            if (!"--resource-bake".equals(imageMode) && !"--resource-bake-batch".equals(imageMode))','            if (!"--resource-bake".equals(imageMode) && !"--resource-bake-batch".equals(imageMode) &&\n                    !"--native-bake".equals(imageMode) && !"--native-import".equals(imageMode))')}
for name,(before,after) in changes.items():
 f=source/name;s=f.read_text()
 if s.count(before)!=1:raise ValueError('Source entry anchor changed: '+name)
 t=s.replace(before,after);f.write_text(t);diff.extend(difflib.unified_diff(s.splitlines(True),t.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
(out/'app-entry.patch').write_text(''.join(diff))
local=source/'.local';local.mkdir();(local/'android-sdk').symlink_to(ROOT/'.local/android-sdk',target_is_directory=True)
# Private generated UI inputs are copied, never changed in the original tree.
frozen=ROOT/'.local/worktrees/turnip-native-bulk-producer-v1'
gen=source/'dm/ios/Gen';gen.mkdir(parents=True,exist_ok=True)
shutil.copytree(frozen/'dm/ios/Gen/assets',gen/'assets',dirs_exist_ok=False)
# Keep the exact already-accepted developer signing identity; no password/account data.
keys=local/'build/android-apk';keys.mkdir(parents=True)
shutil.copy2(frozen/'.local/build/android-apk/debug.keystore',keys/'debug.keystore')
ledger={str(f.relative_to(source)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(source.rglob('*')) if f.is_file() and '.local' not in f.relative_to(source).parts}
result={'schema':1,'base_revision':ready['base_revision'],'source':str(source),'producer_source_sha256':ready['producer_source_sha256'],'producer_source_ready_sha256':hashlib.sha256((out/'source-ready.json').read_bytes()).hexdigest(),'app_entry_patch_sha256':hashlib.sha256((out/'app-entry.patch').read_bytes()).hexdigest(),'source_ledger':ledger,'scope':'Original fixture/loader/Java. Only permit existing native-bake/import modes. No game execution, driver identity/coherent shortcut or timing added.','builder_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
(out/'app-source-ready.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_ledger'},indent=2))
