#!/usr/bin/env python3
"""Read-only reuse of original C1/DPR1 generators with official constant folding."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'dm/gfx/tools'))
import dmsh_constants
import dmsh_uniform_predicates

def normalized(code,defaults):
    with tempfile.TemporaryDirectory(prefix='mesa_static_budget_') as temporary:
        source=Path(temporary)/'source.spv';result=Path(temporary)/'folded.spv'
        source.write_bytes(code)
        options=['--set-spec-const-default-value',defaults,'--freeze-spec-const','-O']
        subprocess.run(['spirv-opt',*options,str(source),'-o',str(result)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(result)],check=True,capture_output=True)
        text=subprocess.check_output(['spirv-dis','--raw-id',str(result)],text=True)
        counts=Counter()
        for line in text.splitlines():
            tokens=line.split(';',1)[0].split()
            if len(tokens)>2 and tokens[1]=='=':tokens=tokens[2:]
            if tokens:counts[tokens[0]]+=1
        return {'SPIRV_sha256':hashlib.sha256(result.read_bytes()).hexdigest(),
            'total_SPIRV_operations_including_declarations':sum(counts.values()),
            'opcode_counts':dict(counts),'official_normalization_options':options}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source_map',type=Path)
    p.add_argument('--native-prefix',action='append',required=True)
    p.add_argument('--spec0',type=int,required=True,help='Actual known first selector bits; not inferred by this tool')
    p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    mapping=json.loads(args.source_map.read_text());reports=[]
    for prefix in args.native_prefix:
        entries=[e for e in mapping['entries']if e['actual_native_hash'].startswith(prefix)]
        if len(entries)!=1 or entries[0]['stage']!='FRAG' or len(entries[0]['exact_original_modules'])!=1:
            raise ValueError('Require a unique exact current fragment module')
        e=entries[0];m=e['exact_original_modules'][0];path=Path(m['path'])
        if path.suffix!='.spec' or m['SPIRV_offset']!=60:raise ValueError('Require the original existing .spec container')
        recipe=path.read_bytes();code=recipe[60:];base=path.with_suffix('.spv').read_bytes()
        if hashlib.sha256(code).hexdigest()!=m['SPIRV_sha256'] or hashlib.sha256(recipe).hexdigest()!=m['file_sha256']:
            raise ValueError('Original module identity changed')
        meta=json.loads(path.with_suffix('.json').read_text())
        if meta['spirv_sha256']!=hashlib.sha256(base).hexdigest():raise ValueError('Base metadata identity changed')
        eligible={b['binding']:b['slot']for b in meta['cbufs']if 3<=b['slot']<19}
        blob,proof=dmsh_uniform_predicates.transform(base,recipe,eligible)
        defaults=f'0:{args.spec0}';baseline=normalized(code,defaults)
        c1_binding=next((b['binding']for b in meta['cbufs']if b['slot']==1),None)
        inline,folds=dmsh_constants.inline_c1(code,c1_binding,bytes.fromhex(meta['c1']),1)
        report={'actual_native_hash':e['actual_native_hash'],'exact_original_module':m,
            'existing_spec0_bits':args.spec0,'baseline':baseline,'C1_static_loads_replaced':folds,
            'C1_inlined':normalized(inline,defaults),'DPR1_qualification':proof,'all_DPR1_masks':[]}
        if blob:
            module,predicates=dmsh_uniform_predicates.unpack(blob,base,recipe,proof)
            for mask in range(1<<len(predicates)):
                report['all_DPR1_masks'].append({'mask':mask,
                    'static_folded':normalized(module,defaults+f" {proof['spec_id']}:{mask}")})
        reports.append(report)
    source_files=[Path(__file__),ROOT/'dm/gfx/tools/dmsh_constants.py',ROOT/'dm/gfx/tools/dmsh_uniform_predicates.py',args.source_map]
    args.output.write_text(json.dumps({'scope':'Static qualification/candidate code-size budget only; no runtime mask distribution, native ISA, GPU cycles or FPS measured; no shader/cache emitted',
        'important_limit':'The original dynamic shader already skips inactive branches. Static DCE is not a claim that their full operation cost was executed before.',
        'reports':reports,'inputs':{str(f.resolve()):hashlib.sha256(f.read_bytes()).hexdigest()for f in source_files}},indent=2)+'\n')

if __name__=='__main__':main()
