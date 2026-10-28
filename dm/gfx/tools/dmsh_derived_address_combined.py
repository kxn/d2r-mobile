"""Verified original-cache stage: exact address form in VS and optional FS pairs.

All proof/closure regeneration is explicit. Original Uniform selector tuples
are preserved by the declared DSP container repacker, not rediscovered.
"""
import argparse,copy,hashlib,json
from pathlib import Path
import shutil,subprocess,tempfile
import dmsh_derived_predicates as B
import dmsh_derived_ubo_address as V1
import dmsh_driver_keys as DK
import dmsh_pixel_local as PL
import dmsh_pixel_local_wire as PW
import dmsh_uniform_predicates as UP
import dmsh_uniform_specialization_repack as REPACK

sha,artifact,require=B.sha,B.artifact,B.require
ROOT=Path(__file__).resolve().parents[3]
SCHEMA='dmsh-verified-cache-address-combined-v2'
RECORD='_derived_build.json';INPUT='_derived_input_receipt.json';BASE='_derived_base_manifest.json'
COMMON=('spv','json','dkey','dspan','plocal.json','plocal')


def fragment_recipe_contract(meta,base,recipe):
    """An originally unpaired FS stays unpaired; never invent a selector.

    The original producer can explicitly report that it skipped specialization.
    Only its diagnostic-only shape is accepted without a recipe. Missing a
    declared pair or retaining selector/hash fields is still a hard failure.
    The archived original receipt independently fixes whether a file existed.
    """
    if recipe is None:
        if 'uniform_specialization' not in meta:return
        proof=meta['uniform_specialization']
        require(isinstance(proof,dict) and
                bool(proof.get('skipped')) and isinstance(proof.get('skipped'),str) and
                set(proof)<={'skipped','weight'} and
                ('weight'not in proof or type(proof['weight'])is int and proof['weight']>=0),
                'Fragment requires its original declared Uniform specialization; '
                'unpaired fragment has residual or malformed specialization metadata')
        return
    proof=meta.get('uniform_specialization')
    require(isinstance(proof,dict) and 'skipped'not in proof and
            proof.get('specialization_sha256')==hashlib.sha256(recipe).hexdigest(),
            'Fragment requires the original declared Uniform specialization')
    h=UP._old_recipe(base,recipe)
    require((h[2],h[3],h[4],h[5])==(proof.get('binding'),proof.get('reg'),proof.get('offset'),proof.get('spec_id')),
            'Original Uniform selector declaration mismatch')


def source_identity():
    result=V1.source_identity()
    result['source_files'].update({str(Path(p).resolve().relative_to(ROOT)):sha(p)
                                  for p in (__file__,REPACK.__file__)})
    path=Path(shutil.which('spirv-cross')).resolve()
    completed=subprocess.run([str(path),'--version'],capture_output=True,text=True,check=False)
    result['official_tools']['spirv-cross']={'path':str(path),'sha256':sha(path),'version':completed.stdout+completed.stderr}
    return result


def selected(cache,key):
    require(B.KEY.fullmatch(key),'Invalid selected shader key')
    meta=json.loads((cache/(key+'.json')).read_text())
    require(meta.get('stage') in ('vs','ps') and meta.get('descriptor_set')=={'vs':0,'ps':1}[meta['stage']],
            'Only original vertex/fragment descriptor sets are supported')
    require(meta.get('spirv_sha256')==sha(cache/(key+'.spv')) and
            meta.get('metadata_sha256')==sha(cache/(key+'.meta')),'Original selected reflection identity mismatch')
    require(all((cache/(key+'.'+s)).is_file()for s in COMMON),'Missing original hash-bound closure')
    require(not any((cache/(key+'.'+s)).exists()for s in ('pred','tssbo')),
            'Existing predicate/texel paired alternatives are outside this stage')
    spec=cache/(key+'.spec')
    if meta['stage']=='vs':require(not spec.exists(),'Vertex paired alternatives unsupported')
    else:
        fragment_recipe_contract(meta,(cache/(key+'.spv')).read_bytes(),
                                 spec.read_bytes()if spec.is_file()else None)
    return meta


def transform_pair(original_base,original_recipe,tools):
    candidate,baseproof=V1.transform(original_base,tools)
    if original_recipe is None:return candidate,None,{'base':baseproof}
    payload,payloadproof=V1.transform(original_recipe[60:],tools)
    rebuilt,recipeproof=REPACK.rebuild_equivalent_recipe(original_base,original_recipe,candidate,payload)
    return candidate,rebuilt,{'base':baseproof,'specialized_payload':payloadproof,'recipe':recipeproof}


def closures(folder,key,original,chain,proof):
    meta=copy.deepcopy(original);meta['chain']=chain;meta['spirv_sha256']=sha(folder/(key+'.spv'))
    meta['compiler_options']={**meta['compiler_options'],'address_normalize_combined':True}
    meta['address_normalize_combined']=proof
    if 'specialized_payload'in proof:
        meta['uniform_specialization']['base_sha256']=sha(folder/(key+'.spv'))
        meta['uniform_specialization']['specialization_sha256']=sha(folder/(key+'.spec'))
    meta.update(DK.emit_with_spans(folder,key,meta))
    if original['compiler_options'].get('pixel_local_reflection'):meta['pixel_local']=PL.emit(folder,key,meta)
    if original['compiler_options'].get('pixel_local_runtime'):meta['pixel_local_runtime']=PW.emit(folder,key,meta)
    (folder/(key+'.json')).write_text(json.dumps(meta,ensure_ascii=False,indent=1)+'\n')


def build(cache,receipt,input_sha,keys,parent,report,dry_run=False,expected_shaders=6931):
    original,manifest,allkeys=B.validate_input(cache,receipt,input_sha,expected_shaders)
    require(keys and len(set(keys))==len(keys) and set(keys)<=allkeys,'Unique selected input keys required')
    metas={k:selected(cache,k)for k in sorted(keys)}
    require({m['stage']for m in metas.values()}=={'vs','ps'},'Combined stage requires vertex and fragment inputs')
    identity={'schema':SCHEMA,'input_receipt_sha256':input_sha,'base_chain':manifest['chain'],
              'base_manifest':manifest,'selection':sorted(keys),'sources':source_identity(),
              'stage_contract':'exact scalar-int32 address form; original base and optional-spec equivalence; original selector tuple or unpaired status preserved'}
    chain=V1.identity_chain(identity);out=parent/('vk-'+chain)
    require(not out.exists() and not out.resolve().is_relative_to(cache.resolve()),'Output must be new independent cache')
    record={'schema':SCHEMA,'identity':identity,'chain':chain,'output':str(out.resolve()),'status':'plan'if dry_run else'building',
            'input_files_verified':len(original['artifacts']),'input_shaders_verified':len(allkeys),'selected_keys':sorted(keys),
            'performance_baseline_eligible':True,'candidate_game_performance':'NOT_YET_MEASURED'}
    report.parent.mkdir(parents=True,exist_ok=True)
    if dry_run:report.write_text(json.dumps(record,indent=2)+'\n');return record
    out.mkdir(parents=True);subprocess.run(['cp','-a','--reflink=auto',str(cache)+'/.',str(out)],check=True)
    (out/INPUT).write_bytes(receipt.read_bytes());(out/BASE).write_bytes((cache/'_vk_manifest.json').read_bytes())
    tools=Path(identity['sources']['official_tools']['spirv-opt']['path']).parent;proofs={}
    for k in sorted(keys):
        raw=(cache/(k+'.spv')).read_bytes();spec=(cache/(k+'.spec')).read_bytes()if(cache/(k+'.spec')).is_file()else None
        (out/(k+'.address-input-spv')).write_bytes(raw);(out/(k+'.address-input-json')).write_bytes((cache/(k+'.json')).read_bytes())
        if spec is not None:(out/(k+'.address-input-spec')).write_bytes(spec)
        code,rebuilt,proof=transform_pair(raw,spec,tools);(out/(k+'.spv')).write_bytes(code)
        if rebuilt is not None:(out/(k+'.spec')).write_bytes(rebuilt)
        proofs[k]=proof;closures(out,k,metas[k],chain,proof)
        print('Derived exact '+metas[k]['stage']+' address pair: '+k,flush=True)
    (out/'.chain').write_text(chain+'\n');(out/'_vk_manifest.json').write_text(json.dumps({'chain':chain,'expected':len(allkeys),
        'validated':len(allkeys),'target':manifest['target'],'toolchain':'verified original cache + exact address normalization of selected base/spec modules',
        'base_compiler_options':manifest['compiler_options'],'derived_identity':identity,'address_proofs':proofs},indent=1)+'\n')
    require(source_identity()==identity['sources'],'Source/tool closure changed while deriving')
    allfiles=B.files(out);record.update(status='built-not-deployed',address_proofs=proofs,
        artifacts={n:artifact(p)for n,p in sorted(allfiles.items())},
        changed_artifacts={n:{'original':original['artifacts'].get(n),'derived':artifact(p)}for n,p in sorted(allfiles.items())
                           if n not in original['artifacts']or artifact(p)!=original['artifacts'][n]})
    (out/RECORD).write_text(json.dumps(record,indent=2)+'\n');record['independent_verification']=verify(out,input_sha,expected_shaders)
    report.write_text(json.dumps(record,indent=2)+'\n');return record


def verify(cache,expected_input_sha,expected_shaders=6931):
    record=json.loads((cache/RECORD).read_text());identity=record['identity'];require(record['schema']==identity['schema']==SCHEMA,'Combined schema mismatch')
    require(identity['sources']==source_identity(),'Combined source/tool identity mismatch')
    require(identity['input_receipt_sha256']==expected_input_sha==sha(cache/INPUT),'Combined input receipt identity mismatch')
    chain=V1.identity_chain(identity);require(record['chain']==chain==(cache/'.chain').read_text().strip()and cache.name=='vk-'+chain,'Combined chain mismatch')
    original=json.loads((cache/INPUT).read_text());require(original.get('validated_game_and_builtin_shaders')==expected_shaders and original.get('original_proofs_unchanged')is True,'Invalid original complete receipt')
    require(artifact(cache/BASE)==original['artifacts']['_vk_manifest.json']and json.loads((cache/BASE).read_text())==identity['base_manifest'],'Original manifest identity mismatch')
    require(identity['base_chain']==original['chain']==identity['base_manifest']['chain'],'Original chain mismatch')
    keys=identity['selection'];require(keys and len(keys)==len(set(keys))and set(keys)==set(record['address_proofs']),'Combined selection mismatch')
    expected=record['artifacts'];require(set(B.files(cache))==set(expected)|{RECORD},'Undeclared combined output')
    for name,info in expected.items():require(artifact(cache/name)==info,'Combined artifact mismatch: '+name)
    metas={};newnames={INPUT,BASE};mutable={'.chain','_vk_manifest.json'}
    for k in keys:
        require(B.KEY.fullmatch(k),'Invalid selected key')
        for suffix,original_suffix in [('address-input-spv','spv'),('address-input-json','json')]:
            name=k+'.'+suffix;require(artifact(cache/name)==original['artifacts'][k+'.'+original_suffix],'Original archived artifact identity mismatch: '+name);newnames.add(name)
        meta=json.loads((cache/(k+'.address-input-json')).read_text());metas[k]=meta
        require(meta['stage']in('vs','ps')and meta['descriptor_set']=={'vs':0,'ps':1}[meta['stage']],'Original stage mismatch')
        require(meta['spirv_sha256']==sha(cache/(k+'.address-input-spv'))and meta['metadata_sha256']==sha(cache/(k+'.meta')),'Original reflection identity mismatch')
        mutable|={k+'.'+s for s in COMMON}
        if meta['stage']=='ps':
            recipe=None
            if k+'.spec'in original['artifacts']:
                name=k+'.address-input-spec';require(artifact(cache/name)==original['artifacts'][k+'.spec'],'Original archived recipe identity mismatch');newnames.add(name);mutable.add(k+'.spec')
                recipe=(cache/name).read_bytes()
            fragment_recipe_contract(meta,(cache/(k+'.address-input-spv')).read_bytes(),recipe)
        else:require(k+'.spec'not in original['artifacts'],'Unexpected original vertex pair')
        require(not any(k+'.'+s in original['artifacts']for s in ('pred','tssbo')),'Unsupported original paired alternative')
    require({m['stage']for m in metas.values()}=={'vs','ps'},'Combined stages missing')
    require(set(expected)==set(original['artifacts'])|newnames,'Combined mutation domain mismatch')
    for n,info in original['artifacts'].items():
        if n not in mutable:require(artifact(cache/n)==info,'Unrelated original artifact changed: '+n)
    allkeys={p.stem for p in cache.glob('*.spv')};require(len(allkeys)==expected_shaders and allkeys=={Path(n).stem for n in original['artifacts']if n.endswith('.spv')},'Combined base key set changed')
    manifest=json.loads((cache/'_vk_manifest.json').read_text());require(manifest['derived_identity']==identity and manifest['chain']==chain and
        manifest['expected']==manifest['validated']==expected_shaders and manifest['address_proofs']==record['address_proofs'],'Combined manifest mismatch')
    tools=Path(identity['sources']['official_tools']['spirv-opt']['path']).parent
    for k in keys:
        raw=(cache/(k+'.address-input-spv')).read_bytes();spec=(cache/(k+'.address-input-spec')).read_bytes()if k+'.spec'in original['artifacts']else None
        code,rebuilt,proof=transform_pair(raw,spec,tools)
        require(code==(cache/(k+'.spv')).read_bytes()and proof==record['address_proofs'][k],'Combined exact normalization regeneration mismatch: '+k)
        if rebuilt is not None:require(rebuilt==(cache/(k+'.spec')).read_bytes(),'Original selector recipe regeneration mismatch: '+k)
        with tempfile.TemporaryDirectory(prefix='dmsh_address_combined_verify_')as temporary:
            temp=Path(temporary)
            for s in ('spv','meta','spec'):
                if (cache/(k+'.'+s)).is_file():shutil.copyfile(cache/(k+'.'+s),temp/(k+'.'+s))
            closures(temp,k,metas[k],chain,proof)
            for s in COMMON:
                require((temp/(k+'.'+s)).read_bytes()==(cache/(k+'.'+s)).read_bytes(),'Combined original closure regeneration mismatch: '+k+'.'+s)
    require(source_identity()==identity['sources'],'Sources changed during combined verification')
    return {'status':'passed-not-deployed','chain':chain,'complete_shaders':len(allkeys),'selected_keys_regenerated':keys,
            'output_artifacts_verified':len(expected),'input_receipt_sha256':expected_input_sha,
            'original_uniform_selector_tuples_preserved':True,'unrelated_original_artifacts_unchanged':len(original['artifacts'])-len(mutable)}


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='operation',required=True)
    for name in ('plan','build'):
        s=sub.add_parser(name);s.add_argument('--input-cache',type=Path,required=True);s.add_argument('--input-receipt',type=Path,required=True)
        s.add_argument('--input-receipt-sha256',required=True);s.add_argument('--keys',nargs='+',required=True);s.add_argument('--output-parent',type=Path,required=True);s.add_argument('--report',type=Path,required=True)
    s=sub.add_parser('verify');s.add_argument('--cache',type=Path,required=True);s.add_argument('--input-receipt-sha256',required=True);s.add_argument('--report',type=Path,required=True)
    a=p.parse_args(argv)
    if a.operation=='verify':r=verify(a.cache.resolve(),a.input_receipt_sha256);a.report.write_text(json.dumps(r,indent=2)+'\n')
    else:r=build(a.input_cache.resolve(),a.input_receipt.resolve(),a.input_receipt_sha256,a.keys,a.output_parent.resolve(),a.report.resolve(),a.operation=='plan')
    print(json.dumps({k:v for k,v in r.items()if k not in ('identity','artifacts','address_proofs','changed_artifacts')},indent=2))
if __name__=='__main__':main()
