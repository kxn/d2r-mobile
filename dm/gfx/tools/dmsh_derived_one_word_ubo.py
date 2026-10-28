"""Verified child of an explicit combined-address cache: exact FS one-word CFG.

Only declared DSP payloads change. Their existing selector/header/base identity
are repacked, never rediscovered. A pinned, independently verified parent cache
is an explicit input to independent verification; no game binary is touched.
"""
import argparse,copy,hashlib,json,shutil,subprocess,tempfile,types
from pathlib import Path
import dmsh_derived_address_combined as P
import dmsh_derived_predicates as B
import dmsh_one_word_ubo as W

ROOT=Path(__file__).resolve().parents[3]
SCHEMA='dmsh-verified-cache-one-word-ubo-v2'
RECORD='_derived_build.json';PARENT='_oneword_parent_build.json';MANIFEST='_oneword_parent_manifest.json'
PARENT_VERIFIER='_oneword_parent_verifier.py'
CLOSURES=('json','dkey','dspan','plocal.json','plocal')
sha,artifact,require=B.sha,B.artifact,B.require

def tools():
    # Same official as/dis used for the actual four-PSO qualification. Their
    # versions are bound separately from the standard opt/val/reflect tools.
    arm=ROOT/'.local/tools/Arm_Performance_Studio_2026.5/renderdoc_for_arm_gpus/share/renderdoc/plugins/spirv'
    return {n:(arm/n if n in ('spirv-as','spirv-dis')and(arm/n).is_file()else Path(shutil.which(n)or'')) .resolve()
            for n in ('spirv-as','spirv-dis','spirv-opt','spirv-val')}

def source_identity():
    identity=P.source_identity()
    identity['source_files'].update({str(Path(p).resolve().relative_to(ROOT)):sha(p)for p in (__file__,W.__file__)})
    identity['oneword_tools']={n:{'path':str(p),'sha256':sha(p),'version':subprocess.check_output([str(p),'--version'],text=True,stderr=subprocess.STDOUT)}for n,p in tools().items()}
    z=Path(W.z3.__file__).resolve();lib=z.parent/'lib/libz3.so';require(lib.is_file(),'Missing Z3 proof runtime identity')
    identity['proof_solver']={'version':W.z3.get_version_string(),'python_init_sha256':sha(z),'native_library_sha256':sha(lib)}
    return identity

def qualified_parent_verifier(record,archived_source=None):
    """Execute exactly the parent's recorded verifier, not today's superset.

    Its self-source read normally sees the current checkout. The wrapper binds
    that ONE self entry to the exact bytes actually executed; every dependency,
    normalizer and official tool is still recomputed by the original function.
    """
    relative='dm/gfx/tools/dmsh_derived_address_combined.py'
    expected=record['identity']['sources']['source_files'][relative]
    source=archived_source
    if source is None:
        current=Path(P.__file__).read_bytes()
        if hashlib.sha256(current).hexdigest()==expected:source=current
        else:
            revisions=subprocess.check_output(['git','-C',str(ROOT),'log','--format=%H','--',relative],text=True).splitlines()
            for revision in revisions:
                blob=subprocess.check_output(['git','-C',str(ROOT),'show',revision+':'+relative])
                if hashlib.sha256(blob).hexdigest()==expected:source=blob;break
    require(source is not None and hashlib.sha256(source).hexdigest()==expected,'Exact recorded parent verifier source unavailable')
    module=types.ModuleType('dmsh_exact_parent_address_combined');module.__file__=str(ROOT/relative)
    exec(compile(source,module.__file__,'exec'),module.__dict__)
    original_source_identity=module.source_identity
    def executed_source_identity():
        value=original_source_identity();value['source_files'][relative]=hashlib.sha256(source).hexdigest();return value
    module.source_identity=executed_source_identity
    require(module.source_identity()==record['identity']['sources'],'Parent exact source/tool identity mismatch')
    return module,source


def parent_record(cache,expected_sha,expected_shaders,verifier_source=None):
    require(B.SHA.fullmatch(expected_sha),'Explicit complete parent record SHA256 required')
    require(sha(cache/RECORD)==expected_sha,'Parent record identity mismatch')
    record=json.loads((cache/RECORD).read_text());require(record.get('schema')==P.SCHEMA and record.get('status')=='built-not-deployed','Unsupported parent stage')
    require(set(B.files(cache))==set(record['artifacts'])|{RECORD},'Parent file set differs from declared artifacts')
    require(record['identity']['schema']==P.SCHEMA and qualified_parent_verifier(record,verifier_source)[0].source_identity()==record['identity']['sources'],'Parent source/tool identity mismatch')
    require(record['chain']==P.V1.identity_chain(record['identity'])==(cache/'.chain').read_text().strip(),'Parent chain identity mismatch')
    require(record['input_shaders_verified']==expected_shaders,'Parent shader count mismatch')
    return record

def selected(cache,key):
    meta=P.selected(cache,key)
    require(meta['stage']=='ps'and(cache/(key+'.spec')).is_file(),'One-word stage requires an existing fragment pair')
    return meta

def transform_pair(base,recipe):
    with tempfile.TemporaryDirectory(prefix='dmsh_oneword_transform_')as directory:
        tmp=Path(directory);src=tmp/'input.spv';src.write_bytes(recipe[60:]);dst=tmp/'candidate.spv';proof=W.compile_candidate(src,dst,tools());candidate=dst.read_bytes()
    rebuilt,repack=P.REPACK.rebuild_equivalent_recipe(base,recipe,base,candidate)
    return rebuilt,{'specialized_payload':proof,'recipe':repack,'base_unchanged':True,'selector_reselected':False}

def closures(folder,key,original,chain,proof):
    meta=copy.deepcopy(original);meta['chain']=chain
    meta['compiler_options']={**meta['compiler_options'],'one_word_ubo_branches':True};meta['one_word_ubo_branches']=proof
    meta['uniform_specialization']['base_sha256']=sha(folder/(key+'.spv'))
    meta['uniform_specialization']['specialization_sha256']=sha(folder/(key+'.spec'))
    meta.update(P.DK.emit_with_spans(folder,key,meta))
    if original['compiler_options'].get('pixel_local_reflection'):meta['pixel_local']=P.PL.emit(folder,key,meta)
    if original['compiler_options'].get('pixel_local_runtime'):meta['pixel_local_runtime']=P.PW.emit(folder,key,meta)
    (folder/(key+'.json')).write_text(json.dumps(meta,ensure_ascii=False,indent=1)+'\n')

def build(cache,parent_sha,keys,output_parent,report,dry_run=False,expected_shaders=6931):
    parent=parent_record(cache,parent_sha,expected_shaders)
    require(keys and len(set(keys))==len(keys)and all(B.KEY.fullmatch(k)for k in keys),'Unique selected shader keys required')
    require(all(k+'.spv'in parent['artifacts']for k in keys),'Selected key outside parent ledger')
    metas={k:selected(cache,k)for k in sorted(keys)}
    identity={'schema':SCHEMA,'parent_record_sha256':parent_sha,'parent_chain':parent['chain'],
              'original_input_receipt_sha256':parent['identity']['input_receipt_sha256'],'selection':sorted(keys),
              'sources':source_identity(),'contract':'one proved four-component readonly Uniform selector; nested DontFlatten branches; base/meta/selector unchanged; only existing DSP payload and dependent closures regenerated'}
    chain=P.V1.identity_chain(identity);out=output_parent/('vk-'+chain)
    require(not out.exists()and not out.resolve().is_relative_to(cache.resolve()),'New independent output cache required')
    record={'schema':SCHEMA,'identity':identity,'chain':chain,'output':str(out.resolve()),'parent_cache':str(cache.resolve()),'status':'plan'if dry_run else'building','candidate_game_performance':'NOT_YET_MEASURED','selected_keys':sorted(keys)}
    report.parent.mkdir(parents=True,exist_ok=True)
    if dry_run:report.write_text(json.dumps(record,indent=2)+'\n');return record
    # Do not repeatedly hash the complete cache before/after copying. The final
    # independent verify authenticates parent once and candidate once. Build
    # inherits untouched ledger entries and hashes only its changed/new files.
    out.mkdir(parents=True);subprocess.run(['cp','-a','--reflink=auto',str(cache)+'/.',str(out)],check=True)
    (out/PARENT).write_bytes((cache/RECORD).read_bytes());(out/MANIFEST).write_bytes((cache/'_vk_manifest.json').read_bytes());(out/PARENT_VERIFIER).write_bytes(qualified_parent_verifier(parent)[1])
    changed={PARENT,MANIFEST,PARENT_VERIFIER,'.chain','_vk_manifest.json'};proofs={}
    for k in sorted(keys):
        for suffix in ('spec','json'):
            name=k+'.oneword-input-'+suffix;(out/name).write_bytes((cache/(k+'.'+suffix)).read_bytes());changed.add(name)
        recipe,proof=transform_pair((cache/(k+'.spv')).read_bytes(),(cache/(k+'.spec')).read_bytes());(out/(k+'.spec')).write_bytes(recipe);proofs[k]=proof
        closures(out,k,metas[k],chain,proof);changed|={k+'.spec',*(k+'.'+s for s in CLOSURES)}
        print('Derived proved one-word paired fragment: '+k,flush=True)
    (out/'.chain').write_text(chain+'\n');manifest={'chain':chain,'expected':expected_shaders,'validated':expected_shaders,'target':'vulkan1.1','toolchain':'verified combined-address parent + one-word Uniform paired-payload CFG','derived_identity':identity,'parent_chain':parent['chain'],'oneword_proofs':proofs}
    (out/'_vk_manifest.json').write_text(json.dumps(manifest,indent=1)+'\n')
    require(source_identity()==identity['sources'],'Source/tool closure changed during derivation')
    artifacts=copy.deepcopy(parent['artifacts'])
    for name in changed:artifacts[name]=artifact(out/name)
    record.update(status='built-not-deployed',proofs=proofs,artifacts=artifacts,changed_artifacts={n:{'parent':parent['artifacts'].get(n),'derived':artifacts[n]}for n in sorted(changed)})
    (out/RECORD).write_text(json.dumps(record,indent=2)+'\n');record['independent_verification']=verify(out,parent_sha,cache,expected_shaders);report.write_text(json.dumps(record,indent=2)+'\n');return record

def verify(cache,expected_parent_sha,parent_cache,expected_shaders=6931):
    record=json.loads((cache/RECORD).read_text());identity=record['identity']
    require(record.get('schema')==identity.get('schema')==SCHEMA,'One-word schema mismatch')
    require(identity['parent_record_sha256']==expected_parent_sha==sha(cache/PARENT),'Pinned archived parent identity mismatch')
    parent=parent_record(parent_cache,expected_parent_sha,expected_shaders,(cache/PARENT_VERIFIER).read_bytes())
    exact_parent,_=qualified_parent_verifier(parent,(cache/PARENT_VERIFIER).read_bytes());parent_verification=exact_parent.verify(parent_cache,parent['identity']['input_receipt_sha256'],expected_shaders)
    require(identity['sources']==source_identity(),'One-word source/tool identity mismatch')
    chain=P.V1.identity_chain(identity);require(record['chain']==chain==(cache/'.chain').read_text().strip()and cache.name=='vk-'+chain,'One-word chain mismatch')
    require(identity['parent_chain']==parent['chain']and identity['original_input_receipt_sha256']==parent['identity']['input_receipt_sha256'],'Parent provenance mismatch')
    require((cache/PARENT).read_bytes()==(parent_cache/RECORD).read_bytes(),'Archived parent bytes mismatch')
    require(artifact(cache/MANIFEST)==parent['artifacts']['_vk_manifest.json'],'Archived parent manifest mismatch')
    keys=identity['selection'];require(keys and len(set(keys))==len(keys)and all(B.KEY.fullmatch(k)for k in keys)and set(keys)==set(record['proofs']),'Invalid one-word selection')
    mutable={'.chain','_vk_manifest.json'};newnames={PARENT,MANIFEST,PARENT_VERIFIER}
    for k in keys:
        mutable|={k+'.spec',*(k+'.'+s for s in CLOSURES)};newnames|={k+'.oneword-input-spec',k+'.oneword-input-json'}
    expected=record['artifacts'];require(set(expected)==set(parent['artifacts'])|newnames,'One-word mutation domain mismatch')
    require(set(B.files(cache))==set(expected)|{RECORD},'Undeclared one-word output')
    observed={n:artifact(cache/n)for n in expected}
    for n,info in expected.items():require(observed[n]==info,'One-word artifact mismatch: '+n)
    for n,info in parent['artifacts'].items():
        if n not in mutable:require(observed[n]==info,'Unrelated parent artifact changed: '+n)
    manifest=json.loads((cache/'_vk_manifest.json').read_text());require(manifest=={'chain':chain,'expected':expected_shaders,'validated':expected_shaders,'target':'vulkan1.1','toolchain':'verified combined-address parent + one-word Uniform paired-payload CFG','derived_identity':identity,'parent_chain':parent['chain'],'oneword_proofs':record['proofs']},'One-word manifest mismatch')
    require(len(list(cache.glob('*.spv')))==expected_shaders,'One-word base key set changed')
    for k in keys:
        for suffix in ('json','spec'):
            name=k+'.oneword-input-'+suffix;require(observed[name]==parent['artifacts'][k+'.'+suffix],'Archived selected parent input mismatch')
        meta=json.loads((cache/(k+'.oneword-input-json')).read_text());base=(cache/(k+'.spv')).read_bytes();recipe=(cache/(k+'.oneword-input-spec')).read_bytes()
        P.fragment_recipe_contract(meta,base,recipe);require(meta['stage']=='ps'and meta['descriptor_set']==1,'Fragment stage contract changed')
        rebuilt,proof=transform_pair(base,recipe);require(rebuilt==(cache/(k+'.spec')).read_bytes()and proof==record['proofs'][k],'One-word transform/selector recipe regeneration mismatch: '+k)
        with tempfile.TemporaryDirectory(prefix='dmsh_oneword_closures_')as d:
            tmp=Path(d)
            for suffix in ('spv','meta','spec'):shutil.copyfile(cache/(k+'.'+suffix),tmp/(k+'.'+suffix))
            closures(tmp,k,meta,chain,proof)
            for suffix in CLOSURES:require((tmp/(k+'.'+suffix)).read_bytes()==(cache/(k+'.'+suffix)).read_bytes(),'One-word closure regeneration mismatch: '+k+'.'+suffix)
    require(source_identity()==identity['sources'],'Source/tool closure changed during verification')
    return {'status':'passed-not-deployed','chain':chain,'parent_chain':parent['chain'],'parent_record_sha256':expected_parent_sha,'parent_independent_verification':parent_verification,'complete_shaders':expected_shaders,'output_artifacts_verified':len(expected),'selected_keys_regenerated':keys,'base_modules_metadata_and_selectors_unchanged':True,'unrelated_parent_artifacts_unchanged':len(parent['artifacts'])-len(mutable)}

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='operation',required=True)
    for op in ('plan','build'):
        s=sub.add_parser(op);s.add_argument('--parent-cache',type=Path,required=True);s.add_argument('--parent-record-sha256',required=True);s.add_argument('--keys',nargs='+',required=True);s.add_argument('--output-parent',type=Path,required=True);s.add_argument('--report',type=Path,required=True)
    s=sub.add_parser('verify');s.add_argument('--cache',type=Path,required=True);s.add_argument('--parent-cache',type=Path,required=True);s.add_argument('--parent-record-sha256',required=True);s.add_argument('--report',type=Path,required=True)
    a=p.parse_args(argv)
    if a.operation=='verify':r=verify(a.cache.resolve(),a.parent_record_sha256,a.parent_cache.resolve());a.report.write_text(json.dumps(r,indent=2)+'\n')
    else:r=build(a.parent_cache.resolve(),a.parent_record_sha256,a.keys,a.output_parent.resolve(),a.report.resolve(),a.operation=='plan')
    print(json.dumps({k:v for k,v in r.items()if k not in ('identity','artifacts','changed_artifacts','proofs')},indent=2))
if __name__=='__main__':main()
