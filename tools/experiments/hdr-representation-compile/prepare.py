#!/usr/bin/env python3
"""Recover this audit's actual PSOs with official GFXReconstruct extraction.

This is host-only artifact preparation. It does not reimplement capture,
Vulkan decoding, timing, compilation or shader translation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
CAPTURE_SHA='6496e7a9cc1b0402740075829bdf55694e4f770f71d6bd32ab55f616c4edadc1'
API_SHA='77ea2a86f4e95350ad88998760d91ccc34636aebb088eca194a345dfdd7e9b2d'
PAIRS=[('5c1ca4e7c3e4657c','54f730a6c0efc118'),('38f344adf801b153','af64b417689666bf')]
def sha(path):
 with Path(path).open('rb')as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--capture',type=Path,required=True);p.add_argument('--official-api',type=Path,required=True)
 p.add_argument('--base-cache',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--extractor',type=Path,default=ROOT/'.local/build/gfxreconstruct-host/tools/extract/gfxrecon-extract')
 a=p.parse_args();assert sha(a.capture)==CAPTURE_SHA and sha(a.official_api)==API_SHA,'This audit accepts its explicitly frozen official inputs only'
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);extracted=out/'gfxr-extracted-shaders'
 command=[str(a.extractor.resolve()),'--dir',str(extracted),str(a.capture.resolve())]
 with(out/'extract.stdout').open('w')as log,(out/'extract.stderr').open('w')as err:subprocess.run(command,stdout=log,stderr=err,check=True)
 modules={sha(p):p.name for p in extracted.iterdir()if p.is_file()};pairs=set();source_pairs=[]
 for fs,vs in PAIRS:
  fs_payload=(a.base_cache/(fs+'.spec')).read_bytes()[60:];vs_path=a.base_cache/(vs+'.spv')
  fs_hash=hashlib.sha256(fs_payload).hexdigest();vs_hash=sha(vs_path)
  assert fs_hash in modules and vs_hash in modules,'Current paired SPIR-V is absent from actual official capture'
  fs_handle=int(modules[fs_hash].removeprefix('sh'));vs_handle=int(modules[vs_hash].removeprefix('sh'));pairs.add((fs_handle,vs_handle));source_pairs.append({'fragment_key':fs,'vertex_key':vs,'fragment_sha256':fs_hash,'vertex_sha256':vs_hash,'actual_module_handles':[fs_handle,vs_handle]})
 objects={};matches=[]
 for line in a.official_api.open():
  row=json.loads(line);f=row.get('function',{});name=f.get('name');args=f.get('args',{})
  if name in ('vkCreatePipelineLayout','vkCreateDescriptorSetLayout','vkCreateRenderPass'):
   tag={'vkCreatePipelineLayout':'pPipelineLayout','vkCreateDescriptorSetLayout':'pSetLayout','vkCreateRenderPass':'pRenderPass'}[name];objects[name,args[tag]]=row
  if name!='vkCreateGraphicsPipelines':continue
  for info in args['pCreateInfos']:
   stages={s['stage']:s for s in info['pStages']}
   if not all(k in stages for k in ('VK_SHADER_STAGE_FRAGMENT_BIT','VK_SHADER_STAGE_VERTEX_BIT')):continue
   key=(stages['VK_SHADER_STAGE_FRAGMENT_BIT']['module'],stages['VK_SHADER_STAGE_VERTEX_BIT']['module'])
   if key not in pairs:continue
   layout=objects['vkCreatePipelineLayout',info['layout']];sets=layout['function']['args']['pCreateInfo']['pSetLayouts']
   result={'pipeline':{'index':row['index'],'info':info,'function':row},'layout':layout,
           'sets':[objects['vkCreateDescriptorSetLayout',s]for s in sets],
           'render_pass':objects['vkCreateRenderPass',info['renderPass']]}
   name=f'matched-pso-{len(matches)}.json';(out/name).write_text(json.dumps(result,indent=2)+'\n');matches.append(name)
 assert len(matches)==18,'Frozen capture PSO selection changed'
 (out/'preparation.json').write_text(json.dumps({'host_only':True,'command':command,'capture_sha256':sha(a.capture),'official_api_sha256':sha(a.official_api),
  'extractor_sha256':sha(a.extractor),'source_pairs':source_pairs,'actual_PSO_records':{n:sha(out/n)for n in matches}},indent=2)+'\n')
 print(out)
if __name__=='__main__':main()
