#!/usr/bin/env python3
"""Read-only reuse of the existing selector qualification on actual hot VS SPIR-V."""
import argparse
import hashlib
import importlib.util
import inspect
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
ORIGINAL=ROOT/'dm/gfx/tools/dmsh_uniform_specialization.py'

def analyzer():
    spec=importlib.util.spec_from_file_location('existing_selector_qualifier',ORIGINAL);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    source=inspect.getsource(m.transform);source=source[:source.index('        if scores[word]<minimum_weight:')]
    source=source.replace('def transform(','def readonly_vs_selector_qualification(',1).replace("decorations.get((rid,'DescriptorSet'))!='1'","decorations.get((rid,'DescriptorSet'))!='0'")
    source=source.replace("        if not scores:return None,{'skipped':'no one-word Uniform branch selector'}", '''        if not scores:
            def pointer_chain(value, seen=None):
                seen=set() if seen is None else set(seen)
                if value in seen:return {'value':value,'operation':'cycle'}
                seen.add(value);d=defs.get(value)
                if not d:return {'value':value,'operation':'unknown'}
                _,op,a,_,_=d
                row={'value':value,'operation':op,'operands':a}
                if op=='OpVariable':
                    row.update(storage=a[1],descriptor_set=decorations.get((value,'DescriptorSet')),binding=decorations.get((value,'Binding')))
                elif op in ('OpAccessChain','OpInBoundsAccessChain','OpCopyObject'):
                    row['root']=pointer_chain(a[1],seen)
                return row
            def blockers(value, seen=None):
                seen=set() if seen is None else set(seen)
                if value in seen:return [{'value':value,'operation':'cycle'}]
                seen.add(value);d=defs.get(value)
                if not d:return [{'value':value,'operation':'unknown'}]
                _,op,a,_,_=d
                if value in loads or op in ['OpConstant','OpConstantTrue','OpConstantFalse','OpConstantNull']:return []
                if op in ['OpLoad','OpPhi','OpFunctionCall','OpVariable','OpUndef','OpLabel','OpExtInstImport','OpFunction'] or op.startswith(('OpType','OpImage','OpGroup','OpDP','OpFwidth','OpAtomic')):
                    row={'value':value,'operation':op,'operands':a}
                    if op=='OpLoad':row['pointer']=pointer_chain(a[1])
                    return [row]
                result=[]
                for v in (a[3:] if op=='OpExtInst' else a[1:]):
                    if v.startswith('%'):result.extend(blockers(v,seen))
                return result
            branches=[]
            for _,op,a,b,_ in instructions:
                if op!='OpBranchConditional':continue
                words=dependencies(a[0]);branches.append({'condition':a[0],
                    'proved_words':None if words is None else sorted(words),
                    'first_unproved_dependencies':blockers(a[0])})
            return None,{'skipped':'no one-word Uniform branch selector','normalized_branches':branches,
                'qualified_static_scalar_load_count':len(loads)}''')
    source+='''        return None, {'eligible_at_existing_threshold': scores[word]>=minimum_weight,
            'minimum_weight':minimum_weight,'best_binding':binding,'best_offset':offset,
            'best_branch_region_weight':scores[word],
            'all_candidates':[{'binding':k[0],'offset':k[1],'scalar_type_id':k[2],'branch_region_weight':v} for k,v in sorted(scores.items())]}
'''
    namespace=dict(m.__dict__);exec(compile(source,'<read-only-derived-VS-qualifier>','exec'),namespace)
    return namespace['readonly_vs_selector_qualification'],source

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-map',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();qualifier,derived=analyzer();source_map=json.loads(a.source_map.read_text())
    modules={m['path']:m for e in source_map['entries'] if e['stage']=='VERT' for m in e['exact_original_modules']};results=[]
    for path,binding in sorted(modules.items()):
        file=Path(path);code=file.read_bytes()[binding['SPIRV_offset']:];require=hashlib.sha256(code).hexdigest()==binding['SPIRV_sha256']
        if not require:raise ValueError('actual module bytes changed')
        if file.suffix!='.spv' or binding['SPIRV_offset']:raise ValueError('VS qualification requires exact original base module')
        meta_path=file.with_suffix('.json');meta=json.loads(meta_path.read_text())
        if meta['stage']!='vs' or meta['descriptor_set']!=0 or meta['spirv_sha256']!=binding['SPIRV_sha256']:raise ValueError('actual VS reflection source mismatch')
        eligible={b['binding']:b['slot'] for b in meta['cbufs'] if 3<=b['slot']<19}
        text=subprocess.check_output(['spirv-dis','--raw-id',str(file)],text=True)
        _,proof=qualifier(code,eligible)
        results.append({'original_SPIRV':binding,'reflection_sha256':hashlib.sha256(meta_path.read_bytes()).hexdigest(),'source_declared_label':meta.get('source'),'base_OpBranchConditional_count':text.count('OpBranchConditional'),'eligible_uniform_binding_to_reg':eligible,'qualification':proof})
    out={'scope':'Only qualification/CFG budget; no transformed SPIR-V, shader recipe, game or production source modification','source_map_sha256':hashlib.sha256(a.source_map.read_bytes()).hexdigest(),'reused_qualifier':{'path':str(ORIGINAL),'sha256':hashlib.sha256(ORIGINAL.read_bytes()).hexdigest(),'analysis_only_changes':['DescriptorSet1→0 for VS inspection','return candidate weights before transformation; no SpecConstant or module emitted'],'derived_function_sha256':hashlib.sha256(derived.encode()).hexdigest()},'weight_limit':'Static normalized SPIR-V branch-region instruction count, not native instruction savings or runtime selection frequency','modules':results,'tools':{tool:hashlib.sha256(Path('/usr/bin',tool).read_bytes()).hexdigest() for tool in ['spirv-dis','spirv-opt']}}
    a.output.write_text(json.dumps(out,indent=2,sort_keys=True)+'\n');a.output.with_suffix('.derived.py').write_text(derived);print(json.dumps({Path(r['original_SPIRV']['path']).stem:r['qualification'] for r in results}))
