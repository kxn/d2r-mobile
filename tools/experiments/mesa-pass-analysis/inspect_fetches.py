#!/usr/bin/env python3
"""Inspect existing SPIR-V buffer-fetch use paths; never infer runtime format."""
import argparse
from collections import Counter, defaultdict, deque
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

TRANSPARENT = {'OpCompositeExtract', 'OpCompositeConstruct', 'OpCompositeInsert',
    'OpVectorShuffle', 'OpVectorExtractDynamic', 'OpVectorInsertDynamic',
    'OpCopyObject', 'OpPhi', 'OpSelect', 'OpBitcast'}
INTEGER = {'OpBitwiseAnd', 'OpBitwiseOr', 'OpBitwiseXor', 'OpNot', 'OpBitFieldInsert',
    'OpBitFieldSExtract', 'OpBitFieldUExtract', 'OpBitReverse', 'OpBitCount',
    'OpShiftRightLogical', 'OpShiftRightArithmetic', 'OpShiftLeftLogical',
    'OpIEqual', 'OpINotEqual', 'OpUGreaterThan', 'OpSGreaterThan',
    'OpUGreaterThanEqual', 'OpSGreaterThanEqual', 'OpULessThan', 'OpSLessThan',
    'OpULessThanEqual', 'OpSLessThanEqual', 'OpIAdd', 'OpISub', 'OpIMul',
    'OpUDiv', 'OpSDiv', 'OpUMod', 'OpSRem', 'OpSMod', 'OpConvertSToF',
    'OpConvertUToF', 'OpAccessChain', 'OpInBoundsAccessChain'}

def inspect(text, descriptor_set, binding):
    defs, uses, decorations, counts = {}, defaultdict(list), {}, Counter()
    for num, line in enumerate(text.splitlines(), 1):
        tokens = line.split(';', 1)[0].split()
        if not tokens:
            continue
        result = None
        if len(tokens) > 2 and tokens[1] == '=':
            result, tokens = tokens[0], tokens[2:]
        op, args = tokens[0], tokens[1:]
        counts[op] += 1
        if op == 'OpDecorate' and len(args) == 3:
            decorations[(args[0], args[1])] = args[2]
        if result:
            defs[result] = (op, args, num, line.strip())
            for arg in args[1:]:
                if arg.startswith('%'):
                    uses[arg].append(result)

    def opaque_root(value, seen=None):
        seen = set() if seen is None else set(seen)
        if value in seen:
            return None
        seen.add(value)
        op, args, _, _ = defs.get(value, ('', [], 0, ''))
        if op == 'OpVariable':
            return value
        if op in ('OpLoad', 'OpImage', 'OpCopyObject'):
            return opaque_root(args[1], seen)
        return None

    roots = {value for value in defs
        if decorations.get((value, 'DescriptorSet')) == str(descriptor_set)
        and decorations.get((value, 'Binding')) == str(binding)}
    rows = []
    for fetch, (op, args, line_no, _) in defs.items():
        if op != 'OpImageFetch' or opaque_root(args[1]) not in roots:
            continue
        todo = deque([(fetch, [fetch])])
        seen, observed, numeric, unknown = {fetch}, [], [], []
        while todo:
            value, path = todo.popleft()
            for result in uses[value]:
                op, args, num, text_line = defs[result]
                chain = path + [result]
                if op in INTEGER:
                    observed.append({'operation': op, 'line': num,
                        'path': [defs[x][3] for x in chain]})
                elif op in TRANSPARENT:
                    if result not in seen:
                        seen.add(result)
                        todo.append((result, chain))
                elif op.startswith('OpF') or op in ('OpExtInst', 'OpDot',
                        'OpVectorTimesScalar', 'OpImageSampleImplicitLod',
                        'OpImageSampleExplicitLod'):
                    numeric.append({'operation': op, 'line': num})
                else:
                    unknown.append({'operation': op, 'line': num, 'instruction': text_line})
        rows.append({'fetch': fetch, 'line': line_no,
            'direct_integer_observation_proved': bool(observed),
            'integer_paths': observed[:8], 'first_numeric_uses': numeric[:8],
            'unclassified_uses': unknown[:8]})
    return {'set': descriptor_set, 'binding': binding, 'fetch_count': len(rows),
        'direct_integer_observation_count': sum(r['direct_integer_observation_proved'] for r in rows),
        'fetches': rows, 'normalized_opcode_counts': dict(counts),
        'limits': ['A transparent path reaching integer operations proves raw-bit observability.',
            'No such path is not proof of semantic equivalence or safe numeric-only replacement.',
            'No runtime format, descriptor range, resource identity, or dynamic execution count is inferred.']}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_map', type=Path)
    parser.add_argument('--native-prefix', action='append', required=True)
    parser.add_argument('--set', type=int, required=True)
    parser.add_argument('--binding', type=int, required=True)
    parser.add_argument('--spec', help='Existing specialization defaults, e.g. 0:1')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source_map = json.loads(args.source_map.read_text())
    reports = []
    for prefix in args.native_prefix:
        entries = [e for e in source_map['entries'] if e['actual_native_hash'].startswith(prefix)]
        if len(entries) != 1 or len(entries[0]['exact_original_modules']) != 1:
            raise ValueError('Native prefix must resolve to one exact original module')
        entry = entries[0]
        module = entry['exact_original_modules'][0]
        raw = Path(module['path']).read_bytes()[module['SPIRV_offset']:]
        if hashlib.sha256(raw).hexdigest() != module['SPIRV_sha256']:
            raise ValueError('Exact original module identity changed')
        with tempfile.TemporaryDirectory(prefix='mesa_fetch_readonly_') as temporary:
            folder = Path(temporary)
            source, normal = folder/'original.spv', folder/'normalized.spv'
            source.write_bytes(raw)
            options = []
            if args.spec:
                options += ['--set-spec-const-default-value', args.spec, '--freeze-spec-const']
            options += ['--inline-entry-points-exhaustive', '--ssa-rewrite',
                '--simplify-instructions', '--eliminate-dead-code-aggressive']
            subprocess.run(['spirv-opt', *options, str(source), '-o', str(normal)],
                check=True, capture_output=True)
            subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(normal)],
                check=True, capture_output=True)
            text = subprocess.check_output(['spirv-dis', '--raw-id', str(normal)], text=True)
            reports.append({'actual_native_hash': entry['actual_native_hash'],
                'exact_original_module': module, 'official_normalization_options': options,
                'normalized_sha256': hashlib.sha256(normal.read_bytes()).hexdigest(),
                'analysis': inspect(text, args.set, args.binding)})
    args.output.write_text(json.dumps({'scope': 'Read-only existing SPIR-V use-def inspection; no rewritten shader emitted or deployed',
        'reports': reports, 'source_map_sha256': hashlib.sha256(args.source_map.read_bytes()).hexdigest(),
        'tool_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}, indent=2)+'\n')

if __name__ == '__main__':
    main()
