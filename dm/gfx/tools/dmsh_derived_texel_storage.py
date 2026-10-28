"""Bounded paired texel representation over an authenticated derived parent.

Base/spec/meta and every unrelated resource stay byte-identical. Parent proof
rehydration uses the existing verifier; no original-proof flag is fabricated.
"""
import argparse,copy,hashlib,json,shutil,subprocess,tempfile,os,sys
from pathlib import Path
import dmsh_derived_predicates as B
import dmsh_derived_verify as P
import dmsh_texel_ssbo as T
SCHEMA='dmsh-verified-cache-texel-representation-v1'
RECORD='_derived_build.json';INPUT='_derived_input_receipt.json';BASE='_derived_base_manifest.json'
ARCHIVES={RECORD:'_texel_parent_build.json',INPUT:'_texel_parent_input_receipt.json',BASE:'_texel_parent_base_manifest.json'}
sha,artifact,require=B.sha,B.artifact,B.require

def sources():
 s=B.source_identity();s['source_files'].update({'dmsh_derived_texel_storage.py':sha(__file__),'dmsh_texel_ssbo.py':sha(T.__file__)})
 return s

def selected(cache,key):
 require(B.KEY.fullmatch(key),'invalid texel selection key')
 m=json.loads((cache/(key+'.json')).read_text())
 require(m.get('stage')=='ps' and m.get('descriptor_set')==1,'fragment scalar representation only')
 require(m['spirv_sha256']==sha(cache/(key+'.spv'))and m['metadata_sha256']==sha(cache/(key+'.meta')),'parent module/reflection hash')
 require(not any((cache/(key+'.'+e)).exists()for e in ['tssbo','pred']),'existing alternatives require separate composition')
 return m

def derive(cache,key):
 m=selected(cache,key);base=(cache/(key+'.spv')).read_bytes();sp=cache/(key+'.spec');spec=sp.read_bytes()if sp.exists()else None
 eligible={b['binding']for b in m['textures']if b['type']&0xff==2 and b['array']<=1}
 payload,proof=T.pair(base,spec,eligible,1,T.IDENTITY_MODEL);require(payload is not None,'no admissible texel alternative')
 T.unpack_pair(payload,base,spec,proof)
 return payload,proof,m

def closure(meta,chain,proof):
 m=copy.deepcopy(meta);m['chain']=chain;m['compiler_options']={**m['compiler_options'],'texel_storage_alternatives':True};m['texel_storage_alternative']=proof
 return json.dumps(m,ensure_ascii=False,indent=1)+'\n'

def clone(a,b):subprocess.run(['cp','-a','--reflink=auto',str(a)+'/.',str(b)],check=True)

def verify_parent(cache,input_sha,expected_shaders):
    record=json.loads((cache/RECORD).read_text())
    require(record['schema']=='dmsh-verified-cache-address-combined-v2','only the declared combined parent is admitted')
    old=record['identity']['sources']['source_files'];name='dm/gfx/tools/dmsh_derived_address_combined.py'
    if sha(B.ROOT/name)==old[name]:return P.verify(cache,input_sha,expected_shaders)
    # The optional-unpaired enhancement changed the verifier source fingerprint
    # after the already authenticated paired cache was made. Never relabel it.
    # Reexecute its exact recorded source revision in an isolated source tree.
    require(old[name]=='86c34de4b282bba14b01dc5c3a379bc19b49d7e6087544a3c88ef1bfb59ceee9','unknown historical combined verifier')
    source=subprocess.check_output(['git','-C',str(B.ROOT),'show','111154fc2cc23e26aff0e2d5f7c080154ddf1d09:'+name])
    require(hashlib.sha256(source).hexdigest()==old[name],'historic Git source fingerprint')
    with tempfile.TemporaryDirectory(prefix='dmsh_texel_original_verifier_')as tmp:
        root=Path(tmp);tools=root/'dm/gfx/tools';tools.mkdir(parents=True)
        for p in (B.ROOT/'dm/gfx/tools').glob('*.py'):shutil.copyfile(p,tools/p.name)
        for rel,h in old.items():
            target=root/rel;target.parent.mkdir(parents=True,exist_ok=True)
            if rel==name:target.write_bytes(source)
            else:
                require(sha(B.ROOT/rel)==h,'parent tool closure changed beyond pinned historic verifier: '+rel)
                shutil.copyfile(B.ROOT/rel,target)
            require(sha(target)==h,'frozen original parent source')
        code='from pathlib import Path;import json,dmsh_derived_verify as V;print(json.dumps(V.verify(Path('+repr(str(cache.resolve()))+'),'+repr(input_sha)+','+repr(expected_shaders)+')))'
        run=subprocess.run([sys.executable,'-c',code],cwd=root,env=dict(os.environ,PYTHONPATH=str(tools)),capture_output=True,text=True,check=True)
        result=json.loads(run.stdout.strip().splitlines()[-1]);result['historical_source_reexecuted_without_relabeling']=True;result['historical_commit']='111154fc2cc23e26aff0e2d5f7c080154ddf1d09';return result

def build(cache,receipt,input_sha,keys,parent,report,expected_shaders=6931):
 require(sha(receipt)==input_sha,'explicit parent receipt hash')
 rec=json.loads(receipt.read_text());require(rec.get('verified_cache_derived') is True and rec['validated_game_and_builtin_shaders']==expected_shaders,'authenticated derived-parent receipt required')
 B.check_artifacts(cache,rec['artifacts']);pr=json.loads((cache/RECORD).read_text());require(pr['schema']!=SCHEMA,'nested texel stage not admitted')
 parent_input=pr['identity']['input_receipt_sha256'];validation=verify_parent(cache,parent_input,expected_shaders)
 require(validation['chain']==rec['chain']==(cache/'.chain').read_text().strip(),'parent chain identity')
 require(1<=len(keys)<=8 and len(set(keys))==len(keys),'bounded distinct fragment selection')
 payloads={k:derive(cache,k)for k in sorted(keys)}
 identity={'schema':SCHEMA,'input_receipt_sha256':input_sha,'base_chain':rec['chain'],'parent_record_sha256':sha(cache/RECORD),'parent_input_receipt_sha256':parent_input,'selected_keys':sorted(keys),'sources':sources(),'contract':'original base/spec/metadata unchanged; readonly float4 alternative with runtime RGBA32F/stride16/exact range/format/offset proof; identity bit-model only'}
 chain=hashlib.sha256(B.canonical(identity)).hexdigest()[:12];out=parent/('vk-'+chain);require(not out.exists()and not out.resolve().is_relative_to(cache.resolve()),'new independent output required');out.mkdir(parents=True);clone(cache,out)
 for a,b in ARCHIVES.items():shutil.copyfile(out/a,out/b)
 shutil.copyfile(out/'_vk_manifest.json',out/BASE);shutil.copyfile(receipt,out/INPUT);(out/RECORD).unlink();proofs={}
 for k,(data,proof,m)in payloads.items():
  shutil.copyfile(cache/(k+'.json'),out/(k+'.texel-input-json'));(out/(k+'.tssbo')).write_bytes(data);(out/(k+'.json')).write_text(closure(m,chain,proof));proofs[k]=proof
 (out/'.chain').write_text(chain+'\n');manifest=json.loads((cache/'_vk_manifest.json').read_text());(out/'_vk_manifest.json').write_text(json.dumps({'chain':chain,'expected':expected_shaders,'validated':expected_shaders,'target':manifest['target'],'derived_identity':identity,'toolchain':'verified derived parent + original readonly texel pair adapter','base_compiler_options':manifest.get('base_compiler_options',manifest.get('compiler_options')),'texel_proofs':proofs},indent=1)+'\n')
 require(sources()==identity['sources'],'tool/source changed during pair derivation')
 record={'schema':SCHEMA,'identity':identity,'chain':chain,'status':'built-not-deployed','texel_proofs':proofs,'parent_verification':validation,'artifacts':{n:artifact(p)for n,p in sorted(B.files(out).items())},'performance_baseline_eligible':True,'candidate_game_performance':'NOT_MEASURED'}
 (out/RECORD).write_text(json.dumps(record,indent=2)+'\n');result=verify(out,input_sha,expected_shaders);report.write_text(json.dumps({'output':str(out.resolve()),**result},indent=2)+'\n');return out,result

def verify(cache,input_sha,expected_shaders=6931):
 record=json.loads((cache/RECORD).read_text());i=record['identity'];require(record['schema']==i['schema']==SCHEMA,'texel stage schema');require(i['sources']==sources(),'tool/source fingerprint');require(sha(cache/INPUT)==input_sha==i['input_receipt_sha256'],'parent receipt identity')
 chain=hashlib.sha256(B.canonical(i)).hexdigest()[:12];require(chain==record['chain']==(cache/'.chain').read_text().strip()and cache.name=='vk-'+chain,'texel chain identity')
 receipt=json.loads((cache/INPUT).read_text());parent=receipt['artifacts'];require(receipt.get('verified_cache_derived')is True and receipt['validated_game_and_builtin_shaders']==expected_shaders and receipt['chain']==i['base_chain'],'parent receipt contract')
 actual=B.files(cache);require(set(actual)==set(record['artifacts'])|{RECORD},'undeclared paired output')
 for n,a in record['artifacts'].items():require(artifact(actual[n])==a,'paired output hash: '+n)
 keys=i['selected_keys'];require(1<=len(keys)<=8 and len(set(keys))==len(keys)and set(keys)==set(record['texel_proofs']),'texel selection set')
 new=set(ARCHIVES.values())|{k+'.tssbo'for k in keys}|{k+'.texel-input-json'for k in keys};mutable={RECORD,INPUT,BASE,'.chain','_vk_manifest.json'}|{k+'.json'for k in keys};require(set(actual)==set(parent)|new,'paired mutation domain')
 for n,a in parent.items():
  if n not in mutable:require(artifact(cache/n)==a,'unrelated parent artifact changed: '+n)
 for old,arch in ARCHIVES.items():require(artifact(cache/arch)==parent[old],'archived parent identity: '+old)
 require(sha(cache/ARCHIVES[RECORD])==i['parent_record_sha256'],'parent record binding')
 for k in keys:require(artifact(cache/(k+'.texel-input-json'))==parent[k+'.json'],'original selected reflection binding')
 # Reconstruct only a private filesystem view of the original parent. The
 # existing verifier rechecks its complete immutable source/proof contracts.
 with tempfile.TemporaryDirectory(prefix='dmsh_texel_parent_')as tmp:
  restored=Path(tmp)/('vk-'+i['base_chain']);restored.mkdir();clone(cache,restored)
  preserved={old:(restored/arch).read_bytes()for old,arch in ARCHIVES.items()};preserved['_vk_manifest.json']=(cache/BASE).read_bytes();preserved['.chain']=(i['base_chain']+'\n').encode();preserved.update({k+'.json':(cache/(k+'.texel-input-json')).read_bytes()for k in keys})
  for n in new:(restored/n).unlink()
  for n,data in preserved.items():(restored/n).write_bytes(data)
  parent_validation=verify_parent(restored,i['parent_input_receipt_sha256'],expected_shaders)
  for k in keys:
   payload,proof,m=derive(restored,k);require(payload==(cache/(k+'.tssbo')).read_bytes()and proof==record['texel_proofs'][k],'paired exact regeneration: '+k);require(closure(m,chain,proof)==(cache/(k+'.json')).read_text(),'paired reflection closure: '+k)
 manifest=json.loads((cache/'_vk_manifest.json').read_text());require(manifest['chain']==chain and manifest['derived_identity']==i and manifest['expected']==manifest['validated']==expected_shaders and manifest['texel_proofs']==record['texel_proofs'],'paired manifest binding')
 require(sources()==i['sources'],'source changed during verification')
 return {'status':'passed-not-deployed','chain':chain,'complete_shaders':expected_shaders,'selected_keys_regenerated':keys,'output_artifacts_verified':len(record['artifacts']),'input_receipt_sha256':input_sha,'all_base_spec_meta_unchanged':True,'parent_verified_by_original_verifier':parent_validation['derived_schema']}

def main():
 p=argparse.ArgumentParser();p.add_argument('--cache',type=Path,required=True);p.add_argument('--input-receipt',type=Path,required=True);p.add_argument('--input-receipt-sha256',required=True);p.add_argument('--keys',nargs='+',required=True);p.add_argument('--output-parent',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args();a.output_parent.mkdir(parents=True,exist_ok=True);out,result=build(a.cache,a.input_receipt,a.input_receipt_sha256,a.keys,a.output_parent,a.report);print(json.dumps({'output':str(out),**result}))
if __name__=='__main__':main()
