#!/usr/bin/env python3
"""Adapt the frozen ordinary-UID controller; no ADB is executed by this generator."""
import argparse,hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4];BASE=ROOT/'.local/worktrees/turnip-native-bulk-producer-v1/scripts/dev/run-turnip-native-image-fixture-v2.py'
parser=argparse.ArgumentParser();parser.add_argument('--reference',action='store_true');a=parser.parse_args()
s=BASE.read_text();changes=[]
def change(before,after):
 global s
 if s.count(before)!=1:raise ValueError('Controller anchor not unique: '+before[:80])
 s=s.replace(before,after);changes.append(before[:120])
change('a=P.parse_args();R=Path(__file__).resolve().parents[2]','a=P.parse_args();R=Path(__file__).resolve().parents[4]')
change("pkg='com.d2rmobile.turniphostimagefixture'","pkg='com.d2rmobile.turniprealproducer'")
change("assert sha(a.driver)=='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'",'''assert sha(a.driver)=='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'
ready=json.loads((a.artifact/'ready.json').read_text())
assert ready['APK']['sha256']==m['apk_sha256'] and ready['producer_source_sha256']==m['producer_source_sha256']
assert ready['native_libraries']==m['native_library_sha256'] and ready['driver']['sha256']==sha(a.driver)
assert sha(Path(ready['driver_build']['path']))==ready['driver_build']['sha256']
driver_build=json.loads(Path(ready['driver_build']['path']).read_text())
assert driver_build['complete'] and driver_build['driver']['sha256']==sha(a.driver)
assert ready['producer_source_sha256']=='9a8a5a64bc4dc8b9e19fb35d2ff1c0d0bd72f307f2e772f8a18e4e9d6f99019b'
assert sha(Path(ready['prepared_real_inputs']['path']))==ready['prepared_real_inputs']['sha256']
resources_input=json.loads(Path(ready['prepared_real_inputs']['path']).read_text())['resources']
assert set(resources_input)=={'bc3-unorm','bc3-srgb'}
for row in resources_input.values():
 for key in ['input','declaration']:
  assert sha(Path(row[key]['path']))==row[key]['sha256']
 declaration=json.loads(Path(row['declaration']['path']).read_text())
 assert declaration['source_profile']=='original-bc3-raw-casc-v1' and declaration['intended_producer_sha256']==ready['producer_source_sha256']''')
change("assert len(deployment['artifacts'])==28","assert 28<=len(deployment['artifacts'])<=64")
change("'module_count':28 if a.module_report else 0","'module_count':len(deployment['artifacts']) if a.module_report else 0")
change("ONE installation, TWO cold standalone owned native-image modes: bake/import, each once; game=false; original package read-only; VVL explicitly unavailable with direct ICD; no performance claim","ONE installation, FOUR cold modes: original native bake/import for BC3+ASTC regression, two original real BC3 host-copy bakes; game=false; original package read-only; no performance claim. Generic8x8 four mips only; NPOT and original NVN real-resource sampling NOT covered.")
change("'launches_allowed':2,'modes_once':['native-bake','native-import']","'launches_allowed':4,'modes_once':['native-bake','native-import','bc3-unorm','bc3-srgb']")
change("for kind,test_name in [('native-bake','native-host-image-payload'),('native-import','native-host-image-payload')]:",'''for label,kind,test_name in [('native-bake','native-bake','native-host-image-payload'),('native-import','native-import','native-host-image-payload'),('bc3-unorm','resource-bake','native-host-image-payload'),('bc3-srgb','resource-bake','native-host-image-payload')]:
  if kind=='resource-bake':
   entry=resources_input[label]
   for key,leaf in [('input','resource-input.bin'),('declaration','resource-source.json')]:
    local=Path(entry[key]['path']);assert sha(local)==entry[key]['sha256']
    ad('push',local,tmp+'/'+leaf)
    ad('shell','run-as',pkg,'cp',tmp+'/'+leaf,'files/runtime/'+leaf)
    assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/'+leaf).split()[0]==entry[key]['sha256']''')
change("run={'name':kind,'kind':kind","run={'name':label,'kind':kind")
s=s.replace("kind+'-launch.txt'","label+'-launch.txt'").replace("kind+'-maps.txt'","label+'-maps.txt'")
change("final['formats']==4","final['formats']==(1 if kind=='resource-bake' else 6)")
change(" and identity[0]['width']==8 and identity[0]['height']==8 and identity[0]['mips']==4",'')
change("formats={157,158,165,166}","formats={137,138,157,158,165,166}")
change("assert len(preflights)==4 and {r['format'] for r in preflights}==formats","assert kind=='resource-bake' or (len(preflights)==6 and {r['format'] for r in preflights}==formats)")
change("assert len(baked)==8 and", "assert len(baked)==12 and")
change("  else:\n   imported=[r for r in rows if r.get('native_import_sampled') is True]", "  elif kind=='native-import':\n   imported=[r for r in rows if r.get('native_import_sampled') is True]")
change("assert len(imported)==16 and", "assert len(imported)==24 and")
change("  run.update(result='passed');save()",'''  else:
   baked=[r for r in rows if r.get('native_resource_bake_complete') is True]
   entry=resources_input[label];declaration=json.loads(Path(entry['declaration']['path']).read_text())
   assert len(baked)==1 and (baked[0]['format'],baked[0]['width'],baked[0]['height'],baked[0]['levels'])==(declaration['VkFormat'],256,256,9)
   assert baked[0]['GPU_sampling_executed'] is False
   run['baked_outputs']={}
   import shlex
   for leaf in ['native-resource.bin','native-resource-layout.bin','native-resource.json']:
    remote='files/runtime/'+leaf;digest=ad('exec-out','run-as',pkg,'sha256sum',remote).split()[0]
    target=tmp+'/'+label+'-'+leaf;ad('shell','test','!','-e',target)
    ad('shell','run-as '+shlex.quote(pkg)+' cat '+shlex.quote(remote)+' > '+shlex.quote(target))
    assert ad('shell','sha256sum',target).split()[0]==digest
    local=a.output/label/leaf;local.parent.mkdir(exist_ok=True)
    ad('pull',target,local);assert sha(local)==digest;run['baked_outputs'][leaf]=digest
   record=json.loads((a.output/label/'native-resource.json').read_text())
   assert record['producer_source_sha256']==ready['producer_source_sha256']
   assert all(record[k]==identity[0][k] for k in ('layout_UUID','device_UUID','driver_UUID'))
   run['actual_baked_record']=record
  run.update(result='passed');save()''')
change("  run['PID_absent_after']=True;save()",'''  run['PID_absent_after']=True;save()
  if kind=='resource-bake':
   # Own immutable outputs were pulled and hashed. Move rather than overwrite
   # before the next cold resource bake's exclusive-create contract.
   ad('shell','run-as',pkg,'mkdir','-p','files/runtime/probe-results/'+label)
   for leaf,digest in run['baked_outputs'].items():
    assert ad('exec-out','run-as',pkg,'sha256sum','files/runtime/'+leaf).split()[0]==digest
    ad('shell','run-as',pkg,'mv','files/runtime/'+leaf,'files/runtime/probe-results/'+label+'/'+leaf)''')
s=s.replace('actual-coherent-turnip-native-image-owned-passed','actual-bc3-native-owned-probe-passed')
if a.reference:
 s=s.replace('9a8a5a64bc4dc8b9e19fb35d2ff1c0d0bd72f307f2e772f8a18e4e9d6f99019b','9194548d1b1c897d299d6f42b9336aa445a48f39c21f099a9b806b77fbace8fa')
 s=s.replace('Generic8x8 four mips only; NPOT and original NVN real-resource sampling NOT covered.','BC3-only13x9/4mip with midtone565 sRGB CPU oracle and exact ordinary nonexternal VkImage GPU sample-byte reference. Original NVN real-resource sampling NOT covered.')
 s=s.replace("else 6)","else 2)").replace('formats={137,138,157,158,165,166}','formats={137,138}').replace('len(preflights)==6','len(preflights)==2').replace('len(baked)==12','len(baked)==4').replace('len(imported)==24','len(imported)==8')
 s=s.replace("   for row in imported:","   references=[r for r in rows if r.get('ordinary_BC3_reference') is True]\n   assert len(references)==8 and all(r['format'] in formats and r['width']==13 and r['height']==9 and r['mips']==4 and r['NPOT'] and r['nonexternal_optimal'] and r['native_sample_bytes_exact'] and r['nonendpoint_RGB565'] for r in references)\n   for row in imported:")
path=Path(__file__).with_name('run_reference_probe.py' if a.reference else 'run_probe.py');path.write_text(s)
Path(__file__).with_name('controller-reference-source.json' if a.reference else 'controller-source.json').write_text(json.dumps({'base':str(BASE),'base_sha256':hashlib.sha256(BASE.read_bytes()).hexdigest(),'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'controller_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'changes':changes,'scope':'Only orchestration and assertions. Original cold app/PID/crash/installer/cleanup ownership logic retained; no profiler, timer, shader or loader implementation.'},indent=2)+'\n')
print(path)
