#!/usr/bin/env python3
"""Data/driver variant of the already-built NVN fixture; never rebuild or relabel payloads."""
import argparse,copy,hashlib,json,shutil,struct,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();base=a.base.resolve();out=a.output.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if out.exists():raise ValueError('New variant directory required')
br=json.loads((base/'ready.json').read_text());bm=json.loads((base/'candidate.apk.build.json').read_text());assert sha(base/'candidate.apk')==bm['apk_sha256']=='4d3449f34ef2d8c863347b9f213863d5109d7d0771d79ed295cbef9399f89cd7'
assert bm['native_library_sha256']['libdmhost_android.so']=='6bcec2002761294f0d8bb511e572be549e3c809fc0b24a086b3e9245f55abca0'
with zipfile.ZipFile(base/'candidate.apk')as z:
 for n,h in bm['native_library_sha256'].items():assert hashlib.sha256(z.read('lib/arm64-v8a/'+n)).hexdigest()==h
assert sha(base/'candidate.apk.build.json')==br['candidate_manifest']['sha256'];assert len(bm['native_library_sha256'])==6
build=ROOT/'.local/build/turnip-native-bc3-tiled-v1/build.json';driver=json.loads(build.read_text());DS='3e9376ed3759af2cd015cf5608274f661ba3b33df6e95cb6fa2542666f34ee50';PS='9194548d1b1c897d299d6f42b9336aa445a48f39c21f099a9b806b77fbace8fa'
assert driver['complete'] and driver['driver']['sha256']==DS==sha(Path(driver['driver']['path']))
out.mkdir();resources=out/'resources';resources.mkdir();inputs={'driver_sha256':DS,'resources':{},'shaders':{}};owned={};identity=None;provenance={}
for label,fmt in [('bc3-unorm',137),('bc3-srgb',138)]:
 pack=ROOT/'.local/logs/native-bc3-tiled-real-pack-v1'/label;query=ROOT/'.local/logs/native-bc3-tiled-probe-v1/actual'/label;source=ROOT/'.local/build/native-bc3-producer-v2/real-inputs'/label
 meta=json.loads((query/'native-resource.json').read_text());declaration=json.loads((source/'resource-source.json').read_text());manifest=json.loads((pack/'native-pack.json').read_text())
 assert meta['complete'] and meta['producer_source_sha256']==declaration['intended_producer_sha256']==PS
 assert meta['recipe']['VkFormat']==fmt and meta['layout_UUID']=='4504211f903049f5ae3acd7eda74b7bb'
 current={k:meta[k]for k in ['layout_UUID','device_UUID','driver_UUID']}
 assert identity is None or identity==current;identity=current
 table=(pack/'native-layouts.bin').read_bytes();assert table[:8]==b'DMNTBL2\0' and len(table)==1216
 assert table[32:64].hex()==manifest['pack_content_sha256'] and table[64:96].hex()==DS and table[96:128].hex()==PS
 recipe=table[208:1200];assert struct.unpack_from('<I',recipe,32)[0]==fmt and recipe[160:192].hex()==sha(source/'resource-input.bin')
 assert recipe[192:224].hex()==sha(query/'native-resource.bin') and recipe[288:320].hex()==PS
 for key,offset in [('layout_UUID',112),('device_UUID',128),('driver_UUID',144)]:assert recipe[offset:offset+16].hex()==meta[key]
 dest=resources/label;dest.mkdir();files={}
 for leaf,path in [('native-layouts.bin',pack/'native-layouts.bin'),('native-resource.bin',query/'native-resource.bin'),('resource-input.bin',source/'resource-input.bin')]:
  shutil.copy2(path,dest/leaf);h=sha(path);assert sha(dest/leaf)==h;files[leaf]={'path':str((dest/leaf).resolve()),'sha256':h};owned[label+'/'+leaf]=h
 inputs['resources'][label]={'pack_sha256':manifest['pack_content_sha256'],'producer_sha256':PS,'vk_format':fmt,'files':files,'pack_manifest':{'path':str(pack/'native-pack.json'),'sha256':sha(pack/'native-pack.json')}}
 provenance[label]={'actual_query':{'path':str(query/'native-resource.json'),'sha256':sha(query/'native-resource.json')},'source_declaration':{'path':str(source/'resource-source.json'),'sha256':sha(source/'resource-source.json')},'physical_image_bytes':meta['physical_image_bytes']}
inputs['actual_identity']=identity
shader_dir=out/'vulkan-fixture';shader_dir.mkdir()
for n,h in bm['shader_files'].items():
 path=Path(br['artifact_root'])/'vulkan-fixture'/n;assert sha(path)==h;shutil.copy2(path,shader_dir/n);inputs['shaders'][n]={'path':str((shader_dir/n).resolve()),'sha256':h}
(out/'fixture-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
canonical=out/'canonical';canonical.mkdir();shutil.copy2(base/'candidate.apk',canonical/'candidate.apk');assert sha(canonical/'candidate.apk')==bm['apk_sha256']
m=copy.deepcopy(bm);m.update(fixture_inputs_sha256=sha(out/'fixture-inputs.json'),driver_sha256=DS,owned_resource_inputs=owned,variant_kind='data-and-driver-only; APK and all six compiled libraries byte-identical',variant_base_manifest_sha256=sha(base/'candidate.apk.build.json'),ordinary_UID_executed=False,new_original_NVNGPU_executed=False,coherent_qualification='NOT_GRANTED; unchanged compiled APK uses FOREIGN for this SHA')
(canonical/'candidate.apk.build.json').write_text(json.dumps(m,indent=2)+'\n')
r=copy.deepcopy(br);r.update(status='DATA_DRIVER_VARIANT_AWAITING_ORIGINAL_NVN_GPU',artifact_root=str(out),APK={'path':str(canonical/'candidate.apk'),'sha256':sha(canonical/'candidate.apk')},candidate_manifest={'path':str(canonical/'candidate.apk.build.json'),'sha256':sha(canonical/'candidate.apk.build.json')},fixture_inputs={'path':str(out/'fixture-inputs.json'),'sha256':sha(out/'fixture-inputs.json')},driver_build={'path':str(build),'sha256':sha(build)},driver={'path':driver['driver']['path'],'sha256':DS},variant_base_ready={'path':str(base/'ready.json'),'sha256':sha(base/'ready.json')},variant_base_manifest_sha256=sha(base/'candidate.apk.build.json'),data_variant_builder={'path':str(Path(__file__).resolve()),'sha256':sha(Path(__file__))},actual_data_provenance=provenance,device_executed=False,coherent_qualification='NOT_GRANTED')
# Source/compiler identities are inherited without edits. The new wrapper's
# provenance is separate and cannot pretend to be a new compiled host revision.
(canonical/'ready.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:r[k]for k in ['status','APK','native_libraries','driver','fixture_inputs','actual_data_provenance']},indent=2))
