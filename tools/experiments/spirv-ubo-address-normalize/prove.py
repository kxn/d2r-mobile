#!/usr/bin/env python3
"""Use standard Z3 bitvectors to prove each changed integer SSA result exactly.

All unsupported expressions are independent symbolic bit patterns shared by
their original IDs. This overapproximates the unchanged source, rather than
assuming bounded bone indices or sampling a few input values.
"""
import argparse
import hashlib
import json
from pathlib import Path
import z3
from normalize import parse


def expressions(text):
    definitions = parse(text)
    int32_types = {r for r, d in definitions.items()
                   if d.op == 'OpTypeInt' and d.args[0] == '32'}
    cache = {}
    visiting = set()

    def expression(value):
        if value in cache:
            return cache[value]
        if value in visiting:
            raise ValueError('cyclic arithmetic expression is not a proof input')
        visiting.add(value)
        d = definitions.get(value)
        op, a = (d.op, d.args) if d else ('unknown', [])
        if op == 'OpConstant' and len(a) == 2 and a[0] in int32_types:
            try:
                result = z3.BitVecVal(int(a[1], 0), 32)
            except ValueError:
                result = z3.BitVec(value, 32)
        elif op in ('OpBitcast', 'OpCopyObject') and len(a) == 2:
            result = expression(a[1])
        elif op in ('OpIAdd', 'OpISub', 'OpIMul', 'OpBitwiseAnd',
                    'OpBitwiseOr', 'OpBitwiseXor', 'OpShiftLeftLogical',
                    'OpShiftRightLogical') and len(a) == 3:
            x, y = expression(a[1]), expression(a[2])
            result = {'OpIAdd': lambda: x + y, 'OpISub': lambda: x - y,
                      'OpIMul': lambda: x * y, 'OpBitwiseAnd': lambda: x & y,
                      'OpBitwiseOr': lambda: x | y, 'OpBitwiseXor': lambda: x ^ y,
                      'OpShiftLeftLogical': lambda: x << y,
                      'OpShiftRightLogical': lambda: z3.LShR(x, y)}[op]()
        else:
            result = z3.BitVec(value, 32)
        visiting.remove(value)
        cache[value] = result
        return result
    return expression


def prove(original, normalized, result_ids):
    before, after = expressions(original), expressions(normalized)
    results = []
    for rid in sorted(set(result_ids)):
        solver = z3.Solver()
        solver.set(timeout=10000)
        solver.add(before(rid) != after(rid))
        status = solver.check()
        row = {'result_id': rid, 'inequality': str(status),
               'all_32bit_input_patterns_equal': status == z3.unsat}
        if status == z3.sat:
            row['counterexample'] = str(solver.model())
        if status == z3.unknown:
            row['reason'] = solver.reason_unknown()
        results.append(row)
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('candidate', type=Path)
    a = p.parse_args()
    recipe = json.loads((a.candidate / 'result.json').read_text())
    results = prove((a.candidate / 'original.spvasm').read_text(),
                    (a.candidate / 'normalized.spvasm').read_text(),
                    [r['result_id'] for r in recipe['proofs']])
    assert results and all(r['all_32bit_input_patterns_equal'] for r in results), results
    record = {'solver': 'Z3', 'version': z3.get_version_string(),
              'domain': 'All 32-bit bit patterns with exact modular arithmetic, '
                        'not a random test or a runtime value assumption.',
              'all_changed_results_proved': True, 'results': results,
              'proof_source_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'candidate_record_sha256': hashlib.sha256((a.candidate / 'result.json').read_bytes()).hexdigest()}
    (a.candidate / 'equivalence-proof.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'proved_results': len(results), 'all_equal': True}))


if __name__ == '__main__':
    main()
