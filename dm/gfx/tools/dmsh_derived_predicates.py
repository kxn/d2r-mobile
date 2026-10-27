"""Explicit verified-cache input stage: add selected DPR1 recipes, never retranslate bases."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import dmsh_driver_keys
import dmsh_pixel_local
import dmsh_pixel_local_wire
import dmsh_uniform_predicates

ROOT=Path(__file__).resolve().parents[3]
SCHEMA='dmsh-verified-cache-predicates-v1'
KEY=re.compile('[0-9a-f]{16}')
SHA=re.compile('[0-9a-f]{64}')
RECORD='_derived_build.json'
INPUT='_derived_input_receipt.json'
BASE_MANIFEST='_derived_base_manifest.json'

def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':')).encode()
def require(condition,message):
    if not condition:raise ValueError(message)
def artifact(path):return {'sha256':sha(path),'bytes':path.stat().st_size}
def files(folder):
    entries=list(folder.iterdir())
    require(all(p.is_file() and not p.is_symlink() for p in entries),'Cache must contain only ordinary immediate files')
    return {p.name:p for p in entries}
def check_artifacts(folder,expected):
    actual=files(folder)
    require(set(actual)==set(expected),'Cache file set differs from declared artifacts')
    for name,path in actual.items():
        require(Path(name).name==name and name not in ('.','..'),'Unsafe artifact filename')
        require(artifact(path)==expected[name],'Artifact mismatch: '+name)

def validate_input(cache,receipt,expected_sha,expected_shaders=6931):
    require(SHA.fullmatch(expected_sha),'Explicit input receipt SHA256 is required')
    require(sha(receipt)==expected_sha,'Input receipt identity mismatch')
    record=json.loads(receipt.read_text())
    require(record.get('validated_game_and_builtin_shaders')==expected_shaders and
        record.get('original_proofs_unchanged') is True,'Input is not an original complete verified cache receipt')
    require(isinstance(record.get('artifacts'),dict),'Missing complete per-file input ledger')
    check_artifacts(cache,record['artifacts'])
    manifest=json.loads((cache/'_vk_manifest.json').read_text())
    require(manifest.get('chain')==record.get('chain')==(cache/'.chain').read_text().strip(),'Input chain mismatch')
    require(manifest.get('expected')==manifest.get('validated')==expected_shaders,'Input shader count mismatch')
    require(manifest.get('target')=='vulkan1.1','Unsupported input target')
    require(not manifest['compiler_options'].get('uniform_predicates'),'Input already has predicate alternatives')
    keys={p.stem for p in cache.glob('*.spv')}
    require(len(keys)==expected_shaders and all(KEY.fullmatch(k) for k in keys),'Input shader key set mismatch')
    require(keys=={p.stem for p in cache.glob('*.meta')},'Input metadata key set mismatch')
    require(not list(cache.glob('*.pred')),'Undeclared input predicates')
    results=json.loads((cache/'_vk_build.json').read_text())
    require(len(results)==expected_shaders and keys=={r['key']for r in results} and
        all(r.get('st')=='ok' for r in results),'Incomplete original input build results')
    return record,manifest,keys

def source_identity():
    names=['dmsh_derived_predicates.py','dmsh_build.py','dmsh_uniform_predicates.py',
        'dmsh_driver_keys.py','dmsh_pixel_local.py','dmsh_pixel_local_wire.py']
    source={name:sha(Path(__file__).with_name(name)) for name in names}
    tools={}
    for name in ('spirv-opt','spirv-dis','spirv-as','spirv-val'):
        path=Path(shutil.which(name)).resolve()
        tools[name]={'sha256':sha(path),'version':subprocess.check_output([str(path),'--version'],text=True,stderr=subprocess.STDOUT)}
    return {'source_files':source,'official_tools':tools}

def make_identity(base_manifest,input_sha,selected,sources):
    return {'schema':SCHEMA,'input_receipt_sha256':input_sha,'base_chain':base_manifest['chain'],
        'base_manifest':base_manifest,'predicate_selection':sorted(selected),
        'operation':'selected original DPR1 generator; immutable base/meta/spec/reflection input stage',
        'sources':sources}
def identity_chain(identity):return hashlib.sha256(canonical(identity)).hexdigest()[:12]

def selected_reflection(cache,key):
    require(KEY.fullmatch(key),'Invalid selected shader key')
    path=cache/(key+'.json');require(path.is_file(),'Selected key has no original reflection: '+key)
    meta=json.loads(path.read_text())
    require(meta.get('stage')=='ps' and meta.get('descriptor_set')==1,'Selected shader must be fragment set1: '+key)
    require((cache/(key+'.spec')).is_file(),'Selected shader lacks the original specialization recipe: '+key)
    require(all((cache/(key+'.'+suffix)).is_file()for suffix in ('dkey','dspan')),'Selected input lacks original driver closures: '+key)
    require(meta['spirv_sha256']==sha(cache/(key+'.spv')) and meta['metadata_sha256']==sha(cache/(key+'.meta')),'Selected original reflection identity mismatch')
    return meta

def make_predicate(folder,key,meta):
    base=(folder/(key+'.spv')).read_bytes();recipe=(folder/(key+'.spec')).read_bytes()
    eligible={b['binding']:b['slot']for b in meta['cbufs']if 3<=b['slot']<19}
    blob,proof=dmsh_uniform_predicates.transform(base,recipe,eligible)
    require(blob is not None,'Selected shader has no qualified DPR1 recipe: '+key+' '+str(proof))
    dmsh_uniform_predicates.unpack(blob,base,recipe,proof)
    return blob,proof

def emit_closures(folder,key,meta,predicate_proof,options):
    # Only an in-memory reflection overlay is supplied to the ORIGINAL emitters.
    # The complete original .json/.meta/.spv/.spec are never rewritten.
    effective=dict(meta);effective['uniform_predicates']=predicate_proof
    changed=dmsh_driver_keys.emit_with_spans(folder,key,effective);effective.update(changed)
    if options.get('pixel_local_reflection'):
        changed['pixel_local']=dmsh_pixel_local.emit(folder,key,effective);effective.update(changed)
    if options.get('pixel_local_runtime'):
        changed['pixel_local_runtime']=dmsh_pixel_local_wire.emit(folder,key,effective)
    return {'uniform_predicates':predicate_proof,**changed}

def preserved_names(artifacts):
    return {name for name in artifacts if Path(name).suffix in ('.spv','.meta','.spec') or
        (Path(name).suffix=='.json' and KEY.fullmatch(Path(name).stem))}

def build(cache,receipt,expected_sha,selected,parent,report,dry_run=False,expected_shaders=6931):
    original,manifest,keys=validate_input(cache,receipt,expected_sha,expected_shaders)
    require(selected and len(set(selected))==len(selected) and set(selected)<=keys,'Unique selected input keys are required')
    for key in selected:selected_reflection(cache,key)
    identity=make_identity(manifest,expected_sha,selected,source_identity());chain=identity_chain(identity)
    output=parent/('vk-'+chain)
    require(not output.exists() and not output.resolve().is_relative_to(cache.resolve()),'Output must be a new independent cache')
    record={'schema':SCHEMA,'identity':identity,'chain':chain,'output':str(output.resolve()),
        'scope':'Complete original cache plus explicitly selected observation recipes; no base translation or shader selection',
        'status':'plan' if dry_run else 'building','selected_keys':sorted(selected),
        'input_files_verified':len(original['artifacts']),'input_shaders_verified':len(keys)}
    report.parent.mkdir(parents=True,exist_ok=True)
    if dry_run:
        report.write_text(json.dumps(record,indent=2)+'\n');return record
    output.mkdir(parents=True)
    subprocess.run(['cp','-a','--reflink=auto',str(cache)+'/.' ,str(output)],check=True)
    (output/INPUT).write_bytes(receipt.read_bytes())
    (output/BASE_MANIFEST).write_bytes((cache/'_vk_manifest.json').read_bytes())
    overlays={}
    for key in sorted(selected):
        meta=selected_reflection(output,key);blob,proof=make_predicate(output,key,meta)
        (output/(key+'.pred')).write_bytes(blob)
        overlays[key]=emit_closures(output,key,meta,proof,manifest['compiler_options'])
        print('Derived qualified predicate and original closures: '+key,flush=True)
    for name in preserved_names(original['artifacts']):
        require(artifact(output/name)==original['artifacts'][name],'Pure observation changed immutable base input: '+name)
    require(source_identity()==identity['sources'],'Source/tool closure changed during derivation')
    new_manifest={'chain':chain,'expected':len(keys),'validated':len(keys),'target':manifest['target'],
        'toolchain':'verified-cache-derived; selected original DPR1 recipes only',
        'base_compiler_options':manifest['compiler_options'],'derived_identity':identity,
        'predicate_proof_overlays':overlays}
    (output/'_vk_manifest.json').write_text(json.dumps(new_manifest,indent=1)+'\n')
    (output/'.chain').write_text(chain+'\n')
    record.update(status='built-not-deployed',predicate_proof_overlays=overlays,
        artifacts={name:artifact(path)for name,path in sorted(files(output).items())})
    (output/RECORD).write_text(json.dumps(record,indent=2)+'\n')
    # Independent reconstruction of the two new recipes/closures follows below.
    verified=verify(output,expected_sha,expected_shaders)
    record['independent_verification']=verified
    report.write_text(json.dumps(record,indent=2)+'\n');return record

def verify(cache,expected_input_sha,expected_shaders=6931):
    record=json.loads((cache/RECORD).read_text());identity=record['identity']
    require(record.get('schema')==SCHEMA==identity.get('schema'),'Derived schema mismatch')
    require(identity['input_receipt_sha256']==expected_input_sha==sha(cache/INPUT),'Derived input receipt identity mismatch')
    require(identity['sources']==source_identity(),'Derived source/tool identity mismatch')
    chain=identity_chain(identity)
    require(record['chain']==chain==(cache/'.chain').read_text().strip() and cache.name=='vk-'+chain,'Derived chain identity mismatch')
    observed=files(cache);expected=record['artifacts'];require(set(observed)==set(expected)|{RECORD},'Undeclared derived output')
    for name,info in expected.items():require(artifact(cache/name)==info,'Derived artifact mismatch: '+name)
    original=json.loads((cache/INPUT).read_text());require(original.get('validated_game_and_builtin_shaders')==expected_shaders and original.get('original_proofs_unchanged') is True,'Invalid complete input receipt')
    require(artifact(cache/BASE_MANIFEST)==original['artifacts']['_vk_manifest.json'],
        'Original base manifest identity mismatch')
    require(json.loads((cache/BASE_MANIFEST).read_text())==identity['base_manifest'] and
        identity['base_chain']==original['chain']==identity['base_manifest']['chain'],
        'Derived options do not match the verified original base manifest')
    selected=identity['predicate_selection']
    require(selected and len(set(selected))==len(selected),'Invalid derived selection')
    new_names={INPUT,BASE_MANIFEST}|{key+'.pred'for key in selected}
    require(set(expected)==set(original['artifacts'])|new_names,'Unexpected derived file mutation domain')
    mutable={'.chain','_vk_manifest.json'}|{key+'.'+suffix for key in selected for suffix in ('dkey','dspan','plocal.json','plocal')}
    for name,info in original['artifacts'].items():
        if name not in mutable:require(artifact(cache/name)==info,'Unrelated input artifact changed: '+name)
    immutable=preserved_names(original['artifacts'])
    for name in immutable:require(artifact(cache/name)==original['artifacts'][name],'Immutable input changed: '+name)
    keys={p.stem for p in cache.glob('*.spv')};require(len(keys)==expected_shaders,'Derived shader count mismatch')
    require(keys=={Path(n).stem for n in original['artifacts']if n.endswith('.spv')},'Derived base key set mismatch')
    require(set(selected)=={p.stem for p in cache.glob('*.pred')},'Derived predicate selection mismatch')
    manifest=json.loads((cache/'_vk_manifest.json').read_text())
    require(manifest['derived_identity']==identity and manifest['chain']==chain and
        manifest['expected']==manifest['validated']==expected_shaders and
        manifest['predicate_proof_overlays']==record['predicate_proof_overlays'],'Derived manifest mismatch')
    for key in selected:
        meta=selected_reflection(cache,key);blob,proof=make_predicate(cache,key,meta)
        require(blob==(cache/(key+'.pred')).read_bytes(),'DPR1 regeneration mismatch: '+key)
        with tempfile.TemporaryDirectory(prefix='dmsh_derived_verify_')as temporary:
            temp=Path(temporary)
            for suffix in dmsh_driver_keys.SUFFIXES:
                path=cache/(key+'.'+suffix)
                if path.is_file():shutil.copyfile(path,temp/path.name)
            overlay=emit_closures(temp,key,meta,proof,identity['base_manifest']['compiler_options'])
            require(overlay==record['predicate_proof_overlays'][key],'Derived proof overlay mismatch: '+key)
            for suffix in ('dkey','dspan','plocal.json','plocal'):
                path=temp/(key+'.'+suffix)
                if path.is_file():require(path.read_bytes()==(cache/path.name).read_bytes(),'Original closure regeneration mismatch: '+path.name)
    require(source_identity()==identity['sources'],'Source changed during independent verification')
    return {'status':'passed-not-deployed','chain':chain,'complete_shaders':len(keys),
        'immutable_payloads_identical':len(immutable),'predicate_keys_regenerated':selected,
        'output_artifacts_verified':len(expected),'input_receipt_sha256':expected_input_sha}

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='operation',required=True)
    for op in ('plan','build'):
        s=sub.add_parser(op);s.add_argument('--input-cache',type=Path,required=True)
        s.add_argument('--input-receipt',type=Path,required=True);s.add_argument('--input-receipt-sha256',required=True)
        s.add_argument('--keys',nargs='+',required=True);s.add_argument('--output-parent',type=Path,required=True)
        s.add_argument('--report',type=Path,required=True)
    s=sub.add_parser('verify');s.add_argument('--cache',type=Path,required=True)
    s.add_argument('--input-receipt-sha256',required=True);s.add_argument('--report',type=Path,required=True)
    a=p.parse_args(argv)
    if a.operation=='verify':
        result=verify(a.cache.resolve(),a.input_receipt_sha256);a.report.write_text(json.dumps(result,indent=2)+'\n')
    else:result=build(a.input_cache.resolve(),a.input_receipt.resolve(),a.input_receipt_sha256,a.keys,a.output_parent.resolve(),a.report.resolve(),a.operation=='plan')
    print(json.dumps({k:v for k,v in result.items()if k not in ('identity','artifacts','predicate_proof_overlays')},indent=2))
    return 0

if __name__=='__main__':raise SystemExit(main())
