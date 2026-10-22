"""Explicit, immutable sample-profile inputs for the native host builder.
This verifies declared producer files, not profile weights or an invented format.
Actual LLVM reader/compiler consumption qualification remains separate.
"""
from pathlib import Path
import hashlib
import json
import re

BASE='f75de421d0558c746ffc9cd4fee2e1a66c5e9c40'
BASE_ELF='4a8420209e2ac6c71c161626472458ac498ee844bdce58677c8e2769b5ec4618'
BASE_BID='6e6f12c5c5f7a858704a84ee294b19d95aa3f894'
DIAGNOSTIC_FLAGS=['-Rpass-analysis=sample-profile.*','-Rpass=sample-profile.*',
                  '-fsave-optimization-record','-foptimization-record-passes=sample-profile.*']

def sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def record(path):
    path=Path(path).resolve();return dict(path=str(path),sha256=sha(path),bytes=path.stat().st_size)

def require(ok,why):
    if not ok:raise ValueError(why)

def pin(row):
    require(isinstance(row,dict) and isinstance(row.get('path'),str) and re.fullmatch('[a-f0-9]{64}',row.get('sha256','')),
            'invalid profile producer file pin')
    path=Path(row['path']).resolve()
    require(path.is_file() and sha(path)==row['sha256'],'profile producer file changed: '+str(path))
    require('bytes' not in row or path.stat().st_size==row['bytes'],'profile producer file size changed')
    return record(path)

def verify(args):
    values=[args.sample_profile,args.sample_profile_sha256,args.sample_profile_provenance,args.sample_profile_provenance_sha256]
    if not any(v is not None for v in values):return None
    require(all(v is not None for v in values),'sample profile and producer path/hash must all be explicit')
    require(not args.thinlto,'sample profile and ThinLTO must be qualified separately')
    profile=pin(dict(path=str(args.sample_profile),sha256=args.sample_profile_sha256))
    proof=pin(dict(path=str(args.sample_profile_provenance),sha256=args.sample_profile_provenance_sha256))
    provenance=json.loads(Path(proof['path']).read_text())
    require(provenance.get('completed') is True and provenance.get('source_revision')==BASE,
            'sample profile producer is not completed exact f75')
    require(provenance.get('sample_profile',{}).get('sha256')==profile['sha256'], 'profile differs from producer output')
    elf=provenance.get('ELF',{})
    require(elf.get('sha256')==BASE_ELF and elf.get('build_id')==BASE_BID,'sample profile producer ELF differs from f75')
    dependencies={}
    def visit(node):
        if isinstance(node,dict):
            if 'path' in node and 'sha256' in node:
                row=pin(node);key=row['path'];require(key not in dependencies or dependencies[key]==row,'ambiguous producer input')
                dependencies[key]=row
            for child in node.values():visit(child)
        elif isinstance(node,list):
            for child in node:visit(child)
    visit(provenance)
    documents={}
    for name in ('qualification','inputs','manifest'):
        row=pin(provenance.get(name));documents[name]=json.loads(Path(row['path']).read_text())
        visit(documents[name])
    qualified=documents['qualification'];records=qualified.get('records',[])
    require(qualified.get('no_data_rewriting_no_PID_TID_filter_no_custom_weights') is True
            and len(records)==3 and len({r.get('ordinal') for r in records})==3,
            'official three-record producer qualification differs')
    for row in records:
        require(row.get('ELF',{}).get('sha256')==BASE_ELF and row.get('ELF_BID')==BASE_BID
                and '--use_lbr=false' in row.get('create_gcov_command',{}).get('argv',[])
                and row.get('llvm_profdata_show',{}).get('returncode')==0,
                'official producer record/ELF/reader mode differs')
    require(qualified.get('merged_extbinary',{}).get('sha256')==profile['sha256'],
            'declared profile differs from official merged output')
    require(profile['path'] in dependencies,'profile path absent from producer closure')
    return dict(enabled=True,strategy='sample-profile',base_runtime=BASE,profile=profile,
                producer_provenance=proof,producer_files=list(dependencies.values()))

def compile_options(profile_path):
    return ['-fprofile-sample-use='+str(profile_path),*DIAGNOSTIC_FLAGS]


def archive_inputs(manifest, output):
    """Existing canonical freezer can call this for non-source compiler inputs.
    Keep original producer paths and independently verified immutable archive paths.
    """
    import shutil
    if 'host_sample_profile' not in manifest:return None
    sample=manifest['host_sample_profile'];output=Path(output)
    rows=[sample['compiled_profile'],*manifest['external_compiler_inputs']]
    unique={}
    for row in rows:
        checked=pin(row)
        require(checked['path'] not in unique or unique[checked['path']]==checked,'conflicting external compiler input')
        unique[checked['path']]=checked
    output.mkdir(parents=True,exist_ok=False);archived=[]
    for i,row in enumerate(sorted(unique.values(),key=lambda x:x['path'])):
        source=Path(row['path']);destination=output/(format(i,'04d')+'-'+source.name)
        shutil.copyfile(source,destination);copy=pin(dict(path=str(destination),sha256=row['sha256'],bytes=row['bytes']))
        archived.append(dict(source=row,archive=copy))
    path=output/'manifest.json';path.write_text(json.dumps(dict(strategy='sample-profile',files=archived),indent=2)+'\n')
    return record(path)
