#!/usr/bin/env python3
"""Bind one actual ordinary-UID Turnip bake to the existing NVN family oracle.
No device access, vendor tiler, new resource protocol, CASC deployment or game run.
"""
import argparse,hashlib,json,subprocess,sys,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
P=argparse.ArgumentParser();P.add_argument('--bake-report',type=Path,required=True);P.add_argument('--source',type=Path,required=True);P.add_argument('--texture',type=Path,required=True);P.add_argument('--driver-manifest',type=Path,required=True);P.add_argument('--shader-inputs',type=Path,required=True);P.add_argument('--output',type=Path,required=True);a=P.parse_args()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
if a.output.exists():raise ValueError('new output required; do not resume partial bundles')
b=json.loads(a.bake_report.read_text());assert b['stage']=='actual-turnip-real-resource-bake-owned-passed' and not b['cleanup_errors'] and b['original_unchanged'] and b['fixture_uninstalled']
assert b['driver_sha256']=='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'
assert len(b['runs'])==1 and b['runs'][0]['result']=='passed' and b['runs'][0]['PID_absent_after']
source=json.loads(a.source.read_text());assert source['source_texture']['logical']=='data/hd/env/texture/act1/town/act1_town_camp/act1_town_camp_firepit01_a_alb.texture'
assert (source['VkFormat'],source['width'],source['height'],source['levels'])==(158,256,256,9)
assert sha(a.source)=='5d2e767a618a805d412f86c70be6f3c926897e5085621461390386f368e6d269'
assert sha(a.source.with_name('resource-input.bin'))=='d078631c7797b699200230a09533c9ad394a40fb9244fe0f75d905974ca49354'
assert sha(a.texture)==source['source_texture']['sha256']
baked=a.bake_report.parent
for n,h in b['baked_outputs'].items():assert Path(n).name==n and sha(baked/n)==h
record=json.loads((baked/'native-resource.json').read_text());assert record==b['actual_baked_record'] and record['driverID']==18
actual={k:record[k] for k in ('layout_UUID','device_UUID','driver_UUID')}
a.output.mkdir(parents=True)
entries=[{'source':str(a.source.resolve()),'texture':str(a.texture.resolve()),'baked':str(baked.resolve())}]
(a.output/'entries.json').write_text(json.dumps(entries,indent=2)+'\n')
# This is the existing strict packer and DMNRES1/DMNTBL2; no wire bytes patched.
cmd=[sys.executable,str(ROOT/'scripts/dev/pack-native-texture.py'),'--entries',str(a.output/'entries.json'),'--driver-manifest',str(a.driver_manifest),'--driver-query',str(baked/'native-resource.json'),'--output',str(a.output/'native-pack')]
subprocess.run(cmd,check=True)
pack=json.loads((a.output/'native-pack/native-pack.json').read_text());assert pack['complete'] and pack['family_protocol']==2 and len(pack['entries'])==1 and not pack['deployable_game_pack']
resources=a.output/'resources/resource4';resources.mkdir(parents=True)
files={}
for leaf,src in [('native-layouts.bin',a.output/'native-pack/native-layouts.bin'),('native-resource.bin',baked/'native-resource.bin'),('resource-input.bin',a.source.with_name('resource-input.bin'))]:
 dst=resources/leaf;shutil.copy2(src,dst);files[leaf]={'path':str(dst.resolve()),'sha256':sha(dst)}
old=json.loads(a.shader_inputs.read_text());shaders={}
keys=[hashlib.sha256(bytes([n])*0x880).hexdigest()[:16] for n in (3,4)]
for key in keys:
 for leaf,item in old['shaders'].items():
  if leaf.startswith(key+'.'):
   src=Path(item['path']);assert sha(src)==item['sha256'];dst=a.output/'vulkan-fixture'/leaf;dst.parent.mkdir(exist_ok=True);shutil.copy2(src,dst)
   if dst.suffix=='.spv':subprocess.run(['spirv-val','--target-env','vulkan1.1',str(dst)],check=True)
   shaders[leaf]={'path':str(dst.resolve()),'sha256':sha(dst)}
assert len(shaders)==8 and {k+'.spv' for k in keys}<=set(shaders)
inputs={'driver_sha256':b['driver_sha256'],'actual_identity':actual,'resources':{'resource4':{'files':files,'pack_sha256':pack['pack_content_sha256'],'producer_sha256':pack['producer_source_sha256']}},'shaders':shaders,'game_modules_executed':False,'runtime_game_deployment_allowed':False,'source_original_BC_to_ASTC_difference_explicit':True,'provenance':{'actual_bake_report':str(a.bake_report.resolve()),'actual_bake_report_sha256':sha(a.bake_report),'actual_driver_query_sha256':sha(baked/'native-resource.json'),'source_declaration_sha256':sha(a.source),'original_ASTC_texture_sha256':sha(a.texture),'pack_tool_sha256':sha(ROOT/'scripts/dev/pack-native-texture.py'),'bundle_tool_sha256':sha(__file__),'commands':[cmd]}}
(a.output/'fixture-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
print(a.output/'fixture-inputs.json')
