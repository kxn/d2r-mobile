#!/usr/bin/env python3
"""Prepare static ASTC4 resources with one verified CASC context; no encoder/device."""
import argparse
import importlib.util
import json
import shutil
import subprocess
from pathlib import Path
from types import SimpleNamespace
import native_static_selection as selection_policy

_spec=importlib.util.spec_from_file_location('prepare_native_resource',Path(__file__).with_name('prepare-native-image-resource.py'))
resource=importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(resource)
MAX_BATCH_ENTRIES=128


def selected(logical,entry,array_members):
    family=(logical.startswith('data/hd/env/') and '/act1/' in logical) or logical.startswith(('data/hd/character/','data/hd/vfx/meshes/character/'))
    thumb=entry.get('thumbnailBase64')
    return (family and logical not in array_members and entry['format'] in (81,95)
            and entry['type']==2 and entry['depth']==1 and entry['layoutType']==3
            and 0<entry['width']<=4096 and 0<entry['height']<=4096
            and 0<entry['levels']<=16 and entry['layoutData']&255<=5
            and (not thumb or 0<entry['thumbnailMip']<entry['levels']))


def batch_text(entries):
    if not 0<len(entries)<=MAX_BATCH_ENTRIES:raise ValueError('Batch must contain 1..128 entries')
    ids=[e['id'] for e in entries]
    if len(set(ids))!=len(ids):raise ValueError('Duplicate batch entry')
    return ('DM_RESOURCE_BATCH_V1\n'+''.join(e['id']+'\t'+e['input']['sha256']+'\t'+e['source']['sha256']+'\n' for e in entries)).encode('ascii')


def stable_token(path):
    st=Path(path).stat()
    return (st.st_dev,st.st_ino,st.st_size,st.st_mtime_ns,st.st_ctime_ns)


def run(a):
    if a.output.exists():raise ValueError('New output only; partial output is not accepted as complete')
    if resource.file_sha(a.array_closure_report)!=a.array_closure_sha256:raise ValueError('Array census fingerprint mismatch')
    if resource.file_sha(a.full_astc_provenance)!=a.full_astc_provenance_sha256:raise ValueError('ASTC provenance fingerprint mismatch')
    closure=json.loads(a.array_closure_report.read_text())['array_format_closure']
    if not closure.get('complete',True):raise ValueError('Incomplete array census')
    arrays={resource.casc.norm_path(m) for g in closure['groups'] for m in g['ordered_members']}
    provenance=json.loads(a.full_astc_provenance.read_text())
    if not provenance['pack_verification']['matches'] or not all(r['matches'] for r in provenance['source_and_artifact_verification']):
        raise ValueError('Original ASTC audit did not verify its source/artifacts')
    context=resource.SourceContext(a)
    pinned={Path(r['archive']['path']):stable_token(r['archive']['path']) for r in context.common['verified_archives'].values()}
    if not any(r['archive']['sha256']==provenance['pack_verification']['sha256'] for r in context.common['verified_archives'].values()):
        raise ValueError('Source archive differs from original ASTC audit')
    explicit_path=getattr(a,'logical_manifest',None)
    if explicit_path:
        names,explicit_selection=selection_policy.explicit(explicit_path,a.logical_manifest_sha256,context,arrays)
        quality=selection_policy.quality_gate(provenance,names,getattr(a,'artifact_root',resource.ROOT))
        explicit_selection['original_quality']=quality
        explicit_selection['policy_tool']=resource.record(selection_policy.__file__)
        explicit_selection['artifact_root']=str(getattr(a,'artifact_root',resource.ROOT).resolve())
        choices=[(name,context.entries[name]) for name in names]
    else:
        if getattr(a,'logical_manifest_sha256',None):raise ValueError('Logical hash requires a manifest')
        choices=sorted((k,v) for k,v in context.entries.items() if selected(k,v,arrays))
    if len(choices)!=a.expected_count:raise ValueError(f'Selection count changed: {len(choices)} != {a.expected_count}')
    a.output.mkdir(parents=True)
    (a.output/'items').mkdir()
    (a.output/'batches').mkdir()
    proof=a.output/'source-context';proof.mkdir()
    original_lib=Path(context.common['NVN_layout_library']['path'])
    shutil.copy2(original_lib,proof/'nvn-deswizzle.so')
    context.common['NVN_layout_library']=resource.record(proof/'nvn-deswizzle.so')
    historical={}
    # Original encoder/CASC tool revisions are historical build inputs; do not
    # misidentify later live sources as those that produced the verified pack.
    for name in ('dm/tools/astc_pack.py','dm/tools/casc_overlay.py'):
        audit=next(r for r in provenance['source_and_artifact_verification'] if r['path']==name)
        data=subprocess.check_output(['git','-C',str(resource.ROOT),'show','afad65c895386a96dfb863f2317e41d61222a5a9:'+name])
        if resource.sha(data)!=audit['sha256']:raise ValueError('Historical ASTC tool fingerprint mismatch')
        target=proof/Path(name).name;target.write_bytes(data);historical[name]=resource.record(target)
    rows=[];specs=[]
    context.common['full_astc_provenance']={**resource.record(a.full_astc_provenance),'historical_tool_files':historical}
    manifest={'schema':1,'complete':False,'contract':'DM native resource batch preparation v1',
              'source_context':context.common,'selection':{'kind':'act1env-and-characters-excluding-declared-terrain-array','formats':[81,95],
              'logical_whitelist':['data/hd/env/ AND /act1/','data/hd/character/','data/hd/vfx/meshes/character/'],'array_closure':resource.record(a.array_closure_report),'expected_count':a.expected_count,
              'static_only':True,'thumbnail_tail_distinguishable':True,'unknown_runtime_array_uses_not_proven':True},
              'batch_max_entries':MAX_BATCH_ENTRIES,'entries':rows,'batches':[],
              'tool':resource.record(__file__),'encoder_used':False,'device_or_game_executed':False}
    if explicit_path:
        manifest['selection']={**manifest['selection'],**explicit_selection}
        manifest['selection'].pop('logical_whitelist')
    # Checkpoint rows are useful evidence only; complete appears exclusively after all entries.
    with (a.output/'prepare-entries.jsonl').open('x') as checkpoint:
        for logical,entry in choices:
            identity=resource.sha(logical.encode())
            out=a.output/'items'/identity
            args=SimpleNamespace(**vars(a));args.logical=logical;args.output=out
            result=resource.prepare(args,context)
            if result['original_NVNPadded_bytes']>resource.MAX_BYTES:raise ValueError('Original payload exceeds native whitelist')
            row={'id':identity,'logical':logical,'source':resource.record(out/'resource-source.json'),
                 'texture':resource.record(out/'source.texture'),'input':result['input'],
                 'prepare_report':resource.record(out/'prepare-report.json')}
            rows.append(row)
            specs.append({'source':row['source']['path'],'texture':row['texture']['path'],'baked':str(out.resolve())})
            checkpoint.write(json.dumps(row,sort_keys=True)+'\n');checkpoint.flush()
        for n,start in enumerate(range(0,len(rows),MAX_BATCH_ENTRIES)):
            entries=rows[start:start+MAX_BATCH_ENTRIES]
            path=a.output/'batches'/f'{n:04d}.tsv';path.write_bytes(batch_text(entries))
            manifest['batches'].append({'ordinal':n,'manifest':resource.record(path),'entry_ids':[r['id'] for r in entries]})
    if any(stable_token(p)!=t for p,t in pinned.items()):raise ValueError('Verified source archive changed during preparation')
    manifest['complete']=True
    manifest['totals']={'entries':len(rows),'input_bytes':sum(r['input']['bytes'] for r in rows),
                        'source_texture_bytes':sum(r['texture']['bytes'] for r in rows),
                        'max_input_bytes':max(r['input']['bytes'] for r in rows),
                        'max_device_entry_linear_or_native_bytes':resource.MAX_BYTES,
                        'native_payload_total_bytes':None,'native_layout_query_pending':True}
    (a.output/'entries.json').write_bytes(resource.canonical(specs))
    (a.output/'batch-prepare.json').write_bytes(resource.canonical(manifest))
    print(json.dumps({'complete':True,'entries':len(rows),'batches':len(manifest['batches']),
                      'manifest':resource.record(a.output/'batch-prepare.json')},indent=2))


def parser():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--romfs',type=Path,required=True);p.add_argument('--pack-index',type=Path,required=True)
    p.add_argument('--data-dir',type=Path,required=True);p.add_argument('--pack-manifest',type=Path,action='append',default=[])
    p.add_argument('--composition-report',type=Path)
    p.add_argument('--array-closure-report',type=Path,required=True);p.add_argument('--array-closure-sha256',required=True)
    p.add_argument('--full-astc-provenance',type=Path,required=True);p.add_argument('--full-astc-provenance-sha256',required=True)
    p.add_argument('--artifact-root',type=Path,default=resource.ROOT)
    p.add_argument('--logical-manifest',type=Path);p.add_argument('--logical-manifest-sha256')
    p.add_argument('--expected-count',type=int,required=True);p.add_argument('--output',type=Path,required=True)
    return p

if __name__=='__main__':run(parser().parse_args())
