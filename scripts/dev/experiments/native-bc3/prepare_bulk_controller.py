#!/usr/bin/env python3
"""Reuse the one-install frozen v2 bulk controller for the fixed original BC3 set."""
import hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4];FROZEN=ROOT/'.local/worktrees/turnip-native-bulk-controller-v2/scripts/dev'
BASE=FROZEN/'run-turnip-native-resource-batches-v2.py';s=BASE.read_text();changes=[]
def change(before,after):
 global s
 if s.count(before)!=1:raise ValueError('Controller anchor not unique: '+before[:100])
 s=s.replace(before,after);changes.append(before[:100])
change('a=P.parse_args();R=Path(__file__).resolve().parents[2]','a=P.parse_args();R=Path(__file__).resolve().parents[4]')
change("PRODUCER_SHA='8064b80022370de2832964ee22015539441981e7272270b0ff01f5b6c6062ca3'","PRODUCER_SHA='9194548d1b1c897d299d6f42b9336aa445a48f39c21f099a9b806b77fbace8fa'")
change("assert sha(a.driver)=='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'",'''assert sha(a.driver)=='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'
ready=json.loads((a.artifact/'ready.json').read_text())
assert ready['APK']['sha256']==m['apk_sha256'] and ready['producer_source_sha256']==PRODUCER_SHA==m['producer_source_sha256']
assert ready['native_libraries']==m['native_library_sha256'] and ready['driver']['sha256']==sha(a.driver)
assert sha(Path(ready['driver_build']['path']))==ready['driver_build']['sha256']
assert sha(a.resource_input/'batch-prepare.json')=='320bac7f3b4beaa8ecd9cd4dba55c795f4d612c1c5010dbbe07f368f0086001f' ''')
change("assert prepared['complete'] and prepared['batch_max_entries']==128","assert prepared['complete'] and prepared['batch_max_entries']==128\nassert len(prepared['entries'])==6217 and len(prepared['batches'])==49")
change("assert expected_query['complete'] and expected_query['driverID']==18","assert expected_query['complete'] and expected_query['driverID']==18\nassert expected_query['producer_source_sha256']==PRODUCER_SHA")
change('ONE installation, explicit finite cold ASTC4 batches; original bake per entry; each stops/pulls/verifies before next; no game/performance claim','ONE installation, explicit finite cold original BC3 UNORM/sRGB batches for the unchanged6217 logical set; existing driver host-copy bake per entry; each stops/pulls/verifies before next; no game/sampling/performance claim')
path=Path(__file__).with_name('run_bulk.py');path.write_text(s)
helpers={}
for name in ['native_batch_installer.py','native_batch_outputs.py']:
 src=FROZEN/name;dest=Path(__file__).with_name(name);shutil.copy2(src,dest);helpers[name]=hashlib.sha256(src.read_bytes()).hexdigest()
Path(__file__).with_name('bulk-controller-source.json').write_text(json.dumps({'base':str(BASE),'base_sha256':hashlib.sha256(BASE.read_bytes()).hexdigest(),'base_revision':'3f2d062619a429b6e1074c7b377a4051b408767e','generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'controller_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'unchanged_helpers':helpers,'changes':changes,'scope':'Original one-install49 bounded cold batch ownership, archive extraction/receipts helpers reused byte-for-byte. Only fixed BC3 source/producer/driver identity qualification changes. No loader, codec, tiler, timer or sampler.'},indent=2)+'\n');print(path)
