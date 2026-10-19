#!/usr/bin/env python3
"""All-path driver-table proof tests; compile GLSL with official tools."""
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import dmsh_driver_keys as policy
from dmsh_uniform_predicates import _parse


class DriverKeys(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()
    def code(self,expression,helpers=''):
        path=self.root/'source.comp'
        path.write_text('''#version 450
layout(local_size_x=1) in;
layout(set=2,binding=1,std140) uniform Driver { uvec4 t[4096]; };
layout(set=2,binding=2,std430) buffer Output { uvec4 value; };
'''+helpers+'\nvoid main(){'+expression+';}')
        out=self.root/'source.spv'
        subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(path),'-o',str(out)],check=True,capture_output=True)
        return out.read_bytes()
    def recipe(self,code,slot=2):
        meta=struct.pack('<8I',0x314b5644,5,2,1,0,1,1,1)+struct.pack('<8i',1,1,slot,0,0,1,0,0)
        return policy.build({'meta':meta,'spv':code})
    def test_all_branch_arms(self):
        # Selector itself plus both possible values are included, even when
        # the runtime only ever used one material in the captured scene.
        _,r=self.recipe(self.code('if(t[22].z==1u)value=t[23];else value=t[24]'))
        self.assertEqual(r['2']['words'],[0,1,2,3,4])
    def test_dynamic_falls_back(self):
        _,r=self.recipe(self.code('value=t[t[22].z&127u]'))
        self.assertIsNone(r['2']['words'])
    def test_constant_zero_tail(self):
        blob,r=self.recipe(self.code('value=t[4095]'))
        self.assertEqual(r['2']['words'],[])
        self.assertEqual(hashlib.sha256(blob[:-32]).digest(),blob[-32:])
    def test_dense_full_fallback(self):
        _,r=self.recipe(self.code('uvec4 x=uvec4(0);for(uint i=0;i<4096u;++i)x^=t[i];value=x'))
        self.assertIsNone(r['2']['words'])
    def test_c0_compute_both_banks(self):
        _,r=self.recipe(self.code('value=t[49]^t[33]'),0)
        self.assertEqual(r['0']['words'],[0,1,2,48,49,50])
    def test_pointer_escape_fallback(self):
        code=self.code('value=t[23]');p=self.root/'in.spv';p.write_bytes(code)
        text=subprocess.check_output(['spirv-dis','--raw-id',str(p)],text=True)
        defs,rows,_,_=_parse(text)
        rid,op,a,index=next(row for row in rows if row[1]=='OpLoad' and defs.get(row[2][1],(None,None))[1] in ('OpAccessChain','OpInBoundsAccessChain'))
        pointer_type=defs[a[1]][2][0];lines=text.splitlines()
        lines[index]=f'%escape = OpCopyObject {pointer_type} {a[1]}\n{rid} = OpLoad {a[0]} %escape'
        asm=self.root/'escape.spvasm';asm.write_text('\n'.join(lines));out=self.root/'escape.spv'
        subprocess.run(['spirv-as','--target-env','vulkan1.1',str(asm),'-o',str(out)],check=True,capture_output=True)
        _,r=self.recipe(out.read_bytes());self.assertIsNone(r['2']['words'])
        self.assertIn('pointer escape',r['2']['reason'])
    def test_backedge_pointer_phi_full_fallback(self):
        # Legal Vulkan1.1: first iteration only continues; second iteration
        # reads the back-edge AccessChain via Phi and stores it to an SSBO.
        # The chain is physically AFTER the Phi/load. Never infer empty reads.
        asm=Path(__file__).with_name('driver_pointer_phi.spvasm')
        out=self.root/'phi.spv'
        subprocess.run(['spirv-as','--target-env','vulkan1.1',str(asm),'-o',str(out)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(out)],check=True,capture_output=True)
        _,r=self.recipe(out.read_bytes())
        self.assertIsNone(r['2']['words'])
        self.assertIn('pointer',r['2']['reason'])
    def test_unused_variable_pointer_capability_fallback(self):
        code=self.code('value=t[23]');p=self.root/'in.spv';p.write_bytes(code)
        text=subprocess.check_output(['spirv-dis','--raw-id',str(p)],text=True)
        # A valid capability remains a conservative fallback even without a
        # currently recognized pointer Phi. No runtime feature assumption.
        text=text.replace('OpCapability Shader','OpCapability Shader\nOpCapability VariablePointers\nOpExtension "SPV_KHR_variable_pointers"',1)
        asm=self.root/'cap.spvasm';asm.write_text(text);out=self.root/'cap.spv'
        subprocess.run(['spirv-as','--target-env','vulkan1.1',str(asm),'-o',str(out)],check=True,capture_output=True)
        _,r=self.recipe(out.read_bytes());self.assertIsNone(r['2']['words'])
        self.assertIn('capability',r['2']['reason'])
    def test_typed_function_storage_parameter_stays_supported(self):
        # The original HDR base uses Function-storage arithmetic parameters.
        # A Uniform pointer cannot inhabit those typed arguments; only the
        # value loaded at both known table cells is copied into local storage.
        code=self.code('uvec4 tmp=t[23];value=identity(tmp)',
                       'uvec4 identity(inout uvec4 v){v^=uvec4(1);return v;}')
        _,r=self.recipe(code);self.assertEqual(r['2']['words'],[1,2])
    def test_input_identity_changes(self):
        first=self.code('value=t[23]');a,_=self.recipe(first)
        second=self.code('value=t[24]');b,_=self.recipe(second)
        self.assertNotEqual(a[16:48],b[16:48])
    def test_low32_and_padding_map(self):
        self.assertEqual(policy.compact_word(2,1,0x168+8*37),37)
        self.assertIsNone(policy.compact_word(2,1,0x168+8*37+4))
        self.assertEqual(policy.compact_word(2,1,0x568+8*87),128+87)
        self.assertEqual(policy.compact_word(0,5,0x310+16*15+8),47)
        self.assertEqual(policy.compact_word(0,5,0x210+16*15+8),95)
        self.assertIsNone(policy.compact_word(0,5,0x210+16*15+12))
        with self.assertRaises(ValueError):policy.compact_word(2,1,65536)
    def test_unverified_alternate_full_fallback(self):
        code=self.code('value=t[23]')
        meta=struct.pack('<8I',0x314b5644,5,2,1,0,1,1,1)+struct.pack('<8i',1,1,2,0,0,1,0,0)
        _,r=policy.build({'meta':meta,'spv':code,'tssbo':b'unknown future artifact'})
        self.assertIsNone(r['2']['words'])


if __name__=='__main__':unittest.main()
