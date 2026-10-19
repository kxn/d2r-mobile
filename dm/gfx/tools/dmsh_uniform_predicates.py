"""Optional additional integer predicates; preserve the old full-word .spec.

DPR1 adds a second uint specialization input. It changes only proved comparison
boolean results, never substitutes the source word's other numeric uses. The
runtime must read both current inputs without waiting and key complete tuples.
No shader keys, captured Uniform values, scenes or material rules enter policy.
"""
from pathlib import Path
import hashlib
import struct
import subprocess
import tempfile

MAGIC = 0x31525044  # DPR1, little endian
VERSION = 1
MODULE_OFFSET = 164
AND_EQ, AND_NE = 1, 2
MAX_PREDICATES = 4


def _parse(text):
    definitions, rows, decorations, members = {}, [], {}, {}
    for index, line in enumerate(text.splitlines()):
        tokens = line.split(';', 1)[0].split()
        if not tokens:
            continue
        result = None
        if len(tokens) > 2 and tokens[1] == '=':
            result, tokens = tokens[0], tokens[2:]
        op, args = tokens[0], tokens[1:]
        row = (result, op, args, index)
        rows.append(row)
        if result:
            definitions[result] = row
        if op == 'OpDecorate' and len(args) >= 2:
            decorations[(args[0], args[1])] = args[2:] or True
        if op == 'OpMemberDecorate' and len(args) >= 3:
            members[(args[0], args[1], args[2])] = args[3] if len(args) == 4 else True
    return definitions, rows, decorations, members


def _old_recipe(base, recipe):
    if len(recipe) < 80 or len(recipe) % 4:
        raise ValueError('short old specialization recipe')
    header = struct.unpack_from('<7I', recipe)
    if (header[0] not in (0x31505344, 0x32505344) or
            (header[1] != 1 and not (header[0] == 0x32505344 and header[1] == 257)) or
            header[2] >= 128 or not 5 <= header[6] <= (32 << 18) or not 3 <= header[3] < 19 or
            header[4] > 65532 or header[4] % 4 or header[5] > 60000 or
            len(recipe) != 60 + header[6] * 4 or
            recipe[28:60] != hashlib.sha256(base).digest()):
        raise ValueError('old specialization recipe mismatch')
    return header


def unpack(blob, base, recipe, proof=None):
    """Validate the fixed bounded wire format and its immutable input hashes."""
    old = _old_recipe(base, recipe)
    if len(blob) < MODULE_OFFSET + 20 or len(blob) % 4:
        raise ValueError('short predicate sidecar')
    h = struct.unpack_from('<9I', blob)
    if (h[:3] != (MAGIC, VERSION, 0) or h[3] > 60000 or h[4] >= 128 or not 5 <= h[8] <= (32 << 18) or h[3] == old[5] or
            not 3 <= h[5] < 19 or h[6] > 65532 or h[6] % 4 or
            not 1 <= h[7] <= MAX_PREDICATES or
            len(blob) != MODULE_OFFSET + h[8] * 4 or
            blob[36:68] != hashlib.sha256(base).digest() or
            blob[68:100] != hashlib.sha256(recipe).digest() or
            (h[4], h[5], h[6]) == (old[2], old[3], old[4])):
        raise ValueError('predicate sidecar header/input mismatch')
    predicates, bits = [], set()
    for index in range(MAX_PREDICATES):
        p = struct.unpack_from('<4I', blob, 100 + 16 * index)
        if index >= h[7]:
            if any(p):
                raise ValueError('nonzero unused predicate slot')
        else:
            if p[0] not in (AND_EQ, AND_NE) or p[3] >= 4 or p[3] in bits:
                raise ValueError('invalid predicate operation/output bit')
            bits.add(p[3]); predicates.append(p)
    module = blob[MODULE_OFFSET:]
    if struct.unpack_from('<I', module)[0] != 0x07230203:
        raise ValueError('invalid predicate SPIR-V')
    with tempfile.TemporaryDirectory(prefix='dmsh_pred_contract_') as temporary:
        path = Path(temporary) / 'module.spv'; path.write_bytes(module)
        text = subprocess.check_output(['spirv-dis','--raw-id',str(path)], text=True)
        definitions, instructions, _, _ = _parse(text)
        decorations = [(int(a[2]),a[0]) for _,op,a,_ in instructions
                       if op == 'OpDecorate' and len(a) == 3 and a[1] == 'SpecId']
        specs = dict(decorations)
        if len(specs) != len(decorations) or len({r for _,r in decorations}) != len(decorations):
            raise ValueError('duplicate predicate specialization decoration')
        if (set(specs) != {old[5], h[3]} or
                [a[0] for _,op,a,_ in instructions if op == 'OpEntryPoint'] != ['Fragment']):
            raise ValueError('predicate module stage/specialization IDs mismatch')
        for result in specs.values():
            constant = definitions.get(result)
            if (not constant or constant[1] != 'OpSpecConstant' or
                    definitions[constant[2][0]][1:3] != ('OpTypeInt', ['32','0'])):
                raise ValueError('predicate module requires full uint32 specialization')
    if proof is not None:
        expected = {'predicate_sha256': hashlib.sha256(blob).hexdigest(),
                    'base_sha256': hashlib.sha256(base).hexdigest(),
                    'old_specialization_sha256': hashlib.sha256(recipe).hexdigest(),
                    'module_sha256': hashlib.sha256(module).hexdigest(),
                    'binding': h[4], 'reg': h[5], 'offset': h[6], 'spec_id': h[3],
                    'predicates': [list(p) for p in predicates]}
        if any(proof.get(k) != v for k, v in expected.items()):
            raise ValueError('predicate sidecar proof mismatch')
    return module, predicates


def transform(base, recipe, eligible_bindings, minimum_weight=128):
    """Select one additional word by static branch score, not runtime values."""
    old = _old_recipe(base, recipe)
    with tempfile.TemporaryDirectory(prefix='dmsh_uniform_pred_') as temporary:
        root = Path(temporary); source = root / 'old.spv'
        source.write_bytes(recipe[60:])
        text = subprocess.check_output(['spirv-dis', '--raw-id', str(source)], text=True)
        defs, rows, decs, members = _parse(text)
        # Group-applied qualifiers are not represented by direct decorations.
        # Until flattening is an explicit compiler input, reject them entirely.
        if any(op in ('OpDecorationGroup','OpGroupDecorate','OpGroupMemberDecorate') for _,op,_,_ in rows):
            return None, {'skipped':'group decorations require explicit normalization'}
        if [a[0] for _, op, a, _ in rows if op == 'OpEntryPoint'] != ['Fragment']:
            return None, {'skipped': 'fragment only'}
        decorations = [(int(a[2]),a[0]) for _,op,a,_ in rows
                       if op == 'OpDecorate' and len(a) == 3 and a[1] == 'SpecId']
        specs = dict(decorations)
        if len(specs) != len(decorations) or len({r for _,r in decorations}) != len(decorations):
            return None, {'skipped':'duplicate existing specialization decoration'}
        if set(specs) != {old[5]}:
            return None, {'skipped': 'unexpected existing specialization IDs'}
        old_spec = defs.get(specs[old[5]])
        if (not old_spec or old_spec[1] != 'OpSpecConstant' or
                defs[old_spec[2][0]][1:3] != ('OpTypeInt', ['32', '0'])):
            return None, {'skipped': 'old selector is not full uint32'}
        uint = old_spec[2][0]
        boolean = next(r for r, op, _, _ in rows if op == 'OpTypeBool')
        constants = {r: int(a[1]) & 0xffffffff for r, op, a, _ in rows
                     if op == 'OpConstant' and defs[a[0]][1] == 'OpTypeInt' and
                     defs[a[0]][2][0] == '32'}
        def decoration(result, name):
            return decs.get((result, name))
        def qualified(results):
            return any(decoration(r,p) or any(k[0]==r and k[2]==p for k in members)
                       for r in results for p in ('Coherent','Volatile','RelaxedPrecision'))
        roots = {}
        for result, op, args, _ in rows:
            if op != 'OpVariable' or args[1] != 'Uniform' or decoration(result, 'DescriptorSet') != ['1']:
                continue
            binding = int((decoration(result, 'Binding') or ['-1'])[0])
            if not 0 <= binding < 128 or binding not in eligible_bindings or not 3 <= eligible_bindings[binding] < 19:
                continue
            ptr = defs[args[0]]; st = defs.get(ptr[2][1]) if ptr[1] == 'OpTypePointer' else None
            if (not st or st[1] != 'OpTypeStruct' or len(st[2]) != 1 or
                    members.get((st[0], '0', 'Offset')) != '0'):
                continue
            ar = defs[st[2][0]]
            if (ar[1] != 'OpTypeArray' or constants.get(ar[2][1]) != 4096 or
                    decoration(ar[0], 'ArrayStride') != ['16']):
                continue
            vector = defs[ar[2][0]]
            if vector[1] != 'OpTypeVector' or vector[2][1] != '4':
                continue
            scalar = defs[vector[2][0]]
            if scalar[1:3] != ('OpTypeFloat', ['32']):
                continue
            if qualified((result,ptr[0],st[0],ar[0],ar[2][1],vector[0],scalar[0])):
                continue
            roots[result] = (binding, eligible_bindings[binding], scalar[0])
        loads = {}
        for result, op, args, _ in rows:
            if op != 'OpLoad' or len(args) != 2:
                continue
            ptr = defs.get(args[1]); a = ptr[2] if ptr else []
            if (not ptr or ptr[1] not in ('OpAccessChain', 'OpInBoundsAccessChain') or
                    len(a) != 5 or a[1] not in roots or any(x not in constants for x in a[2:])):
                continue
            binding, reg, scalar = roots[a[1]]
            if (args[0] != scalar or constants[a[2]] != 0 or
                    not 0 <= constants[a[3]] < 4096 or not 0 <= constants[a[4]] < 4):
                continue
            word = (binding, reg, constants[a[3]] * 16 + constants[a[4]] * 4)
            if word == (old[2], old[3], old[4]) or qualified((result,ptr[0],ptr[2][0],*a[2:])):
                continue
            loads[result] = word
        candidates = []
        for result, op, args, index in rows:
            if op not in ('OpIEqual', 'OpINotEqual') or args[0] != boolean:
                continue
            left, right = args[1:]
            if left in constants:
                left, right = right, left
            if right not in constants:
                continue
            chain = [result,right]; value = defs.get(left); mask = 0xffffffff
            if value and value[1] == 'OpBitwiseAnd':
                chain.append(value[0]); x, y = value[2][1:]
                if x in constants: x, y = y, x
                if y not in constants: continue
                mask = constants[y]; chain.append(y); value = defs.get(x)
            if not value or value[1] != 'OpBitcast':
                continue
            chain.append(value[0]); integer = defs[value[2][0]]
            if integer[1] != 'OpTypeInt' or integer[2][0] != '32' or value[2][1] not in loads:
                continue
            # AND must retain the exact bitcast integer type, no truncation.
            if defs[left][1] == 'OpBitwiseAnd' and defs[left][2][0] != value[2][0]:
                continue
            if defs[right][2][0] != value[2][0]:
                continue
            chain.append(value[2][1])
            if qualified(chain + [value[2][0],boolean]):
                continue
            candidates.append({'condition': result, 'line': index,
                               'word': loads[value[2][1]], 'opcode': AND_EQ if op == 'OpIEqual' else AND_NE,
                               'and_mask': mask, 'comparison_value': constants[right]})
        # Use original static region scoring to prioritize only proved bools.
        blocks = {}; block = None
        for row in rows:
            if row[1] == 'OpLabel': block = row[0]; blocks[block] = []
            if block: blocks[block].append(row)
            if row[1] == 'OpFunctionEnd': block = None
        def successors(label):
            term = next((r for r in reversed(blocks[label]) if r[1] not in ('OpFunctionEnd','OpLine','OpNoLine')), None)
            if not term: return []
            op, a = term[1:3]
            return a[:1] if op == 'OpBranch' else a[1:3] if op == 'OpBranchConditional' else [a[1],*a[3::2]] if op == 'OpSwitch' else []
        weights = {}
        for instructions in blocks.values():
            for _, op, args, _ in instructions:
                if op != 'OpBranchConditional': continue
                merge = next((r[2][0] for r in instructions if r[1] in ('OpSelectionMerge','OpLoopMerge')), None)
                seen = set(); todo = args[1:3]
                while todo:
                    node = todo.pop()
                    if node == merge or node in seen or node not in blocks: continue
                    seen.add(node); todo.extend(successors(node))
                weights[args[0]] = weights.get(args[0], 0) + sum(len(blocks[n]) for n in seen)
        grouped = {}
        for c in candidates:
            if weights.get(c['condition'], 0):
                grouped.setdefault(c['word'], []).append(c)
        if not grouped:
            return None, {'skipped': 'no additional exact integer Uniform predicate'}
        # Deduplicate identical recipes and replace every matching bool result.
        groups = {}
        for word, values in grouped.items():
            recipes = {}
            for c in values:
                k = (c['opcode'], c['and_mask'], c['comparison_value'])
                recipes.setdefault(k, []).append(c)
            ranked = sorted(recipes.values(), key=lambda cs: (-sum(weights[c['condition']] for c in cs), cs[0]['line']))[:MAX_PREDICATES]
            groups[word] = sorted(ranked, key=lambda cs: cs[0]['line'])
        word = max(groups, key=lambda w: (sum(weights[c['condition']] for cs in groups[w] for c in cs), -w[0], -w[2]))
        chosen = groups[word]; weight = sum(weights[c['condition']] for cs in chosen for c in cs)
        if weight < minimum_weight:
            return None, {'skipped': 'additional predicate region below threshold', 'weight': weight}
        # Also replace identical comparisons not directly controlling a branch;
        # keeping their old result would be valid but miss exact shared uses.
        chosen_keys = {(cs[0]['opcode'], cs[0]['and_mask'], cs[0]['comparison_value']) for cs in chosen}
        for c in candidates:
            k = (c['opcode'], c['and_mask'], c['comparison_value'])
            if c['word'] == word and k in chosen_keys:
                cs = next(cs for cs in chosen if (cs[0]['opcode'],cs[0]['and_mask'],cs[0]['comparison_value']) == k)
                if c not in cs: cs.append(c)
        bound = struct.unpack_from('<I', recipe, 60 + 12)[0]
        def fresh():
            nonlocal bound
            result = '%' + str(bound); bound += 1; return result
        spec_id = next(x for x in range(60001) if x != old[5])
        spec = fresh(); zero = fresh(); mask_ids = [fresh() for _ in chosen]
        declarations = [f'{spec} = OpSpecConstant {uint} 0', f'{zero} = OpConstant {uint} 0'] + [f'{x} = OpConstant {uint} {1<<i}' for i,x in enumerate(mask_ids)]
        lines = text.splitlines(); predicates = []
        for bit, cs in enumerate(chosen):
            c = cs[0]; predicates.append((c['opcode'], c['and_mask'], c['comparison_value'], bit))
            for item in cs:
                temp = fresh()
                lines[item['line']] = f'{temp} = OpBitwiseAnd {uint} {spec} {mask_ids[bit]}\n{item["condition"]} = OpINotEqual {boolean} {temp} {zero}'
        first_function = next(i for i,l in enumerate(lines) if '= OpFunction ' in l)
        lines[first_function:first_function] = declarations
        first_type = next(i for i,l in enumerate(lines) if '= OpType' in l)
        lines.insert(first_type, f'OpDecorate {spec} SpecId {spec_id}')
        assembly = root / 'hybrid.spvasm'; binary = root / 'hybrid.spv'
        assembly.write_text('\n'.join(lines) + '\n')
        subprocess.run(['spirv-as','--target-env','vulkan1.1',str(assembly),'-o',str(binary)], check=True, capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(binary)], check=True, capture_output=True)
        module = binary.read_bytes()
        binding, reg, offset = word
        header = struct.pack('<9I', MAGIC, VERSION, 0, spec_id, binding, reg, offset, len(predicates), len(module)//4)
        slots = b''.join(struct.pack('<4I', *p) for p in predicates) + bytes(16 * (MAX_PREDICATES-len(predicates)))
        blob = header + hashlib.sha256(base).digest() + hashlib.sha256(recipe).digest() + slots + module
        proof = {'predicate_sha256': hashlib.sha256(blob).hexdigest(), 'base_sha256': hashlib.sha256(base).hexdigest(),
                 'old_specialization_sha256': hashlib.sha256(recipe).hexdigest(), 'module_sha256': hashlib.sha256(module).hexdigest(),
                 'binding': binding, 'reg': reg, 'offset': offset, 'spec_id': spec_id,
                 'predicates': [list(p) for p in predicates], 'branch_weight': weight,
                 'comparison_results': [c['condition'] for cs in chosen for c in cs],
                 'old_full_word_preserved': True, 'other_source_loads_preserved': True}
        unpack(blob, base, recipe, proof)
        return blob, proof
