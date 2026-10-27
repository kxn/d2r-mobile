import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('mesa_native_identity_join',Path(__file__).with_name('link_draws.py'));M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
H='a'*64
class Link(unittest.TestCase):
    def test_native_header_exact_stage_hash_statistics(self):
        blocks=M.parse_ir3([f'MESA: info: Native code for unnamed FRAG shader (null) with blake3 {H}:','MESA: info: (ss)ldc.4 r0.x, 0','MESA: info: ; FRAG prog 2/1: 10 instr, 1 nops','MESA: info: NIR (final form) for VERT shader (null):','MESA: info: add.f fake, fake'])
        self.assertEqual(len(blocks),1);self.assertEqual(blocks[0]['compiler_instruction_counts'],[10]);self.assertEqual(blocks[0]['opcode_text_histogram'],{'ldc.4':1})
    def test_link_exact_native_hash_not_program_number_or_duration(self):
        rp={'id':1,'kind':'render_pass','owner_cb_id':0,'start_ns':10,'end_ns':100,'render_pass_ordinal_in_CB':10,'start_params':{'width':'1673','height':'765','attachment_count':'2'},'end_params':{'drawCount':'1'}}
        draw={'id':2,'kind':'draw','owner_cb_id':0,'start_ns':20,'end_ns':80,'start_params':{'fs_sha1':H,'vs_sha1':'b'*64,'count':'123'}}
        r=M.link([rp,draw],[{'stage':'FRAG','blake3':H,'compiler_instruction_counts':[99]}]);self.assertFalse(r['draw_timestamps_used_as_cost']);self.assertTrue(r['draw_identity_completeness'][0]['count_matches']);frag=next(s for s in r['groups'][0]['shaders'] if s['stage']=='FRAG');self.assertTrue(frag['compiler_match_available']);self.assertEqual(frag['draw_calls'],1);self.assertEqual(frag['reported_count_argument_sum'],123)
    def test_unknown_draw_owner_not_silently_assigned(self):
        r=M.link([{'id':2,'kind':'draw','owner_cb_id':9,'start_ns':20,'end_ns':80,'start_params':{}}],[]);self.assertEqual(len(r['unowned_or_ambiguous_draws']),1);self.assertFalse(r['groups'])
    def test_native_flags_spacing_and_repeat_are_text_not_dynamic_counts(self):
        blocks=M.parse_ir3([f'MESA: info: Native code for unnamed FRAG shader (null) with blake3 {H}:',
            'MESA: info: (nop1) min.u r0.x, r0.y, c0.x',
            'MESA: info: (sy)(ss) (rpt3) isam.v.base1 (u32)(x)r0.x, r1.x, 0',
            'MESA: info: (rpt6)nop', 'MESA: info: l123:',
            'MESA: info: ; FRAG prog 2/1: 100 instr, 20 nops'])
        self.assertEqual(blocks[0]['opcode_text_histogram'],{'min.u':1,'isam.v.base1':1,'nop':1})
        self.assertEqual(blocks[0]['compiler_instruction_counts'],[100])
if __name__=='__main__':unittest.main(verbosity=2)
