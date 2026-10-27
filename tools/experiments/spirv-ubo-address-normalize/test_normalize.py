import unittest
import z3
from normalize import normalize_assembly, parse
from prove import prove


def assembly(stride=48, byte_offset=0, component=1, inner=2, outer=2,
             arithmetic=False, vector=False):
    return f'''
%1 = OpTypeInt 32 1
%2 = OpTypeInt 32 0
%3 = OpTypeFloat 32
%4 = OpTypeVector %1 4
%10 = OpConstant %1 {stride}
%11 = OpConstant %1 {byte_offset}
%12 = OpConstant %1 {component}
%13 = OpConstant %1 {inner}
%14 = OpConstant %1 {outer}
%20 = OpLoad %1 %100
%21 = OpIMul %1 %20 %10
%22 = OpIAdd %1 %21 %11
%23 = OpShiftRightLogical %1 %22 %13
%24 = OpBitcast %2 %23
%25 = OpBitcast %1 %24
%26 = OpIAdd %1 %25 %12
%27 = {'OpShiftRightArithmetic' if arithmetic else 'OpShiftRightLogical'} {'%4' if vector else '%1'} %26 %14
'''


class NormalizeTests(unittest.TestCase):
    def check_equal(self, text):
        result, proofs = normalize_assembly(text, 101)
        checks = prove(text, result, [r['result_id'] for r in proofs])
        self.assertTrue(proofs)
        self.assertTrue(all(r['all_32bit_input_patterns_equal'] for r in checks), checks)
        return result, proofs

    def test_actual_alignment_shapes_and_overflow_all_uint32(self):
        # Includes all four matrix components and the byte offsets that straddle
        # a row when naïvely rewriting the word-index addition modulo32.
        for stride in (16, 32, 48, 80, 0x80000000):
            for offset in (0, 8, 16, 24, 32, 40, -8, -24):
                for c in (0, 1):
                    with self.subTest(stride=stride, offset=offset, component=c):
                        self.check_equal(assembly(stride, offset, c))

    def test_no_runtime_alignment_assumption(self):
        text = assembly(stride=1, component=1)
        _, proofs = normalize_assembly(text, 101)
        self.assertFalse(proofs)

    def test_component_carry_rejected(self):
        # base+12 has low word bits3; +1 crosses the retained boundary. In
        # particular 0xfffffff0+12 must yield vec index0x10000000, not wrap0.
        text = assembly(byte_offset=12, component=1)
        output, proofs = normalize_assembly(text, 101)
        self.assertFalse(any(r['result_id'] == '%27' for r in proofs))
        self.assertEqual(parse(output)['%27'].args, parse(text)['%27'].args)

    def test_direct_shift_composition_needs_no_alignment(self):
        text = assembly(stride=1, component=0)
        self.check_equal(text)

    def test_arithmetic_shift_and_vectors_rejected(self):
        for text in (assembly(arithmetic=True), assembly(vector=True)):
            _, proofs = normalize_assembly(text, 101)
            self.assertFalse(any(r['result_id'] == '%27' for r in proofs))

    def test_shift_width_and_negative_component_rejected(self):
        for text in (assembly(inner=16, outer=16), assembly(inner=32),
                     assembly(component=-1), assembly(component=4)):
            _, proofs = normalize_assembly(text, 101)
            self.assertFalse(any(r['result_id'] == '%27' and
                                 r['rule'] == 'compose-logical-shifts-without-carry'
                                 for r in proofs))
            # A different, independently valid offset-rounding rule may still
            # apply to -1; it must retain modular overflow exactly.
            output, _ = normalize_assembly(text, 101)
            checks = prove(text, output, ['%27'])
            self.assertTrue(checks[0]['all_32bit_input_patterns_equal'])

    def test_proof_finds_counterexample_without_gate(self):
        text = assembly(stride=1)
        incorrect = text.replace('%27 = OpShiftRightLogical %1 %26 %14',
                                 '%27 = OpShiftRightLogical %1 %22 %14')
        result = prove(text, incorrect, ['%27'])
        self.assertFalse(result[0]['all_32bit_input_patterns_equal'])
        self.assertEqual(result[0]['inequality'], 'sat')


if __name__ == '__main__':
    unittest.main()
