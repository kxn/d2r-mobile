#!/usr/bin/env python3
"""Adapt the existing Turnip original-NVN ownership transaction, offline only."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
BASE=ROOT/'.local/build/turnip-real-native-family-v1-candidate/source/scripts/dev/run-turnip-real-native-family.py';s=BASE.read_text();changes=[]
def change(before,after):
 global s
 if s.count(before)!=1:raise ValueError('Nonunique controller anchor: '+before[:100])
 s=s.replace(before,after);changes.append(before[:100])
change('a=P.parse_args();R=Path(__file__).resolve().parents[2]','a=P.parse_args();R=Path(__file__).resolve().parents[4]')
change("pkg='com.d2rmobile.turniprealfamily'","pkg='com.d2rmobile.nativefamilyfixture'")
change("m=json.loads((a.artifact/'candidate.apk.build.json').read_text())", """m=json.loads((a.artifact/'candidate.apk.build.json').read_text())
ready=json.loads((a.artifact/'ready.json').read_text())
assert ready['APK']['sha256']==m['apk_sha256'] and ready['native_libraries']==m['native_library_sha256']
assert sha(a.artifact/'candidate.apk.build.json')==ready['candidate_manifest']['sha256']
asset_root=Path(ready['artifact_root']);assert asset_root.resolve()==a.artifact.resolve().parent
assert sha(Path(ready['driver_build']['path']))==ready['driver_build']['sha256']
assert ready['driver']['sha256']=='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'""")

change("P.add_argument('--execute',action='store_true')","P.add_argument('--driver-query',type=Path,required=True,help='prior actual new-driver real resource metadata')\nP.add_argument('--execute',action='store_true')")
change("assert sha(a.driver)=='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'",'''assert sha(a.driver)=='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'
query=json.loads(a.driver_query.read_text());assert query['complete'] and query['driverID']==18
assert query['producer_source_sha256']=='9a8a5a64bc4dc8b9e19fb35d2ff1c0d0bd72f307f2e772f8a18e4e9d6f99019b'
assert all(re.fullmatch('[0-9a-f]{32}',query[k]) and int(query[k],16) for k in ('layout_UUID','device_UUID','driver_UUID'))''')
change("assert set(inputs['resources'])=={'resource4'}","assert set(inputs['resources'])=={'bc3-unorm','bc3-srgb'}\nassert all(row['producer_sha256']==query['producer_source_sha256'] for row in inputs['resources'].values())")
change("for name in ['resource4']","for name in ['bc3-unorm','bc3-srgb']")
change("assert len(deployment['artifacts'])==28","assert 28<=len(deployment['artifacts'])<=64")
change("'module_count':28 if a.module_report else 0","'module_count':len(deployment['artifacts']) if a.module_report else 0")
change('ONE installation, ONE cold original NVN real ASTC4 family; full/native + original tail CPU-packaged/B2T; game=false; original package read-only; VVL unavailable direct ICD; no performance claim','ONE installation, TWO cold original NVN real BC3 UNORM/sRGB families; full/native plus original thumbnail/tail packaged and B2T, alias/publication/retirement assertions; no window/lazy, no game/performance claim')
change("'launches_allowed':1,'modes_once':['resource4']","'launches_allowed':2,'modes_once':['bc3-unorm','bc3-srgb']")
change("for kind,test_name in [('resource4','native-resource-original-NVN')]:","for kind,test_name in [('bc3-unorm','native-resource-original-NVN'),('bc3-srgb','native-resource-original-NVN')]:")
change("assert final['format']==(158 if kind=='resource4' else 166)","assert final['format']==(137 if kind=='bc3-unorm' else 138)")
change("assert all(identities[0][k]==inputs['actual_identity'][k] for k in ('layout_UUID','device_UUID','driver_UUID'))","assert all(identities[0][k]==query[k] for k in ('layout_UUID','device_UUID','driver_UUID'))")
# Borrow the exact existing batch controller's map identity checker. This is
# process/file qualification only, not a new tool or sampler.
bulk=(ROOT/'.local/worktrees/turnip-native-bulk-controller-v2/scripts/dev/run-turnip-native-resource-batches-v2.py').read_text();start=bulk.index('def validate_driver_maps(');end=bulk.index('\ndef valid_provider(',start);helper=bulk[start:end]
change('def snapshot():',helper+'\ndef snapshot():')
anchor=" result['runs']=[]"
stats=''' driver_alias_stat={}
 for private in (f'/data/data/{pkg}/files/runtime/vulkan-driver/libvulkan_freedreno.so',f'/data/user/0/{pkg}/files/runtime/vulkan-driver/libvulkan_freedreno.so'):
  raw_stat=ad('exec-out','run-as',pkg,'stat','-c','%d:%i:%s',private)
  device,inode,size=map(int,raw_stat.split(':'))
  assert inode>0 and size==a.driver.stat().st_size
  assert ad('exec-out','run-as',pkg,'sha256sum',private).split()[0]==sha(a.driver)
  driver_alias_stat[private]={'major':os.major(device),'minor':os.minor(device),'inode':inode,'size':size}
 assert len({(x['major'],x['minor'],x['inode'],x['size']) for x in driver_alias_stat.values()})==1
 result['driver_alias_stat']=driver_alias_stat
'''
change(anchor,stats+anchor);s=s.replace('import argparse, hashlib, io, json, re, subprocess, time, uuid, signal, zipfile, xml.etree.ElementTree as ET','import argparse, hashlib, io, json, re, subprocess, time, uuid, signal, zipfile, os, xml.etree.ElementTree as ET')
change("  assert 'files/runtime/vulkan-driver/libvulkan_freedreno.so' in maps_text","  run['actual_driver_maps']=validate_driver_maps(maps_text,driver_alias_stat)")
change("'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),","'driver_sha256':sha(a.driver),'controller_sha256':sha(__file__),'driver_query_sha256':sha(a.driver_query),")
s=s.replace("a.artifact/'fixture-inputs.json'","asset_root/'fixture-inputs.json'").replace("a.artifact/'resources'","asset_root/'resources'").replace("a.artifact/'vulkan-fixture'","asset_root/'vulkan-fixture'")
path=Path(__file__).with_name('run_nvn_fixture.py');path.write_text(s)
Path(__file__).with_name('nvn-controller-source.json').write_text(json.dumps({'base':str(BASE),'base_sha256':hashlib.sha256(BASE.read_bytes()).hexdigest(),'map_checker_source':str(ROOT/'.local/worktrees/turnip-native-bulk-controller-v2/scripts/dev/run-turnip-native-resource-batches-v2.py'),'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'controller_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'changes':changes,'scope':'Reuse original cold launch/PID/crash/installer/cleanup transaction; original NVN C untouched; no profiler/loader/timer.'},indent=2)+'\n');print(path)
