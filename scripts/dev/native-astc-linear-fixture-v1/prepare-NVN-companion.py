#!/usr/bin/env python3
"""Reuse byte-exact 0a54 THIN owned NVN fixture APK; resources require real new-driver bake."""
import argparse,hashlib,importlib.util,json,shutil,zipfile
from pathlib import Path
HOST=Path('/home/dl/d2r-mobile');DRIVER='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def rec(p):p=Path(p).resolve();return dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size)
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mode',choices=['old-AA-negative','new-positive','qualified-ASTC6'],required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--producer-receipt',type=Path);p.add_argument('--source-texture',type=Path,default=HOST/'.local/build/nvn-native-resource-runtime-v1/source4.texture');p.add_argument('--qualified-format',type=int,choices=[165,166]);a=p.parse_args();assert not a.output.exists()
base=HOST/'.local/build/native-image-only-app-0a54ef2-candidate';canonical=base/'candidate.apk';assert sha(canonical)=='0c5fbf472fcb3b597100de0c02cddebb3ee679d883ce63d23354b2fd62ed459f';m=json.loads((base/'candidate.apk.build.json').read_text());source=HOST/'.local/build/turnip-real-native-resource-v1-input'
for n,h in {'resource-input.bin':'d078631c7797b699200230a09533c9ad394a40fb9244fe0f75d905974ca49354','resource-source.json':'5d2e767a618a805d412f86c70be6f3c926897e5085621461390386f368e6d269'}.items():assert sha(source/n)==h
assert m['package']=='com.d2rmobile.nativewindowfamilyfixture'and m['contains_game_binary']is False and m['original_game_classes_assets_packaged']is False
with zipfile.ZipFile(canonical)as apk:
 dex=apk.read('classes.dex');assert b'Lcom/d2rmobile/nativewindowfamilyfixture/MainActivity;'in dex and b'Lcom/d2rmobile/nativeport/MainActivity;'not in dex
 for n,h in m['native_library_sha256'].items():assert hashlib.sha256(apk.read('lib/arm64-v8a/'+n)).hexdigest()==h
 assert not any(n.startswith('assets/')and n!='assets/licenses/opus-1.5.2.txt'for n in apk.namelist())
if a.mode=='qualified-ASTC6':
 import sys
 sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
 import qualified_astc6_NVN
 assert a.qualified_format is not None and a.producer_receipt is None,'qualified mode uses the exact reviewed actual32 receipt'
 print(json.dumps(qualified_astc6_NVN.prepare(a,base,canonical,m)));raise SystemExit(0)
assert a.qualified_format is None,'qualified format is only valid in explicit ASTC6 mode'
receipt=None
if a.mode=='new-positive':
 assert a.producer_receipt is not None,'actual original producer output required; no predicted opaque payloads'
 source_metadata=json.loads((source/'resource-source.json').read_text());assert sha(a.source_texture)==source_metadata['source_texture']['sha256']=='05ccdebfb3bbf578f455cccd6c9cccd6bf1db5259c117aa3f68784bfceeb3ea4'
 original_composition=json.loads((HOST/'.local/build/turnip-one-native-overlay-final-v2/composition.json').read_text());assert original_composition['source_fingerprints'][str(a.source_texture.resolve())]==sha(a.source_texture)
 receipt=json.loads(a.producer_receipt.read_text());assert receipt['stage']=='actual-native-ASTC-fully-linear-real-firepit-producer-passed-not-NVN'and not receipt['cleanup_errors']and receipt['original_unchanged']and receipt['driver_sha256']==DRIVER
 baked=a.producer_receipt.parent/'resource4'
 for n,h in receipt['baked_outputs'].items():assert sha(baked/n)==h
 query=json.loads((baked/'native-resource.json').read_text());assert query['complete']and query['layout_UUID']=='ab91c540847448360a44496b50045159'and query['driverID']==18 and query['physical_image_bytes']==94208 and query['recipe']['mipLevels']==9 and query['recipe']['VkFormat']==158
else:baked=base/'resource4'
a.output.mkdir(parents=True);out=a.output.resolve();shutil.copy2(canonical,out/'candidate.apk');shutil.copytree(base/'vulkan-fixture',out/'vulkan-fixture');shutil.copy2(base/'fixture-hashes.json',out/'fixture-hashes.json');(out/'resource4').mkdir()
if a.mode=='new-positive':
 packer=HOST/'.local/worktrees/native-image-only-astc-app-v2/scripts/dev/pack-native-texture.py'
 assert sha(packer)=='f4ae133c09071606a48fce987ac9c1da442266e89d217a386c471eea8ee1b5cb'
 spec=importlib.util.spec_from_file_location('original_native_packer',packer);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 manifest=HOST/'.local/logs/turnip-native-astc-whole-linear-v1-source-ready.json';assert json.loads(manifest.read_text())['driver']['sha256']==DRIVER
 module.build([{'source':str(source/'resource-source.json'),'texture':str(a.source_texture.resolve()),'baked':str(baked)}],out/'pack',manifest,driver_query=baked/'native-resource.json')
 record=json.loads((out/'pack/native-pack.json').read_text());assert record['complete']and record['family_protocol']==2 and len(record['entries'])==1 and record['driver_binary_sha256']==DRIVER
 paths={'native-layouts.bin':out/'pack/native-layouts.bin','native-resource.bin':baked/'native-resource.bin','resource-input.bin':source/'resource-input.bin'}
 m['native_resource_pack_sha256']=record['pack_content_sha256'];m['native_resource_producer_sha256']=record['producer_source_sha256'];m['native_payload_bytes']=94208;m['actual_producer_receipt']=rec(a.producer_receipt);m['actual_query']=rec(baked/'native-resource.json');m['actual_source_texture']=rec(a.source_texture);m['original_strict_packer']=rec(packer);m['native_pack']=rec(out/'pack/native-pack.json')
else:paths={n:base/'resource4'/n for n in m['owned_resource_inputs']}
for n,f in paths.items():shutil.copy2(f,out/'resource4'/n)
m['owned_resource_inputs']={n:sha(out/'resource4'/n)for n in paths};m['driver_sha256']=DRIVER;m['apk_sha256']=sha(out/'candidate.apk');m['original_thin_owned_APK']=rec(canonical);m['linear_candidate_mode']=a.mode;m['preparer']=rec(Path(__file__));m['source_runtime_unmodified']='0a54ef2ce1902f1967f00ee28d8765fa7a978942';m['no_shader_changes']=True;m['requires_actual_matrix_before_execute']=True;m['installed']=False;m['performance_claim']=False;(out/'candidate.apk.build.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps({'artifact':str(out),'mode':a.mode,'APK':rec(out/'candidate.apk'),'descriptor':rec(out/'candidate.apk.build.json'),'actual_GPU_or_NVN_run':False}))
