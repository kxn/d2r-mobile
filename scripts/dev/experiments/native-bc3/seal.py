#!/usr/bin/env python3
"""Seal an original fixture APK and bind source/ELF/driver receipts, no device work."""
import argparse,hashlib,json,shutil,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--producer',type=Path,required=True);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--driver-build',type=Path,required=True);p.add_argument('--scope',required=True);a=p.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=a.producer.resolve();src=out/'source';ready=json.loads((out/'source-ready.json').read_text());app=json.loads((out/'app-source-ready.json').read_text());prefix=src/'scripts/dev/native-image-payload'
for name,digest in ready['CMake_inputs'].items():assert sha(prefix/name)==digest
host=src/'.local/build/android-native/libdmhost_android.so';assert ready['producer_source_sha256'].encode() in host.read_bytes()
pack=src/'.local/build/android-native-image-fixture/native-image-fixture.apk';m=json.loads(pack.with_suffix('.apk.build.json').read_text());assert sha(pack)==m['apk_sha256']
with zipfile.ZipFile(pack) as z:
 for name,digest in m['native_library_sha256'].items():assert hashlib.sha256(z.read('lib/arm64-v8a/'+name)).hexdigest()==digest
 assert hashlib.sha256(z.read('lib/arm64-v8a/libdmhost_android.so')).hexdigest()==sha(host)
assert len(m['native_library_sha256'])==6 and m['package']=='com.d2rmobile.turniprealproducer'
# The archive source has no .git; the original packager may discover the parent
# live worktree HEAD. Preserve that lookup but explicitly replace its authority.
m['packager_ambient_parent_revision']=m['source_revision'];m['source_revision']=ready['base_revision'];m['source_clean']=False
m['source_identity_authority']='source-ready/app-source-ready ledgers and original compiler-dependency manifest; ambient parent git HEAD is not authoritative'
m['independent_source_patches']={'producer':sha(out/'producer.patch'),'App_entry':sha(out/'app-entry.patch')};m['producer_source_sha256']=ready['producer_source_sha256']
canonical=out/'canonical';canonical.mkdir(exist_ok=False);shutil.copy2(pack,canonical/'candidate.apk');(canonical/'candidate.apk.build.json').write_text(json.dumps(m,indent=2)+'\n')
driver_path=a.driver_build.resolve();d=json.loads(driver_path.read_text());driver=Path(d['driver']['path']);assert d['complete'] and sha(driver)==d['driver']['sha256']=='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'
inputs=a.inputs.resolve();j=json.loads(inputs.read_text());assert j['producer_source_sha256']==ready['producer_source_sha256']
for row in j['resources'].values():
 for key in ['input','declaration']:assert sha(Path(row[key]['path']))==row[key]['sha256']
ledger={str(f.relative_to(src)):sha(f) for f in sorted(src.rglob('*')) if f.is_file() and '.local' not in f.relative_to(src).parts}
(out/'final-source-ledger.json').write_text(json.dumps(ledger,indent=2)+'\n')
record=lambda f:{'path':str(f.resolve()),'sha256':sha(f)}
result={'status':'HOST_BUILT_AWAITING_REAL_GPU_QUERY','package':m['package'],'activity':m['activity'],'APK':record(canonical/'candidate.apk'),'candidate_manifest_sha256':sha(canonical/'candidate.apk.build.json'),'native_libraries':m['native_library_sha256'],'producer_source_sha256':ready['producer_source_sha256'],'producer_source_ready':record(out/'source-ready.json'),'app_source_ready':record(out/'app-source-ready.json'),'producer_patch':record(out/'producer.patch'),'app_entry_patch':record(out/'app-entry.patch'),'final_source_ledger':record(out/'final-source-ledger.json'),'driver_build':record(driver_path),'driver':record(driver),'prepared_real_inputs':record(inputs),'host_tests':record(out/'all-owned-tests.log'),'queried_UUIDs':None,'device_executed':False,'coherent_qualification':'NOT_GRANTED','scope':a.scope,'sealer_sha256':sha(Path(__file__))}
(canonical/'ready.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
