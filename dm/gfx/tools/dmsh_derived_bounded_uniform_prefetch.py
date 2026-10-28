"""Verified child of an explicit combined-address cache: exact FS bounded-prefetch CFG.

Only declared DSP payloads change. Their existing selector/header/base identity
are repacked, never rediscovered. A pinned, independently verified parent cache
is an explicit input to independent verification; no game binary is touched.
"""
import argparse,copy,hashlib,json,shutil,subprocess,tempfile,types
from pathlib import Path
import dmsh_derived_address_combined as P
import dmsh_derived_predicates as B
import dmsh_bounded_uniform_prefetch as W
import dmsh_derived_one_word_ubo as LEGACY
import struct

ROOT=Path(__file__).resolve().parents[3]
SCHEMA='dmsh-verified-cache-bounded-uniform-prefetch-v1'
RECORD='_derived_build.json';PARENT='_prefetch_parent_build.json';MANIFEST='_prefetch_parent_manifest.json'
PARENT_VERIFIER='_prefetch_parent_verifier.py'
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
    identity['source_files'].update({str(Path(p).resolve().relative_to(ROOT)):sha(p)for p in
        (__file__,W.__file__,LEGACY.__file__,W.selector.__file__,ROOT/'tools/experiments/bounded-uniform-prefetch/prove-domain.py')})
    identity['prefetch_tools']={n:{'path':str(p),'sha256':sha(p),'version':subprocess.check_output([str(p),'--version'],text=True,stderr=subprocess.STDOUT)}for n,p in tools().items()}
    z=Path(W.selector.z3.__file__).resolve();lib=z.parent/'lib/libz3.so';require(lib.is_file(),'Missing Z3 proof runtime identity')
    identity['proof_solver']={'version':W.selector.z3.get_version_string(),'python_init_sha256':sha(z),'native_library_sha256':sha(lib)}
    return identity

qualified_parent_verifier = LEGACY.qualified_parent_verifier
parent_record = LEGACY.parent_record

def selected(cache,key):
    meta=P.selected(cache,key)
    require(meta['stage']=='ps'and(cache/(key+'.spec')).is_file(),'Bounded-prefetch stage requires an existing fragment pair')
    return meta

MAGIC=0x33505344
MIN_BYTES=192

def guard_binding(metadata,code,uniform):
    text=W.command(['spirv-dis','--raw-id','-o','-','-'],code).decode()
    _,_,_,_=W.selector.parse(text)
    decs={}
    for line in text.splitlines():
        fields=line.split()
        if len(fields)==4 and fields[0]=='OpDecorate'and fields[1]==uniform:
            if fields[2]in('DescriptorSet','Binding'):decs[fields[2]]=int(fields[3])
    stage,descriptor_set,bindings=P.DK.metadata(metadata)
    require(stage==1 and descriptor_set==decs.get('DescriptorSet')==1,'Prefetch Uniform descriptor set mismatch')
    binding=decs.get('Binding')
    matches=[b for b in bindings if b[0]==1 and b[1]==binding and b[5]==1 and 3<=b[2]<19]
    require(len(matches)==1,'Prefetch Uniform reg/binding mismatch')
    return binding,matches[0][2]

def unpack(base,envelope,metadata=None):
    require(len(envelope)>=128 and len(envelope)%4==0,'DSP3 envelope size')
    h=struct.unpack_from('<32I',envelope)
    require(h[0]==MAGIC and h[1]==1 and h[31]==0,'DSP3 envelope header/reserved')
    require(h[2]>=80 and h[2]%4==0 and 5<=h[3]<=(32<<18) and len(envelope)==128+h[2]+h[3]*4,'DSP3 payload size')
    require(h[4]<128 and 3<=h[5]<19 and h[6]==MIN_BYTES,'DSP3 range/binding contract')
    legacy=envelope[128:128+h[2]];candidate=envelope[128+h[2]:]
    require(hashlib.sha256(base).digest()==envelope[28:60],'DSP3 base SHA mismatch')
    require(hashlib.sha256(legacy).digest()==envelope[60:92],'DSP3 legacy SHA mismatch')
    require(hashlib.sha256(candidate).digest()==envelope[92:124],'DSP3 candidate SHA mismatch')
    P.UP._old_recipe(base,legacy)
    require(struct.unpack_from('<I',candidate)[0]==0x07230203,'DSP3 candidate SPIRV magic')
    require(P.REPACK.specialization_contract(legacy[60:])==P.REPACK.specialization_contract(candidate),'DSP3 original SpecId/default/type changed')
    if metadata is not None:
        text=W.command(['spirv-dis','--raw-id','-o','-','-'],legacy[60:]).decode()
        _,proof=W.transform(text,MIN_BYTES)
        require(guard_binding(metadata,legacy[60:],proof['original_uniform_variable'])==(h[4],h[5]),'DSP3 guard differs from original binding')
    return legacy,candidate,{'binding':h[4],'reg':h[5],'min_effective_bytes':h[6]}

def transform_pair(base,recipe,metadata):
    P.UP._old_recipe(base,recipe)
    with tempfile.TemporaryDirectory(prefix='dmsh_bounded_prefetch_')as directory:
        tmp=Path(directory);src=tmp/'input.spv';src.write_bytes(recipe[60:]);dst=tmp/'candidate.spv'
        proof=W.build(src,dst,MIN_BYTES);candidate=dst.read_bytes()
    binding,reg=guard_binding(metadata,recipe[60:],proof['original_uniform_variable'])
    header=(struct.pack('<7I',MAGIC,1,len(recipe),len(candidate)//4,binding,reg,MIN_BYTES)+
        hashlib.sha256(base).digest()+hashlib.sha256(recipe).digest()+hashlib.sha256(candidate).digest()+bytes(4))
    envelope=header+recipe+candidate
    old,new,guard=unpack(base,envelope,metadata)
    require(old==recipe and new==candidate,'DSP3 envelope roundtrip mismatch')
    return envelope,{'specialized_payload':proof,'range_guard':guard,'base_unchanged':True,
        'legacy_full_recipe_sha256':hashlib.sha256(recipe).hexdigest(),'candidate_spirv_sha256':hashlib.sha256(candidate).hexdigest(),
        'envelope_sha256':hashlib.sha256(envelope).hexdigest(),'selector_reselected':False,
        'native_minimum_range_evidence':'Current12b actual newly copied byte interval [64,192); source accesses [112,176)',
        'requires_cross_stage_readonly_admission':True}

def driver_closures(blobs,legacy,candidate):
    original={**blobs,'spec':legacy}
    key,kproof,span,sproof=P.DK.build_with_spans(original)
    stage,descriptor_set,bindings=P.DK.metadata(blobs['meta'])
    # Reuse the frozen driver-read interpreter. The candidate only adds a
    # non-driver UBO, but this must be proved for every driver binding.
    for slot in (0,2):
        matches=[b for b in bindings if b[0]==1 and b[2]==slot]
        if not matches:continue
        require(len(matches)==1 and matches[0][5]==1,'Ambiguous driver binding')
        binding=matches[0][1]
        before=P.DK.reads(legacy[60:],descriptor_set,binding)
        after=P.DK.reads(candidate,descriptor_set,binding)
        require(before==after,'Prefetch changed driver constant read set')
    fingerprint=P.DK.fingerprint(blobs)
    def serialize(data):
        # Standard DDK1/DSP1 header and trailer; explicitly bind the actual
        # DSP3 artifact instead of falsely naming the embedded legacy bytes.
        body=data[:16]+fingerprint+data[48:-32]
        return body+hashlib.sha256(body).digest()
    return serialize(key),kproof,serialize(span),sproof

def pixel_closures(blobs,reflection,legacy,candidate):
    modules={'base':blobs['spv'],'spec':legacy[60:],'prefetch_spec':candidate}
    report={'schema':P.PL.VERSION,'closure_verified':True,'consumer_rule':'exact module SHA only; do not reuse base proof for alternatives',
        'artifacts':{s:P.PL.sha(b)for s,b in blobs.items()},'modules':{k:P.PL.analyze(v)for k,v in modules.items()}}
    records=[]
    for name,code in modules.items():
        kind={'base':1,'spec':2,'prefetch_spec':6}[name]
        full=P.PW._record(kind,code)
        if full is None:continue
        records.append(full);count=struct.unpack_from('<I',full,8)[0]
        if count>1:records.extend(P.PW._record(kind,code,omit)for omit in range(count))
    body=b''.join(records);size=P.PW.HEADER_BYTES+len(body)
    require(size<=P.PW.MAX_BYTES,'Pixel-local artifact size')
    data=(struct.pack('<8I',P.PW.MAGIC,P.PW.VERSION,size,1,reflection['descriptor_set'],len(records),P.PW.HEADER_BYTES,0)+
        b''.join(P.PW.sha(blobs[s])if s in blobs else bytes(32)for s in P.PW.SUFFIXES)+body)
    return report,data,{'schema':P.PW.VERSION,'sha256':P.PW.sha(data).hex(),'records':len(records),
        'source_modules':{k:P.PW.sha(v).hex()for k,v in modules.items()}}

def closures(folder,key,original,chain,proof):
    meta=copy.deepcopy(original);meta['chain']=chain
    meta['compiler_options']={**meta['compiler_options'],'bounded_uniform_prefetch':True}
    meta['bounded_uniform_prefetch']=proof
    meta['uniform_specialization']['specialization_sha256']=sha(folder/(key+'.spec'))
    blobs={s:(folder/(key+'.'+s)).read_bytes()for s in P.DK.SUFFIXES if(folder/(key+'.'+s)).is_file()}
    legacy,candidate,guard=unpack(blobs['spv'],blobs['spec'],blobs['meta'])
    data,kproof,span,sproof=driver_closures(blobs,legacy,candidate)
    (folder/(key+'.dkey')).write_bytes(data);(folder/(key+'.dspan')).write_bytes(span)
    meta.update(driver_keys={'sha256':hashlib.sha256(data).hexdigest(),'proof':kproof},
        driver_spans={'sha256':hashlib.sha256(span).hexdigest(),'proof':sproof})
    report,pwire,pinfo=pixel_closures(blobs,meta,legacy,candidate)
    if original['compiler_options'].get('pixel_local_reflection'):
        encoded=json.dumps(report,sort_keys=True,indent=1)+'\n';(folder/(key+'.plocal.json')).write_text(encoded)
        meta['pixel_local']={'sha256':hashlib.sha256(encoded.encode()).hexdigest(),'schema':P.PL.VERSION}
    if original['compiler_options'].get('pixel_local_runtime'):
        (folder/(key+'.plocal')).write_bytes(pwire);meta['pixel_local_runtime']=pinfo
    (folder/(key+'.json')).write_text(json.dumps(meta,ensure_ascii=False,indent=1)+'\n')

def build(cache,parent_sha,keys,output_parent,report,dry_run=False,expected_shaders=6931):
    parent=parent_record(cache,parent_sha,expected_shaders)
    require(keys and len(set(keys))==len(keys)and all(B.KEY.fullmatch(k)for k in keys),'Unique selected shader keys required')
    require(all(k+'.spv'in parent['artifacts']for k in keys),'Selected key outside parent ledger')
    metas={k:selected(cache,k)for k in sorted(keys)}
    identity={'schema':SCHEMA,'parent_record_sha256':parent_sha,'parent_chain':parent['chain'],
              'original_input_receipt_sha256':parent['identity']['input_receipt_sha256'],'selection':sorted(keys),
              'sources':source_identity(),'contract':'DSP3 preserves exact legacy DSP2 plus constant-table prefetch candidate; current >=192-byte range and readonly admission required; domain-outside full32 fallback remains'}
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
            name=k+'.prefetch-input-'+suffix;(out/name).write_bytes((cache/(k+'.'+suffix)).read_bytes());changed.add(name)
        recipe,proof=transform_pair((cache/(k+'.spv')).read_bytes(),(cache/(k+'.spec')).read_bytes(),(cache/(k+'.meta')).read_bytes());(out/(k+'.spec')).write_bytes(recipe);proofs[k]=proof
        closures(out,k,metas[k],chain,proof);changed|={k+'.spec',*(k+'.'+s for s in CLOSURES)}
        print('Derived proved bounded-prefetch paired fragment: '+k,flush=True)
    (out/'.chain').write_text(chain+'\n');manifest={'chain':chain,'expected':expected_shaders,'validated':expected_shaders,'target':'vulkan1.1','toolchain':'verified combined-address parent + bounded-prefetch Uniform paired-payload CFG','derived_identity':identity,'parent_chain':parent['chain'],'prefetch_proofs':proofs}
    (out/'_vk_manifest.json').write_text(json.dumps(manifest,indent=1)+'\n')
    require(source_identity()==identity['sources'],'Source/tool closure changed during derivation')
    artifacts=copy.deepcopy(parent['artifacts'])
    for name in changed:artifacts[name]=artifact(out/name)
    record.update(status='built-not-deployed',proofs=proofs,artifacts=artifacts,changed_artifacts={n:{'parent':parent['artifacts'].get(n),'derived':artifacts[n]}for n in sorted(changed)})
    (out/RECORD).write_text(json.dumps(record,indent=2)+'\n');record['independent_verification']=verify(out,parent_sha,cache,expected_shaders);report.write_text(json.dumps(record,indent=2)+'\n');return record

def verify(cache,expected_parent_sha,parent_cache,expected_shaders=6931):
    record=json.loads((cache/RECORD).read_text());identity=record['identity']
    require(record.get('schema')==identity.get('schema')==SCHEMA,'Bounded-prefetch schema mismatch')
    require(identity['parent_record_sha256']==expected_parent_sha==sha(cache/PARENT),'Pinned archived parent identity mismatch')
    parent=parent_record(parent_cache,expected_parent_sha,expected_shaders,(cache/PARENT_VERIFIER).read_bytes())
    exact_parent,_=qualified_parent_verifier(parent,(cache/PARENT_VERIFIER).read_bytes());parent_verification=exact_parent.verify(parent_cache,parent['identity']['input_receipt_sha256'],expected_shaders)
    require(identity['sources']==source_identity(),'Bounded-prefetch source/tool identity mismatch')
    chain=P.V1.identity_chain(identity);require(record['chain']==chain==(cache/'.chain').read_text().strip()and cache.name=='vk-'+chain,'Bounded-prefetch chain mismatch')
    require(identity['parent_chain']==parent['chain']and identity['original_input_receipt_sha256']==parent['identity']['input_receipt_sha256'],'Parent provenance mismatch')
    require((cache/PARENT).read_bytes()==(parent_cache/RECORD).read_bytes(),'Archived parent bytes mismatch')
    require(artifact(cache/MANIFEST)==parent['artifacts']['_vk_manifest.json'],'Archived parent manifest mismatch')
    keys=identity['selection'];require(keys and len(set(keys))==len(keys)and all(B.KEY.fullmatch(k)for k in keys)and set(keys)==set(record['proofs']),'Invalid bounded-prefetch selection')
    mutable={'.chain','_vk_manifest.json'};newnames={PARENT,MANIFEST,PARENT_VERIFIER}
    for k in keys:
        mutable|={k+'.spec',*(k+'.'+s for s in CLOSURES)};newnames|={k+'.prefetch-input-spec',k+'.prefetch-input-json'}
    expected=record['artifacts'];require(set(expected)==set(parent['artifacts'])|newnames,'Bounded-prefetch mutation domain mismatch')
    require(set(B.files(cache))==set(expected)|{RECORD},'Undeclared bounded-prefetch output')
    observed={n:artifact(cache/n)for n in expected}
    for n,info in expected.items():require(observed[n]==info,'Bounded-prefetch artifact mismatch: '+n)
    for n,info in parent['artifacts'].items():
        if n not in mutable:require(observed[n]==info,'Unrelated parent artifact changed: '+n)
    manifest=json.loads((cache/'_vk_manifest.json').read_text());require(manifest=={'chain':chain,'expected':expected_shaders,'validated':expected_shaders,'target':'vulkan1.1','toolchain':'verified combined-address parent + bounded-prefetch Uniform paired-payload CFG','derived_identity':identity,'parent_chain':parent['chain'],'prefetch_proofs':record['proofs']},'Bounded-prefetch manifest mismatch')
    require(len(list(cache.glob('*.spv')))==expected_shaders,'Bounded-prefetch base key set changed')
    for k in keys:
        for suffix in ('json','spec'):
            name=k+'.prefetch-input-'+suffix;require(observed[name]==parent['artifacts'][k+'.'+suffix],'Archived selected parent input mismatch')
        meta=json.loads((cache/(k+'.prefetch-input-json')).read_text());base=(cache/(k+'.spv')).read_bytes();recipe=(cache/(k+'.prefetch-input-spec')).read_bytes()
        P.fragment_recipe_contract(meta,base,recipe);require(meta['stage']=='ps'and meta['descriptor_set']==1,'Fragment stage contract changed')
        rebuilt,proof=transform_pair(base,recipe,(cache/(k+'.meta')).read_bytes());require(rebuilt==(cache/(k+'.spec')).read_bytes()and proof==record['proofs'][k],'Bounded-prefetch transform/selector recipe regeneration mismatch: '+k)
        with tempfile.TemporaryDirectory(prefix='dmsh_prefetch_closures_')as d:
            tmp=Path(d)
            for suffix in ('spv','meta','spec'):shutil.copyfile(cache/(k+'.'+suffix),tmp/(k+'.'+suffix))
            closures(tmp,k,meta,chain,proof)
            for suffix in CLOSURES:require((tmp/(k+'.'+suffix)).read_bytes()==(cache/(k+'.'+suffix)).read_bytes(),'Bounded-prefetch closure regeneration mismatch: '+k+'.'+suffix)
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
