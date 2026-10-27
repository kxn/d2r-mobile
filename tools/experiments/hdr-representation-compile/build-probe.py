#!/usr/bin/env python3
"""Compile-only matched HDR representation probe, reusing qualified loader/C body.

PSO states come from official GFXReconstruct extraction/JSON, whose four actual
shader hashes match current draw-linked sources. No draw, queue submit or timer.
"""
import argparse,copy,hashlib,json,re,shutil,subprocess as S,sys,zipfile
from pathlib import Path
R=Path(__file__).resolve().parents[3];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(R/'dm/gfx/tools'))
import dmsh_texel_ssbo as T
import dmsh_uniform_predicates as P
import dmsh_vulkan as V
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
A=R/'.local/logs/hdr-representation-compile-v1'
B=R/'.local/build/adreno-shader-upstream-rewrites-v2/vk-29e56b51bce5'
D=R/'.local/build/current-dpr1-observe-derived-v1/vk-e9ce329d7e17'

class Expr(str):pass
POINTERS={
 ('VkDescriptorSetLayoutCreateInfo','pBindings'):('VkDescriptorSetLayoutBinding','array'),
 ('VkPipelineLayoutCreateInfo','pPushConstantRanges'):('VkPushConstantRange','array'),
 ('VkRenderPassCreateInfo','pAttachments'):('VkAttachmentDescription','array'),
 ('VkRenderPassCreateInfo','pSubpasses'):('VkSubpassDescription','array'),
 ('VkSubpassDescription','pColorAttachments'):('VkAttachmentReference','array'),
 ('VkSubpassDescription','pDepthStencilAttachment'):('VkAttachmentReference','one'),
 ('VkPipelineVertexInputStateCreateInfo','pVertexBindingDescriptions'):('VkVertexInputBindingDescription','array'),
 ('VkPipelineVertexInputStateCreateInfo','pVertexAttributeDescriptions'):('VkVertexInputAttributeDescription','array'),
 ('VkPipelineColorBlendStateCreateInfo','pAttachments'):('VkPipelineColorBlendAttachmentState','array'),
 ('VkPipelineDynamicStateCreateInfo','pDynamicStates'):('VkDynamicState','array'),
 ('VkGraphicsPipelineCreateInfo','pStages'):('VkPipelineShaderStageCreateInfo','array'),
 ('VkPipelineShaderStageCreateInfo','pSpecializationInfo'):('VkSpecializationInfo','one'),
 ('VkSpecializationInfo','pMapEntries'):('VkSpecializationMapEntry','array'),
 ('VkSpecializationInfo','pData'):('uint8_t','array'),
}
STATES={'pVertexInputState':'VkPipelineVertexInputStateCreateInfo','pInputAssemblyState':'VkPipelineInputAssemblyStateCreateInfo',
 'pViewportState':'VkPipelineViewportStateCreateInfo','pRasterizationState':'VkPipelineRasterizationStateCreateInfo',
 'pMultisampleState':'VkPipelineMultisampleStateCreateInfo','pDepthStencilState':'VkPipelineDepthStencilStateCreateInfo',
 'pColorBlendState':'VkPipelineColorBlendStateCreateInfo','pDynamicState':'VkPipelineDynamicStateCreateInfo'}
POINTERS.update({('VkGraphicsPipelineCreateInfo',k):(v,'one')for k,v in STATES.items()})
class Emitter:
 def __init__(self):self.lines=[];self.serial=0
 def literal(self,value):
  if isinstance(value,Expr):return str(value)
  if value is None:return 'NULL'
  if isinstance(value,bool):return 'VK_TRUE'if value else'VK_FALSE'
  if isinstance(value,(int,float)):return repr(value)
  if isinstance(value,str):
   if value=='main':return '"main"'
   if not re.fullmatch(r'(?:VK_[A-Z0-9_]+|0x[0-9a-fA-F]+)(?:\|VK_[A-Z0-9_]+)*',value):raise ValueError('Unexpected C enum '+value)
   return value
  if isinstance(value,list):return '{'+','.join(self.literal(x)for x in value)+'}'
  if isinstance(value,dict):return '{'+','.join('.'+k+'='+self.literal(v)for k,v in value.items())+'}'
  raise ValueError(type(value))
 def initializer(self,type_name,obj):
  result={}
  for key,value in obj.items():
   if key=='pNext' and value is not None:raise ValueError('Unsupported captured pNext')
   if value is not None and not isinstance(value,Expr) and (type_name,key)in POINTERS:
    typ,mode=POINTERS[type_name,key];self.serial+=1;n='p'+str(self.serial)
    self.declare(typ,n,value,array=mode=='array');result[key]=Expr(n if mode=='array'else'&'+n)
   else:
    if re.match(r'p[A-Z]',key) and value is not None and not isinstance(value,Expr) and key not in ('pName',):raise ValueError('Unknown pointer '+type_name+'.'+key)
    result[key]=value
  return self.literal(result)
 def declare(self,typ,name,obj,array=False):
  if array:
   values=[self.initializer(typ,x)if isinstance(x,dict)else self.literal(x)for x in obj]
   self.lines.append(typ+' '+name+'['+str(len(values))+']={'+','.join(values)+'};')
  else:self.lines.append(typ+' '+name+'='+self.initializer(typ,obj)+';')

def original_match(index,fs_key,vs_key):
 p=A/f'matched-pso-{index}.json';r=json.loads(p.read_text());info=r['pipeline']['info'];stage={x['stage']:x for x in info['pStages']}
 fs=(B/(fs_key+'.spec')).read_bytes()[60:];vs=(B/(vs_key+'.spv')).read_bytes()
 # Verify against official extractor output, not just legacy opaque module IDs.
 for data,kind in ((fs,'VK_SHADER_STAGE_FRAGMENT_BIT'),(vs,'VK_SHADER_STAGE_VERTEX_BIT')):
  handle=stage[kind]['module'];assert data==(A/'gfxr-extracted-shaders'/('sh'+str(handle))).read_bytes()
 assert stage['VK_SHADER_STAGE_FRAGMENT_BIT']['pSpecializationInfo']['pData']==[1,0,0,0]
 assert info['stageCount']==2 and len(r['sets'])==2
 assert [x['format']for x in r['render_pass']['function']['args']['pCreateInfo']['pAttachments']]==['VK_FORMAT_B10G11R11_UFLOAT_PACK32','VK_FORMAT_D24_UNORM_S8_UINT']
 return r

def generate_pipeline(case,match):
 e=Emitter();e.lines.append('static void compile_case_'+str(case['index'])+'(VkDevice device,const char *dir){')
 e.lines.append('VkDescriptorSetLayout sets[2];')
 for n,set_record in enumerate(match['sets']):
  info=copy.deepcopy(set_record['function']['args']['pCreateInfo'])
  if case['representation']=='identity':
   b=next(b for b in info['pBindings']if b['binding']==2)if n==1 else None
   if b:assert b['descriptorType']=='VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER';b['descriptorType']='VK_DESCRIPTOR_TYPE_STORAGE_BUFFER'
  e.declare('VkDescriptorSetLayoutCreateInfo','set'+str(n),info)
  e.lines.append(f'CHECK(vkCreateDescriptorSetLayout(device,&set{n},NULL,&sets[{n}]));')
 info=copy.deepcopy(match['layout']['function']['args']['pCreateInfo']);info['pSetLayouts']=Expr('sets')
 e.declare('VkPipelineLayoutCreateInfo','layout_info',info);e.lines.append('VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&layout_info,NULL,&layout));')
 e.declare('VkRenderPassCreateInfo','render_info',match['render_pass']['function']['args']['pCreateInfo']);e.lines.append('VkRenderPass pass;CHECK(vkCreateRenderPass(device,&render_info,NULL,&pass));')
 e.lines.append('VkShaderModule vs=module(device,dir,"'+case['vs_file']+'"),fs=module(device,dir,"'+case['file']+'");')
 info=copy.deepcopy(match['pipeline']['info']);info['layout']=Expr('layout');info['renderPass']=Expr('pass')
 for stage in info['pStages']:
  if stage['stage']=='VK_SHADER_STAGE_VERTEX_BIT':stage['module']=Expr('vs')
  else:
   stage['module']=Expr('fs')
   if case['predicate_mask']is not None:
    spec=stage['pSpecializationInfo'];spec['pMapEntries'].append({'constantID':case['predicate_id'],'offset':4,'size':4});spec['mapEntryCount']=2;spec['dataSize']=8;spec['pData']+=[case['predicate_mask'],0,0,0]
 e.declare('VkGraphicsPipelineCreateInfo','pipeline_info',info)
 e.lines.append('VkPipeline pipeline;CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));')
 e.lines.append('vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,vs,NULL);vkDestroyShaderModule(device,fs,NULL);vkDestroyRenderPass(device,pass,NULL);vkDestroyPipelineLayout(device,layout,NULL);for(unsigned n=0;n<2;n++)vkDestroyDescriptorSetLayout(device,sets[n],NULL);}')
 return '\n'.join(e.lines)

def main():
 global A,B,D
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--driver-build',type=Path,required=True);p.add_argument('--inputs-root',type=Path,default=A);p.add_argument('--base-cache',type=Path,default=B);p.add_argument('--predicate-cache',type=Path,default=D);a=p.parse_args();A=a.inputs_root.resolve();B=a.base_cache.resolve();D=a.predicate_cache.resolve()
 O=a.output.resolve();O.mkdir(parents=True,exist_ok=False);source=O/'source';source.mkdir();commands=[]
 sdk=R/'.local/android-sdk';build=sdk/'build-tools/36.0.0';ndk=sdk/'ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64';jar=sdk/'platforms/android-36/android.jar'
 dr=json.loads((a.driver_build/'build.json').read_text());driver=Path(dr['driver']['path']);assert dr['complete']and sha(driver)==dr['driver']['sha256']=='12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5'
 original=R/'tools/experiments/surface-control-ahb/fixture';ubosource=R/'tools/experiments/spirv-ubo-address-normalize/probe.c';normal=R/'.local/build/internal-queue-present-v3/canonical.apk';assert sha(normal)=='28caae83d9b5e05a1b175c4c1989964b995061fbba7aed3537c980016f7acdbc'
 original_c=(original/'probe.c').read_text();start=original_c.index('  REQUIRE(snprintf(driver_root,');end=original_c.index('volkInitializeCustom(get);',start)+len('volkInitializeCustom(get);');(source/'loader.inc').write_text(original_c[start:end]+'\n')
 java=(original/'ProbeActivity.java').read_text().replace('org.kxn.scahb','org.kxn.hdrcompile').replace('loadLibrary("scahbprobe")','loadLibrary("hdrcompile")')
 needle='        SurfaceView view = new SurfaceView(this);';assert needle in java
 java=java.replace(needle,'''        try { for (String leaf : getAssets().list("shaders")) {
            try (InputStream in=getAssets().open("shaders/"+leaf); FileOutputStream out=new FileOutputStream(new File(getFilesDir(),leaf))) {
                byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1)out.write(b,0,n);
            }} } catch (Exception e) { throw new IllegalStateException(e); }
'''+needle)
 (source/'ProbeActivity.java').write_text(java);(source/'AndroidManifest.xml').write_text((original/'AndroidManifest.xml').read_text().replace('org.kxn.scahb','org.kxn.hdrcompile').replace('SurfaceControl AHB Fixture','D2R Mobile'))
 manifest=json.loads((D/'_vk_manifest.json').read_text());cases=[];payloads={};proofs={};matches={}
 for key,vs_key,index in [('5c1ca4e7c3e4657c','54f730a6c0efc118',0),('38f344adf801b153','af64b417689666bf',2)]:
  match=original_match(index,key,vs_key);matches[key]=match
  base=(B/(key+'.spv')).read_bytes();recipe=(B/(key+'.spec')).read_bytes();P._old_recipe(base,recipe);raw=recipe[60:]
  m=json.loads((B/(key+'.json')).read_text());eligible={x['binding']for x in m['textures']if x['type']&255==2 and x['array']<=1}
  blob,tp=T.pair(base,recipe,eligible,1,T.IDENTITY_MODEL);assert blob is not None;alt,identity,bindings=T.unpack_pair(blob,base,recipe,tp);assert bindings==[2]
  predproof=manifest['predicate_proof_overlays'][key]['uniform_predicates'];predicate,_=P.unpack((D/(key+'.pred')).read_bytes(),base,recipe,predproof)
  inp=source/(key+'.input');inp.write_bytes(raw);norm,np=V.normalize_ubo_address(inp);assert np['changed_shift_results']==23
  vs_file=vs_key+'.spv';payloads[vs_file]=(B/(vs_key+'.spv')).read_bytes()
  choices=[('original',raw,None),('identity',identity,None),('predicate-mask3',predicate,3),('normalized',norm,None)]
  if key.startswith('5c'):choices.append(('predicate-mask2',predicate,2))
  for role,data,mask in choices:
   file=f'case-{len(cases)}.spv';payloads[file]=data
   tmp=source/file;tmp.write_bytes(data);S.run(['spirv-val','--target-env','vulkan1.1',str(tmp)],check=True,capture_output=True)
   reflection=json.loads(S.check_output(['spirv-cross',str(tmp),'--reflect']));(source/(file+'.reflection.json')).write_text(json.dumps(reflection,indent=2)+'\n')
   cases.append({'index':len(cases),'label':key+'-'+role,'shader_key':key,'representation':'identity'if role=='identity'else role,'file':file,'sha256':hashlib.sha256(data).hexdigest(),'vs_file':vs_file,'vs_sha256':sha(B/vs_file),'spec0':1,'predicate_id':predproof['spec_id']if mask is not None else None,'predicate_mask':mask,'gfxr_original_pipeline_index':match['pipeline']['index']})
  proofs[key]={'identity':tp,'predicate':predproof,'normalization':np,'original_spec_sha256':sha(B/(key+'.spec')),'original_spv_sha256':hashlib.sha256(raw).hexdigest(),'baseline_native_required':{'instructions':2096 if key.startswith('5c')else 2124,'full_registers':19,'max_waves':10},'original_shader_key':key}
 (source/'cases.h').write_text('static const char *case_labels['+str(len(cases))+']={'+','.join(json.dumps(c['label'])for c in cases)+'};\n')
 generated='\n'.join(generate_pipeline(c,matches[c['shader_key']])for c in cases)
 generated+='\nstatic void compile_one(VkDevice device,const char *dir,unsigned n){switch(n){'+''.join(f'case {c["index"]}:compile_case_{c["index"]}(device,dir);break;'for c in cases)+'default:abort();}}\n';(source/'pipelines.inc').write_text(generated)
 c=ubosource.read_text();s=c.index('  VkDescriptorSetLayout sets[2];');e=c.index('vkDestroyDevice(device,NULL);',s)
 c=c[:s]+f'''  for(unsigned n=0;n<{len(cases)};n++){{fprintf(stderr,"HDRCOMPILE_CASE index=%u label=%s\\n",n,case_labels[n]);fflush(stderr);compile_one(device,dir,n);fprintf(stderr,"HDRCOMPILE_CREATED index=%u label=%s\\n",n,case_labels[n]);}}
  '''+c[e:]
 c=c.replace('JNIEXPORT void JNICALL Java_org_kxn_ubocompile_ProbeActivity_render','\n#include "pipelines.inc"\n\nJNIEXPORT void JNICALL Java_org_kxn_ubocompile_ProbeActivity_render')
 c=c.replace('Java_org_kxn_ubocompile_','Java_org_kxn_hdrcompile_').replace('UBOCOMPILE','HDRCOMPILE').replace('pipelines=4','pipelines='+str(len(cases))).replace('UBO address compiler qualification','Matched HDR representation compilation')
 (source/'probe.c').write_text(c)
 def run(*parts):
  cmd=list(map(str,parts));commands.append(cmd);S.run(cmd,check=True)
 run(ndk/'bin/aarch64-linux-android29-clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-D_DEFAULT_SOURCE','-pthread','-fPIC','-shared','-I',R/'dm/third_party/volk','-I',R/'dm/third_party/libadrenotools/include','-I',R/'dm/host/android','-I',source,source/'probe.c',R/'dm/third_party/volk/volk.c','-landroid','-ldl','-Wl,-z,max-page-size=16384','-o',O/'libhdrcompile.so')
 classes=O/'classes';dex=O/'dex';classes.mkdir();dex.mkdir();run('javac','--release','8','-classpath',jar,'-d',classes,source/'ProbeActivity.java');run(build/'d8','--lib',jar,'--min-api','36','--output',dex,*sorted(classes.rglob('*.class')));run(build/'aapt2','link','-I',jar,'--manifest',source/'AndroidManifest.xml','-o',O/'unsigned.apk')
 hook_hashes={}
 with zipfile.ZipFile(normal)as archive,zipfile.ZipFile(O/'unsigned.apk','a',compression=zipfile.ZIP_DEFLATED)as apk:
  for leaf in ['libadrenotools.so','libmain_hook.so','libhook_impl.so','libfile_redirect_hook.so','libgsl_alloc_hook.so']:
   name='lib/arm64-v8a/'+leaf;data=archive.read(name);hook_hashes[leaf]=hashlib.sha256(data).hexdigest();apk.writestr(name,data)
  apk.write(O/'libhdrcompile.so','lib/arm64-v8a/libhdrcompile.so');apk.write(dex/'classes.dex','classes.dex');apk.write(driver,'assets/driver.so');apk.writestr('assets/driver.sha256',sha(driver))
  for leaf,data in payloads.items():apk.writestr('assets/shaders/'+leaf,data)
 run(build/'zipalign','-f','-P','16','4',O/'unsigned.apk',O/'aligned.apk');apk=O/'compile-probe.apk';run(build/'apksigner','sign','--ks',R/'.local/build/device-probe/debug.keystore','--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',apk,O/'aligned.apk');run(build/'apksigner','verify','--verbose',apk);run(build/'zipalign','-c','-P','16','4',apk)
 source_files=[Path(__file__),ubosource,original/'probe.c',original/'ProbeActivity.java',original/'AndroidManifest.xml',R/'dm/host/android/stderr_redirect.h',Path(T.__file__),Path(P.__file__),Path(V.__file__),V.ubo_address_normalizer_source(),R/'dm/third_party/volk/volk.h',R/'dm/third_party/volk/volk.c',*source.iterdir()]
 record={'complete':True,'APK':{'path':str(apk),'sha256':sha(apk)},'package':'org.kxn.hdrcompile','activity':'.ProbeActivity','driver':dr['driver'],'driver_build_sha256':sha(a.driver_build/'build.json'),'hooks_from_exact_normal_v3':hook_hashes,'cases':cases,'proofs':proofs,'source_inputs':{str(p):sha(p)for p in source_files},'payloads':{name:{'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}for name,data in payloads.items()},'exact_gfxr_pipeline_states':matches,'original_GFXR_capture_sha256':'6496e7a9cc1b0402740075829bdf55694e4f770f71d6bd32ab55f616c4edadc1','official_API_sha256':sha(R/'.local/logs/inline-upstream-rootcause-v1/api-tools/own/official-api-frame/official-api.jsonl'),'scope':'Nine PSO creations only; no draw/submit/timer. Same actual VS+interface+RT+Spec0=1 per family. Baseline must reproduce current native2096/2124,19full,10waves before interpreting candidates. Original GFXR is older same-SPIRV workload, not asserted to be latest game state. Float4/range/alias/sNaN runtime equivalence is NOT tested by this compile fixture.','commands':commands}
 (O/'build.json').write_text(json.dumps(record,indent=2)+'\n');print(apk)
if __name__=='__main__':main()
