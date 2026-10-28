"""Bounded readonly Uniform prefetch with complete uint32 fallback.
No game keys or bindings are embedded. Range and cross-stage readonly
admission are required before execution.
"""
from pathlib import Path
import hashlib,json,re,subprocess
import dmsh_one_word_ubo as selector

def command(argv, data=None):
    return subprocess.run(argv, input=data, capture_output=True, check=True).stdout

def selection_equivalence():
    z=selector.z3
    index,component=z.BitVecs('index component',32)
    memory=z.Array('stable_uniform_words',z.BitVecSort(32),z.BitVecSort(32))
    original=z.Select(memory,(index<<2)+component)
    selected=z.If(index==7,z.Select(memory,28+component),
        z.If(index==8,z.Select(memory,32+component),
        z.If(index==9,z.Select(memory,36+component),z.Select(memory,40+component))))
    candidate=z.If(z.And(z.UGE(index,7),z.ULE(index,10)),selected,original)
    solver=z.Solver();solver.set(timeout=30000)
    solver.add(z.ULE(component,3),candidate!=original)
    result=solver.check()
    if result!=z.unsat:raise ValueError('Complete32-index selection equivalence failed: '+str(result))
    return {'solver':z.get_version_string(),'index_domain':'all uint32','payload_domain':'all uint32 bits',
            'counterexample':'UNSAT','fallback_preserved':True}

def transform(text, minimum_descriptor_bytes):
    if minimum_descriptor_bytes < 176:
        raise ValueError('No production range contract: added reads require bytes [112,176)')
    rows, defs, const, blocks = selector.parse(text)
    # Reuse the existing exact arbitrary-payload selector proof/qualification.
    _, word_proof = selector.transform(text)
    loads = word_proof['original_loads']
    pointers = [defs[x][1][1] for x in loads]
    chains = [defs[p][1] for p in pointers]
    block = defs[loads[0]][3]
    first = min(defs[x][2] for x in loads)
    last = max(defs[x][2] for x in loads)
    assert all(defs[x][3] == block for x in loads)
    assert all(chains[c][:-1] == chains[0][:-1] and const[chains[c][-1]] == c for c in range(4))
    assert all(defs[x][2] >= last for x, (op, a, _, _) in defs.items()
               if any(v in a for v in loads))
    pointer_type, uniform, member, index, component0 = chains[0]
    float_type = defs[loads[0]][1][0]
    vec4 = next(x for x, (op, a, _, _) in defs.items()
                if op == 'OpTypeVector' and a == [float_type, '4'])
    index_type = defs[index][1][0]
    bool_type = next(x for x, (op, a, _, _) in defs.items() if op == 'OpTypeBool')
    entry_fn = next(a[1] for _, _, op, a, _ in rows if op == 'OpEntryPoint')
    fnline = defs[entry_fn][2]
    entry = next(r for _, r, op, _, _ in rows[fnline+1:] if op == 'OpLabel')
    # Real CFG and dominators, restricted to this one entry function.
    fnend = next(i for i in range(fnline + 1, len(rows)) if rows[i][2] == 'OpFunctionEnd')
    fnblocks = {r for _, r, op, _, _ in rows[fnline:fnend] if op == 'OpLabel'}
    succ = {}
    for b in fnblocks:
        _, _, op, a, _ = rows[blocks[b][-1]]
        succ[b] = a if op == 'OpBranch' else a[1:3] if op == 'OpBranchConditional' else (
            a[1:2] + a[3::2] if op == 'OpSwitch' else [])
    reach = {entry}
    while True:
        new = reach | {y for x in reach for y in succ[x]}
        if new == reach: break
        reach = new
    pred = {b: {x for x in reach if b in succ[x]} for b in reach}
    dom = {b: ({entry} if b == entry else set(reach)) for b in reach}
    while True:
        new = {b: ({entry} if b == entry else {b} | set.intersection(*(dom[p] for p in pred[b]))) for b in reach}
        if new == dom: break
        dom = new
    exits = {b for b in reach if not succ[b]}
    # Not required for this preconditioned hypothesis, but report actual scope.
    dominates_all_exits = all(block in dom[x] for x in exits)
    next_id = max(int(x[1:]) for x in defs if re.fullmatch(r'%\d+', x)) + 1
    def fresh():
        nonlocal next_id
        out = '%' + str(next_id); next_id += 1; return out
    boolvec = fresh(); vecptr = fresh()
    constants = [fresh() for _ in range(4)]
    types = [f'{boolvec} = OpTypeVector {bool_type} 4', f'{vecptr} = OpTypePointer Uniform {vec4}']
    types += [f'{c} = OpConstant {index_type} {i}' for c, i in zip(constants, range(7, 11))]
    preloads = []; values = []
    for ci in constants:
        p, value = fresh(), fresh(); values.append(value)
        preloads += [f'{p} = OpAccessChain {vecptr} {uniform} {member} {ci}', f'{value} = OpLoad {vec4} {p}']
    low, high, condition = fresh(), fresh(), fresh()
    hit, miss, merge = fresh(), fresh(), fresh()
    insert = [f'{low} = OpUGreaterThanEqual {bool_type} {index} {constants[0]}',
              f'{high} = OpULessThanEqual {bool_type} {index} {constants[3]}',
              f'{condition} = OpLogicalAnd {bool_type} {low} {high}',
              f'OpSelectionMerge {merge} DontFlatten', f'OpBranchConditional {condition} {hit} {miss}',
              f'{hit} = OpLabel']
    selected = values[3]
    for c in [2, 1, 0]:
        test, tests, value = fresh(), fresh(), fresh()
        insert += [f'{test} = OpIEqual {bool_type} {index} {constants[c]}',
                   f'{tests} = OpCompositeConstruct {boolvec} {test} {test} {test} {test}',
                   f'{value} = OpSelect {vec4} {tests} {values[c]} {selected}']
        selected = value
    insert += [f'OpBranch {merge}', f'{miss} = OpLabel']
    fallback = []
    for c in range(4):
        p, value = fresh(), fresh(); fallback.append(value)
        insert += [f'{p} = OpAccessChain ' + ' '.join(chains[c]), f'{value} = OpLoad {float_type} {p}']
    fv, phi = fresh(), fresh()
    insert += [f'{fv} = OpCompositeConstruct {vec4} ' + ' '.join(fallback),
               f'OpBranch {merge}', f'{merge} = OpLabel',
               f'{phi} = OpPhi {vec4} {selected} {hit} {fv} {miss}']
    insert += [f'{old} = OpCompositeExtract {float_type} {phi} {c}' for c, old in enumerate(loads)]
    output = []; entered = False; inserted = False
    for i, (line, result, op, a, b) in enumerate(rows):
        if i == fnline: output.extend(types)
        if b == entry:
            if op == 'OpLabel': entered = True
            elif entered and not inserted and op not in ['OpVariable', 'OpLine', 'OpNoLine']:
                output.extend(preloads); inserted = True
        if i == first: output.extend(insert); continue
        if result in loads: continue
        if op == 'OpPhi':
            a = list(a)
            for j in range(2, len(a), 2):
                if a[j] == block: a[j] = merge
            line = f'{result} = OpPhi ' + ' '.join(a)
        output.append(line)
    assert inserted
    proof = {'descriptor_minimum_bytes_precondition': minimum_descriptor_bytes,
             'complete32_selection_equivalence':selection_equivalence(),
             'production_admissible': False,
             'not_production_reason': 'No runtime range/readonly admission implemented; standalone source hypothesis only',
             'read_only_precondition': 'Uniform bytes stable for draw, no aliased shader write/atomic/volatile/coherent access',
             'constant_byte_intervals': [[112 + 16*i, 128 + 16*i] for i in range(4)],
             'added_constant_vec4_loads': 4, 'original_dynamic_scalar_loads_preserved_in_fallback': 4,
             'original_dynamic_index': index, 'original_uniform_variable': uniform,
             'original_loads': loads, 'original_block': block,
             'entry_dominates_original': entry in dom[block],
             'original_block_dominates_all_reachable_exits': dominates_all_exits,
             'main_reachable_blocks': len(reach), 'main_exit_blocks': sorted(exits),
             'bit_payloads_preserved': 'Only vector selects/extracts; original float bitcasts/masks/shift users unchanged',
             'index_domain': 'Complete uint32; outside [7,10] original scalar accesses used, no finite assumption',
             'original_word_selector_proof': word_proof}
    return '\n'.join(output) + '\n', proof

def build(source, output, minimum=176):
    data = source.read_bytes()
    text = command(['spirv-dis', '--raw-id', '-o', '-', '-'], data).decode()
    transformed, proof = transform(text, minimum)
    output.with_suffix('.before-dce.spvasm').write_text(transformed)
    result = command(['spirv-as', '--preserve-numeric-ids', '--target-env', 'spv1.3', '-o', '-', '-'], transformed.encode())
    command(['spirv-val', '--target-env', 'vulkan1.1', '-'], result)
    # DCE only. No inlining, branch flattening, load speculation or reordering.
    result = command(['spirv-opt', '--eliminate-dead-code-aggressive', '-o', '-', '-'], result)
    command(['spirv-val', '--target-env', 'vulkan1.1', '-'], result)
    output.write_bytes(result)
    output.with_suffix('.spvasm').write_bytes(command(['spirv-dis', '--raw-id', '-o', '-', '-'], result))
    proof.update(input_sha256=hashlib.sha256(data).hexdigest(),
                 candidate_sha256=hashlib.sha256(result).hexdigest(),
                 input_bytes=len(data), candidate_bytes=len(result),
                 tool_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                 original_selector_tool_sha256=hashlib.sha256(Path(selector.__file__).read_bytes()).hexdigest(),
                 spirv_val='PASS vulkan1.1, pre and post dead-code cleanup')
    output.with_suffix('.proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    return proof

