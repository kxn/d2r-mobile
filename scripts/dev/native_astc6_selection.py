"""Select existing quality-approved ASTC6 replacements, never encode or infer native layout."""
import json
import hashlib
import importlib.util
import sys
from pathlib import Path
import native_static_selection as policy

KIND='explicit-qualified-ASTC6-native-replacement-v1'
QUALITY={'min_rgb_psnr':32.0,'max_alpha_error':8,'max_alpha_coverage_fraction':0.001,'constant_alpha_max_error':0}


def normal(name):
    return name.rsplit('/',1)[-1].endswith(('_nrm.texture','_normal.texture','_nrm$nrm.texture'))


def eligible(entry):
    # The original shape/thumbnail/array limits remain authoritative. Only the
    # format pair differs, after all ASTC6 provenance and quality gates below.
    return entry.get('format') in (99,85) and policy.eligible({**entry,'format':{99:95,85:81}[entry['format']]})


def select(path,expected_sha,context,arrays,composition_path):
    require=policy.require
    require(policy.sha(path)==expected_sha,'ASTC6 selection fingerprint differs')
    value=json.loads(Path(path).read_text())
    require(value['schema']==1 and value['kind']==KIND,'unsupported ASTC6 replacement selection')
    names=value['selected_logicals']
    require(isinstance(names,list) and names and names==sorted(set(names)),'ASTC6 selection must be nonempty sorted unique')
    comp_path=policy.pinned(value['qualified_composition']);comp=json.loads(comp_path.read_text())
    require(composition_path and comp_path.resolve()==Path(composition_path).resolve(),'ASTC6 CASC context must use qualified composition')
    require(comp['complete'] is True and comp['source_clean'] is True and comp['unselected_descriptors_and_index_exact'] is True
            and comp['UI_and_full4_archive_exact'] is True and comp['normal_entries_preserved']>0
            and comp['TVFS_proof']['VFS_only_selected_CFT_fields_changed'] is True,'ASTC6 composition preservation proof missing')
    closure=comp['array_format_closure']
    require(closure['fixed_point'] is True,'ASTC6 array closure incomplete')
    declared={policy.logical_name(m.lower()) for g in closure['groups'] for m in g['ordered_members']}
    require(declared==arrays,'ASTC6 array census differs')
    require(context.common['source_index']['sha256']==comp['merged_index_sha256'] and
            context.cache_info['sha256']==comp['final_cache_sha256'],'ASTC6 source cache/index differs')
    job_path=policy.pinned(value['encoder_job']);report_path=policy.pinned(value['encoder_report'])
    for p in [job_path,report_path]:
        require(comp['inputs'].get(str(p.resolve()))==policy.sha(p),'ASTC6 composition encoder input differs')
    job=json.loads(job_path.read_text());report=json.loads(report_path.read_text())
    require(report['complete'] is True and comp['batch_job_key']==job['job_key']==report['job_key'],'ASTC6 encoder job identity differs')
    require(job['quality_policy']==QUALITY,'ASTC6 original quality policy differs')
    old_path=policy.pinned(value['original_native_source_pack']);old=json.loads(old_path.read_text())
    require(old['schema']==1 and old['complete'] is True and old['encoder_used'] is False
            and old['selection']['formats']==[81,95],'historical ASTC4 source pack differs')
    old_context=old['source_context']
    require(job['source']['manifest_sha256']==old_context['original_build_manifest']['sha256']==context.common['original_build_manifest']['sha256']
            and job['baseline']['full4_cache_sha256']==old_context['source_cache']['sha256']
            and job['baseline']['full4_index_sha256']==old_context['source_index']['sha256'],'original ASTC4/ASTC6 source ancestry differs')
    previous={r['logical']:r for r in old['entries']};quality={r['filename']:r for r in report['results']}
    require(len(previous)==len(old['entries']) and len(quality)==len(report['results']),'duplicate source or quality logical')
    accepted=set(comp['selected']);require(len(accepted)==len(comp['selected']),'duplicate composition selection')
    # Reuse the original quality evaluator over original per-mip metrics. No
    # decoding/scoring/encoding is introduced by this preparation tool.
    evaluator=policy.pinned(value['quality_evaluator'])
    expected=next(sha for name,sha in job['tool_hashes'] if name=='astc_bulk.py')
    require(policy.sha(evaluator)==expected,'original ASTC6 quality evaluator fingerprint differs')
    spec=importlib.util.spec_from_file_location('native_original_astc6_quality',evaluator)
    quality_tool=importlib.util.module_from_spec(spec)
    search=sys.path[:]
    try:
        sys.path.insert(0,str(evaluator.parent));spec.loader.exec_module(quality_tool)
    finally:sys.path[:]=search
    proofs=[]
    for name in names:
        policy.logical_name(name)
        require(not normal(name) and name not in arrays,'normal/declared array is excluded')
        require(name in accepted and name in previous and name in quality and name in context.entries,'ASTC6 replacement lacks qualified membership')
        entry=context.entries[name];require(eligible(entry),'unsupported ASTC6 single-layer descriptor')
        item=quality[name]
        require(item['status']=='converted' and item['baseline_preserved'] is False and item['quality_failures']==[]
                and item['job_key']==job['job_key'],'ASTC6 quality fallback is not accepted')
        directory=job_path.parent/'items'/hashlib.sha256(name.encode()).hexdigest()
        records={}
        for filename in ['descriptor.json','quality.json','texture']:
            p=directory/filename;require(policy.sha(p)==item['files'][filename],'ASTC6 original item fingerprint differs')
            records[filename]=policy.record(p)
        require(json.loads((directory/'descriptor.json').read_text())==entry,'ASTC6 descriptor differs from encoded artifact')
        before=json.loads(policy.pinned(previous[name]['source']).read_text())
        original=before['source_descriptor']
        require(original['format']=={99:95,85:81}[entry['format']] and
                {k:v for k,v in original.items()if k not in ('format','thumbnailBase64')}==
                {k:v for k,v in entry.items()if k not in ('format','thumbnailBase64')},'replacement changes original descriptor topology')
        q=json.loads((directory/'quality.json').read_text())
        require(len(q['mips'])==entry['levels'] and quality_tool.quality_failures(q,job['quality_policy'])==[],
                'ASTC6 original per-mip RGB/alpha quality rejected')
        # CASC must contain the exact approved output, not merely a descriptor
        # claiming the same format. This is one bounded resource read/hash.
        data,_=context.read(name)
        require(hashlib.sha256(data).hexdigest()==item['files']['texture'],'ASTC6 CASC payload differs from approved output')
        proofs.append({'logical':name,'original_source':previous[name]['source'],'encoded_artifacts':records})
    info={'kind':KIND,'formats':[99,85],'logical_manifest':policy.record(path),
          'qualified_composition':value['qualified_composition'],'encoder_job':value['encoder_job'],
          'encoder_report':value['encoder_report'],'quality_evaluator':value['quality_evaluator'],'original_native_source_pack':value['original_native_source_pack'],
          'original_quality_policy':job['quality_policy'],'quality_item_proofs':proofs,
          'native_layout_query_pending':True,'static_file_origin_is_not_runtime_immutability':True}
    return names,info
