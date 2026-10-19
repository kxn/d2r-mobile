"""Structural and immutable-input tests for the optional texel representation."""
import hashlib
import copy
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from dmsh_texel_ssbo import transform, pair, unpack_pair, PAIR_MAGIC, REQUIRED_FORMAT, QUIET_NAN_MODEL


@unittest.skipUnless(all(shutil.which(x) for x in ('glslangValidator', 'spirv-as', 'spirv-opt', 'spirv-dis', 'spirv-val')), 'official SPIR-V tools required')
class TexelStorageTests(unittest.TestCase):
    def compile(self, body='color=texelFetch(t,int(gl_FragCoord.x));', declaration='uniform samplerBuffer t;', extra=''):
        source = '#version 450\nlayout(set=1,binding=2) '+declaration+'\nlayout(location=0)out vec4 color;\n'+extra+'\nvoid main(){'+body+'}\n'
        with tempfile.TemporaryDirectory() as tmp:
            f=Path(tmp)/'source.frag';f.write_text(source);out=Path(tmp)/'source.spv'
            subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(f),'-o',str(out)],check=True,capture_output=True)
            return out.read_bytes()

    def assembly(self, code):
        with tempfile.TemporaryDirectory() as tmp:
            f=Path(tmp)/'module.spv';f.write_bytes(code)
            return subprocess.check_output(['spirv-dis',str(f)],text=True)

    def test_runtime_format_contract_and_real_descriptor_type(self):
        base=self.compile();before=hashlib.sha256(base).hexdigest();alt,proof=transform(base,{2},1)
        self.assertEqual(hashlib.sha256(base).hexdigest(),before)
        self.assertTrue(proof['requires_runtime_format_selection'])
        self.assertEqual(proof['runtime_contracts'][0]['required_runtime_format'],REQUIRED_FORMAT)
        self.assertEqual(proof['runtime_contracts'][0]['stride_bytes'],16)
        a=self.assembly(alt)
        self.assertIn('StorageBuffer',a);self.assertIn('NonWritable',a);self.assertIn('OpArrayLength',a)
        self.assertIn('OpULessThan',a);self.assertNotIn('OpImageFetch',a);self.assertNotIn('Restrict',a)
        self.assertNotIn('UniformConstant',a)

    def test_reflection_set_and_binding_are_authoritative(self):
        base=self.compile()
        for bindings,descriptor_set in [({3},1),({2},0),(set(),1)]:
            alt,proof=transform(base,bindings,descriptor_set)
            self.assertIsNone(alt);self.assertIn('skipped',proof)

    def test_queries_descriptor_arrays_and_integer_formats_fall_back(self):
        cases=[self.compile('color=vec4(float(textureSize(t)));'),
               self.compile('color=texelFetch(t[0],0);','uniform samplerBuffer t[2];'),
               self.compile('color=vec4(texelFetch(t,0));','uniform usamplerBuffer t;'),
               self.compile('color=imageLoad(t,0);','layout(rgba32f) readonly uniform imageBuffer t;')]
        for base in cases:
            alt,proof=transform(base,{2},1);self.assertIsNone(alt);self.assertIn('skipped',proof)

    def test_shader_memory_barriers_and_qualifiers_fall_back(self):
        barrier=self.compile('memoryBarrierImage();color=texelFetch(t,0);')
        coherent=self.compile('color=texelFetch(t,0)+imageLoad(scratch,0);',
                              extra='layout(set=1,binding=3,rgba32f)coherent uniform imageBuffer scratch;')
        for base in (barrier,coherent):
            alt,proof=transform(base,{2},1)
            self.assertIsNone(alt);self.assertIn('skipped',proof)

    def test_specialization_and_pair_input_identity(self):
        base=self.compile()
        specialized=self.compile('color=texelFetch(t,int(gl_FragCoord.x))+vec4(float(selector));',extra='layout(constant_id=0)const int selector=0;')
        recipe=struct.pack('<7I',0x32505344,257,27,4,0,0,len(specialized)//4)+hashlib.sha256(base).digest()+specialized
        blob,proof=pair(base,recipe,{2},1)
        self.assertIsNotNone(blob)
        magic,version,ds,count,base_words,spec_words=struct.unpack_from('<6I',blob)
        self.assertEqual((magic,version,ds,count),(PAIR_MAGIC,1,1,1))
        self.assertEqual(blob[24:56],hashlib.sha256(base).digest())
        self.assertEqual(blob[56:88],hashlib.sha256(recipe).digest())
        alt_spec=blob[88+count*4+base_words*4:]
        self.assertEqual(len(alt_spec),spec_words*4)
        self.assertIn('SpecId 0',self.assembly(alt_spec))
        self.assertEqual(unpack_pair(blob,base,recipe,proof)[1],alt_spec)
        with self.assertRaises(ValueError):unpack_pair(blob[:-1]+bytes([blob[-1]^1]),base,recipe,proof)
        with self.assertRaises(ValueError):unpack_pair(blob,base,recipe+b'xxxx',proof)
        with self.assertRaises(ValueError):pair(base+b'xxxx',recipe,{2},1)

    def test_quiet_model_integer_load_and_existing_vector_types(self):
        # Real packed-bit consumers and bool4 use force preexisting uint4 and
        # bool4 types. The adapter must reuse them, not emit invalid duplicates.
        base=self.compile('uvec4 u=floatBitsToUint(texelFetch(t,int(gl_FragCoord.x)));'
                          'color=mix(vec4(u&uvec4(255u)),vec4(0),greaterThan(u,uvec4(65535u)));')
        alt,proof=transform(base,{2},1,QUIET_NAN_MODEL)
        self.assertEqual(proof['load_model'],QUIET_NAN_MODEL)
        self.assertEqual(proof['runtime_contracts'][0]['load_model'],QUIET_NAN_MODEL)
        a=self.assembly(alt)
        for op in ('OpBitwiseAnd','OpIEqual','OpINotEqual','OpLogicalAnd','OpSelect','OpBitwiseOr','OpBitcast'):
            self.assertIn(op,a)
        self.assertIn('OpTypeRuntimeArray %v4uint',a)
        self.assertNotIn('OpImageFetch',a);self.assertNotIn('OpIsNan',a)
        self.assertNotIn('RelaxedPrecision',a);self.assertNotIn('Restrict',a)
        self.assertIn('OpArrayLength',a);self.assertIn('OpULessThan',a)

    def test_quiet_pair_keeps_immutable_inputs_and_model_contract(self):
        base=self.compile();before=hashlib.sha256(base).hexdigest()
        specialized=self.compile('color=texelFetch(t,int(gl_FragCoord.x))+vec4(float(selector));',
                                 extra='layout(constant_id=0)const int selector=0;')
        recipe=struct.pack('<7I',0x32505344,257,27,4,0,0,len(specialized)//4)+hashlib.sha256(base).digest()+specialized
        blob,proof=pair(base,recipe,{2},1,QUIET_NAN_MODEL)
        alt,spec_alt,bindings=unpack_pair(blob,base,recipe,proof)
        self.assertEqual(bindings,[2]);self.assertIn('SpecId 0',self.assembly(spec_alt))
        self.assertEqual(hashlib.sha256(base).hexdigest(),before)
        self.assertEqual(blob[24:56],hashlib.sha256(base).digest())
        self.assertEqual(blob[56:88],hashlib.sha256(recipe).digest())
        bad=copy.deepcopy(proof);bad['load_model']='identity'
        with self.assertRaises(ValueError):unpack_pair(blob,base,recipe,bad)
        bad=copy.deepcopy(proof);bad['runtime_contracts'][0]['load_model']='unknown'
        with self.assertRaises(ValueError):unpack_pair(blob,base,recipe,bad)
        for code in (alt,spec_alt):self.assertNotIn('OpImageFetch',self.assembly(code))

    def test_default_identity_bytes_and_negative_index_guard(self):
        base=self.compile('color=texelFetch(t,-1);')
        self.assertEqual(pair(base,None,{2},1),pair(base,None,{2},1,'identity'))
        default,proof=pair(base,None,{2},1)
        self.assertNotIn('load_model',proof)
        self.assertNotIn('load_model',proof['runtime_contracts'][0])
        alt,quiet=transform(base,{2},1,QUIET_NAN_MODEL)
        a=self.assembly(alt)
        self.assertIn('OpArrayLength',a);self.assertIn('OpULessThan',a)
        self.assertIn('OpBranchConditional',a);self.assertIn('OpConstantNull',a)
        with self.assertRaises(ValueError):transform(base,{2},1,'guess-driver')

    def test_nondefault_model_is_explicit_and_old_default_options_survive(self):
        import dmsh_vulkan
        with mock.patch.dict(os.environ,{},clear=True):
            default=dmsh_vulkan.compiler_options()
            self.assertNotIn('texel_storage_load_model',default)
            os.environ['DMSH_VK_TEXEL_LOAD_MODEL']=QUIET_NAN_MODEL
            with self.assertRaises(ValueError):dmsh_vulkan.compiler_options()
            os.environ['DMSH_VK_TEXEL_STORAGE_ALTERNATIVES']='1'
            self.assertEqual(dmsh_vulkan.compiler_options()['texel_storage_load_model'],QUIET_NAN_MODEL)
            os.environ['DMSH_VK_TEXEL_LOAD_MODEL']='identity'
            options=dmsh_vulkan.compiler_options();self.assertNotIn('texel_storage_load_model',options)
            self.assertEqual({k:v for k,v in options.items()if k!='texel_storage_alternatives'},
                             {k:v for k,v in default.items()if k!='texel_storage_alternatives'})


if __name__=='__main__':unittest.main()
