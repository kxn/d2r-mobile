#!/usr/bin/env python3
"""Compile production Store decision oracle; sanitizers/negative controls, no device."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess as S
R=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();O=a.output.resolve();O.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();source=R/'dm/gfx/vulkan/tests/store_attachment_domain_host.c'
base=['cc','-std=gnu11','-D_GNU_SOURCE','-DDM_VULKAN_TEST_EMBED','-I',str(R),'-I',str(R/'dm/gfx/vulkan'),'-I',str(R/'dm/gfx/nvn/include'),'-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-O1']
def command(label,argv,env=None):
 q=S.run(argv,text=True,capture_output=True,env=env);(O/(label+'.stdout')).write_text(q.stdout);(O/(label+'.stderr')).write_text(q.stderr);return {'argv':argv,'returncode':q.returncode,'stdout':q.stdout,'stderr':q.stderr}
cmd=base+['-MMD','-MF',str(O/'dependencies.d'),str(source),'-o',str(O/'store_domain_test')];c=command('compile',cmd);assert c['returncode']==0,c
clean=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1');run=command('run',[str(O/'store_domain_test')],clean);assert run['returncode']==0,run
cold=[]
for v,expected in [(None,0),('0',0),('1',1),('bad',None)]:
 env=dict(clean);env.pop('DM_VULKAN_STORE_ATTACHMENT_DOMAIN',None)
 if v is not None:env['DM_VULKAN_STORE_ATTACHMENT_DOMAIN']=v
 q=command('cold-'+str(v),[str(O/'store_domain_test'),str(expected or 0)],env);assert(q['returncode']==0)==(expected is not None),q;cold.append(q)
# Isolated source mutation controls. Main/runtime sources remain unchanged.
text=(R/'dm/gfx/vulkan/nvn_vk_store_index.inc').read_text();negative=[]
mutations={
 'stale-negative':('unsigned checked=proof&UINT32_C(0xffff);','unsigned checked=proof&UINT32_C(0xffff); if(checked && checked<domain_count)return 0;'),
 'forget-old-target':('    domain_enroll_targets(&ctx->rt);','    domain_count=0;memset(domain_proof,0,sizeof domain_proof);domain_enroll_targets(&ctx->rt);'),
 'false-known-zero':('        nonzero=1;','        nonzero=!build_domain;'),
}
for name,(old,new)in mutations.items():
 assert text.count(old)==1,(name,text.count(old));d=O/name;d.mkdir(exist_ok=True);(d/'nvn_vk_plan.c').write_bytes((R/'dm/gfx/vulkan/nvn_vk_plan.c').read_bytes());(d/'nvn_vk_store_index.inc').write_text(text.replace(old,new));binary=d/'oracle'
 q=command(name+'-compile',base+['-DDM_STORE_DOMAIN_PLAN_SOURCE="'+str(d/'nvn_vk_plan.c')+'"',str(source),'-o',str(binary)]);assert q['returncode']==0,q
 q=command(name+'-run',[str(binary)],clean);assert q['returncode']!=0 and 'Store domain oracle mismatch'in q['stderr'],q;negative.append({'mutation':name,'source_sha256':sha(d/'nvn_vk_store_index.inc'),'result':q})
deps=(O/'dependencies.d').read_text().replace('\\\n',' ').split(':',1)[1].split();result={'scope':'production C walk+Store with metadata fixtures; frozen full index oracle, valid prefix-target/start/done; host only, no GPU/Android/game/FPS claim','compile':c,'run':run,'cold':cold,'negative_controls':negative,'compiler_version':S.check_output(['cc','--version'],text=True),'sources':{str(Path(f).resolve()):sha(f)for f in deps if Path(f).is_file()},'script_sha256':sha(__file__)}
(O/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(run['stdout'].strip());print('PASS cold0/1/bad + three unsafe controls rejected by actual Store decision mismatch')
