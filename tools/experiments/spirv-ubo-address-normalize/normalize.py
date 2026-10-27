#!/usr/bin/env python3
"""Normalize proved 32-bit address arithmetic using SPIRV-Tools for I/O.

No memory access, range, precision, robustness policy or interface is changed.
This is an independent offline candidate, not part of the published shader chain.
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

MASK = (1 << 32) - 1


@dataclass
class Instruction:
    line: int
    result: str
    op: str
    args: list


def parse(text):
    definitions = {}
    for line, raw in enumerate(text.splitlines()):
        tokens = raw.partition(';')[0].split()
        if len(tokens) >= 3 and tokens[1] == '=':
            definitions[tokens[0]] = Instruction(line, tokens[0], tokens[2], tokens[3:])
    return definitions


def trailing_zeroes(value):
    value &= MASK
    return 32 if value == 0 else (value & -value).bit_length() - 1


def normalize_assembly(text, first_free_id):
    """Rewrite ((x >> s) + c) >> t with a no-carry low-bit proof.

    For unsigned bit patterns, x divisible by 2**(s+t), 0<=c<2**t,
    and 0<s,t,s+t<32, this equals x>>(s+t). The same rule holds for a
    known low residue whose shifted value plus c cannot carry. With c=0 it
    holds for every x. All arithmetic remains
    modulo 2**32; signed/unsigned *bitcasts* preserve those bits. Arithmetic
    shifts/division, vector operations, specialization constants and unknown
    alignment are deliberately not inferred.
    """
    lines = text.splitlines()
    definitions = parse(text)
    integer_types = {r for r, d in definitions.items()
                     if d.op == 'OpTypeInt' and d.args[0] == '32'}

    def int32(value):
        d = definitions.get(value)
        return bool(d and d.args and d.args[0] in integer_types)

    def constant(value):
        d = definitions.get(value)
        if not d or d.op != 'OpConstant' or not int32(value):
            return None
        try:
            return int(d.args[1], 0) & MASK
        except (ValueError, IndexError):
            return None

    def unalias(value):
        seen = set()
        while value not in seen:
            seen.add(value)
            d = definitions.get(value)
            if (not d or d.op not in ('OpCopyObject', 'OpBitcast') or
                    len(d.args) != 2 or not int32(value) or not int32(d.args[1])):
                break
            value = d.args[1]
        return value

    zero_cache = {}

    def zeroes(value, visiting=frozenset()):
        if value in zero_cache:
            return zero_cache[value]
        if value in visiting or not int32(value):
            return 0
        k = constant(value)
        if k is not None:
            return trailing_zeroes(k)
        d = definitions[value]
        a = d.args
        visiting = visiting | {value}
        result = 0
        if d.op in ('OpCopyObject', 'OpBitcast') and len(a) == 2 and int32(a[1]):
            result = zeroes(a[1], visiting)
        elif d.op in ('OpIAdd', 'OpISub', 'OpIMul', 'OpBitwiseAnd',
                      'OpBitwiseOr', 'OpBitwiseXor') and len(a) == 3:
            x, y = zeroes(a[1], visiting), zeroes(a[2], visiting)
            if d.op == 'OpIMul':
                result = min(32, x + y)
            elif d.op == 'OpBitwiseAnd':
                result = max(x, y)
            else:
                result = min(x, y)
        elif d.op in ('OpShiftLeftLogical', 'OpShiftRightLogical') and len(a) == 3:
            shift = constant(a[2])
            if shift is not None and shift < 32:
                x = zeroes(a[1], visiting)
                result = (min(32, x + shift) if d.op == 'OpShiftLeftLogical'
                          else 32 if x == 32 else max(0, x - shift))
        zero_cache[value] = result
        return result

    def residue(value, bits, visiting=frozenset()):
        """Known low-bit value, not a guessed runtime range."""
        if value in visiting or not int32(value):
            return None
        if zeroes(value) >= bits:
            return 0
        k = constant(value)
        if k is not None:
            return k & ((1 << bits) - 1)
        d = definitions[value]
        visiting = visiting | {value}
        if d.op in ('OpCopyObject', 'OpBitcast') and len(d.args) == 2:
            return residue(d.args[1], bits, visiting)
        if d.op in ('OpIAdd', 'OpISub') and len(d.args) == 3:
            x, y = (residue(v, bits, visiting) for v in d.args[1:])
            if x is not None and y is not None:
                return ((x + y) if d.op == 'OpIAdd' else (x - y)) & ((1 << bits) - 1)
        return None

    typed_constants = {(d.args[0], constant(r)): r for r, d in definitions.items()
                       if d.op == 'OpConstant' and constant(r) is not None}
    added = []
    proofs = []
    next_id = first_free_id

    def typed_constant(type_id, value):
        nonlocal next_id
        key = type_id, value & MASK
        if key not in typed_constants:
            assert next_id < 0x3fffff, 'SPIR-V ID bound exhausted'
            rid = '%' + str(next_id)
            next_id += 1
            typed_constants[key] = rid
            literal = value & MASK
            if definitions[type_id].args[1] == '1' and literal >= 1 << 31:
                literal -= 1 << 32
            added.append(f'{rid} = OpConstant {type_id} {literal}')
            definitions[rid] = Instruction(-1, rid, 'OpConstant', [type_id, str(literal)])
        return typed_constants[key]

    for outer in list(definitions.values()):
        if outer.op != 'OpShiftRightLogical' or len(outer.args) != 3 or not int32(outer.result):
            continue
        output_type, value, shift_id = outer.args
        t = constant(shift_id)
        if t is None or not 0 < t < 32:
            continue
        value = unalias(value)
        addition = definitions.get(value)
        c = 0
        if addition and addition.op == 'OpIAdd' and len(addition.args) == 3:
            left, right = addition.args[1:]
            if constant(left) is not None:
                c, value = constant(left), unalias(right)
            elif constant(right) is not None:
                c, value = constant(right), unalias(left)
            else:
                continue
        inner = definitions.get(value)
        if not inner or inner.op != 'OpShiftRightLogical' or len(inner.args) != 3:
            continue
        _, base, inner_shift_id = inner.args
        s = constant(inner_shift_id)
        if (s is None or not 0 < s < 32 or s + t >= 32 or
                c >= 1 << t or not int32(base) or
                definitions[base].args[0] != output_type):
            continue
        known = residue(base, s + t)
        # With c=0, composing two logical shifts is valid for any bit pattern.
        # Otherwise, the exact low residue proves that c cannot carry into the
        # retained bits. In particular, do not wrap the original word-index
        # addition into a byte-index addition at the end of the 32-bit space.
        if c and (known is None or (known >> s) + c >= 1 << t):
            continue
        total_shift_id = typed_constant(output_type, s + t)
        original = lines[outer.line]
        lines[outer.line] = f'{outer.result} = OpShiftRightLogical {output_type} {base} {total_shift_id}'
        proofs.append({'result_id': outer.result, 'base_id': base,
                       'known_low_zero_bits': zeroes(base), 'inner_shift': s,
                       'rule': 'compose-logical-shifts-without-carry',
                       'known_low_residue': known,
                       'outer_shift': t, 'added_component': c,
                       'bit_width': 32, 'result_type_unchanged': output_type,
                       'before': original.strip(), 'after': lines[outer.line]})
        outer.args = [output_type, base, total_shift_id]

    # An aligned base cannot contribute a carry to the low bits of a constant
    # byte offset. Canonicalize (a+b)>>s to (a+(b & ~mask))>>s, retaining modular
    # overflow in the high addition. This gives different scalar components the
    # same vec4 index, rather than merging or widening any memory load here.
    for shift in list(definitions.values()):
        if shift.op != 'OpShiftRightLogical' or len(shift.args) != 3 or not int32(shift.result):
            continue
        type_id, value, shift_id = shift.args
        bits = constant(shift_id)
        addition = definitions.get(unalias(value))
        if bits is None or not 0 < bits < 32 or not addition or addition.op != 'OpIAdd':
            continue
        left, right = addition.args[1:]
        if constant(left) is not None:
            base, k = right, constant(left)
        elif constant(right) is not None:
            base, k = left, constant(right)
        else:
            continue
        if not int32(base) or definitions[base].args[0] != type_id or zeroes(base) < bits:
            continue
        rounded = k & (MASK ^ ((1 << bits) - 1))
        if rounded == k:
            continue
        original = lines[shift.line]
        source = base
        if rounded:
            # Create immediately before this use. Line order alone does not
            # prove dominance across blocks; leave legal CSE to SPIRV-Tools.
            k_id = typed_constant(type_id, rounded)
            assert next_id < 0x3fffff, 'SPIR-V ID bound exhausted'
            source = '%' + str(next_id)
            next_id += 1
            lines[shift.line] = f'{source} = OpIAdd {type_id} {base} {k_id}\n'
        else:
            lines[shift.line] = ''
        lines[shift.line] += f'{shift.result} = OpShiftRightLogical {type_id} {source} {shift_id}'
        proofs.append({'rule': 'discard-low-constant-offset-bits',
                       'result_id': shift.result, 'base_id': base,
                       'known_low_zero_bits': zeroes(base), 'shift': bits,
                       'constant_offset': k, 'rounded_offset': rounded,
                       'bit_width': 32, 'before': original.strip(),
                       'after': lines[shift.line]})
    if added:
        location = next((i for i, x in enumerate(lines) if '= OpFunction ' in x), len(lines))
        lines[location:location] = added
    return '\n'.join(lines) + '\n', proofs


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='new owned directory')
    parser.add_argument('--tools', type=Path, default=Path(__file__).resolve().parents[3] /
                        '.local/android-sdk/ndk/28.2.13676358/shader-tools/linux-x86_64')
    args = parser.parse_args()
    source = args.input.resolve()
    data = source.read_bytes()
    if (len(data) < 20 or len(data) % 4 or struct.unpack_from('<I', data)[0] != 0x07230203 or
            struct.unpack_from('<I', data, 4)[0] != 0x00010300):
        parser.error('this candidate accepts complete SPIR-V 1.3 modules only')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / 'input.spv').write_bytes(data)
    commands = []

    def run(*command, output=None):
        commands.append([str(x) for x in command])
        result = subprocess.run([str(x) for x in command], check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if output:
            output.write_bytes(result.stdout)
        if result.stderr:
            (out / ('tool-stderr-' + str(len(commands)) + '.txt')).write_bytes(result.stderr)

    tools = {n: args.tools / ('spirv-' + n) for n in ('dis', 'as', 'opt', 'val')}
    run(tools['dis'], '--raw-id', out / 'input.spv', output=out / 'original.spvasm')
    assembly, proofs = normalize_assembly((out / 'original.spvasm').read_text(),
                                         struct.unpack_from('<I', data, 12)[0])
    (out / 'normalized.spvasm').write_text(assembly)
    run(tools['as'], '--preserve-numeric-ids', '--target-env', 'spv1.3',
        out / 'normalized.spvasm', '-o', out / 'normalized.spv')
    run(tools['val'], '--target-env', 'vulkan1.1', out / 'normalized.spv')
    run(tools['opt'], '--simplify-instructions', '--redundancy-elimination',
        '--combine-access-chains', '--eliminate-dead-code-aggressive',
        out / 'normalized.spv', '-o', out / 'candidate.spv')
    run(tools['val'], '--target-env', 'vulkan1.1', out / 'candidate.spv')
    run(tools['dis'], '--raw-id', out / 'candidate.spv', output=out / 'candidate.spvasm')
    result = {'contract': 'SPIR-V exact low-zero address normalization v1',
              'complete': True, 'input': {'path': str(source),
                                          'sha256': hashlib.sha256(data).hexdigest()},
              'tool_source_sha256': digest(__file__), 'proofs': proofs,
              'changed_shift_results': len({p['result_id'] for p in proofs}),
              'rewrite_steps': len(proofs), 'new_float_precision': False,
              'memory_operations_or_robustness_changed_by_custom_pass': False,
              'native_compilation_and_GPU_performance': 'NOT_TESTED',
              'standard_tools': {n: {'path': str(p.resolve()), 'sha256': digest(p)}
                                 for n, p in tools.items()}, 'commands': commands,
              'outputs': {p.name: {'sha256': digest(p), 'bytes': p.stat().st_size}
                          for p in out.iterdir() if p.is_file()}}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'output': str(out),
                      'changed_shift_results': result['changed_shift_results'],
                      'rewrite_steps': len(proofs)}))


if __name__ == '__main__':
    main()
