"""DPR1 semantic boundaries and immutable input/protocol tests with official tools."""
from pathlib import Path
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dmsh_uniform_specialization as old
from dmsh_uniform_predicates import transform, unpack, MODULE_OFFSET, MAGIC, _parse


@unittest.skipUnless(all(shutil.which(x) for x in ('glslangValidator','spirv-as','spirv-dis','spirv-opt','spirv-val')), 'official SPIR-V tools required')
class UniformPredicateTests(unittest.TestCase):
    def source(self, condition='(b&16u)!=0u', extra='if((b&2u)==2u)s+=200u;', prefix=''):
        code='''#version 450
layout(set=1,binding=1,std140)uniform A{vec4 first[4096];};
layout(set=1,binding=2,std140)uniform B{vec4 second[4096];};
layout(location=0)out uvec4 value;
void main(){uint a=floatBitsToUint(first[0].x),b=floatBitsToUint(second[20].x);
uint s=0;for(uint i=0;i<min(a,4u);i++)s+=i+1u;
if(a==1u)s+=20u;else s+=40u;'''+prefix+'\nif('+condition+')s+=100u;'+extra+'''
value=uvec4(s,a,floatBitsToUint(second[20].x*2.0),b);}
'''
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'f.frag';p.write_text(code);b=p.with_suffix('.spv')
            subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(p),'-o',str(b)],check=True,capture_output=True)
            base=b.read_bytes()
        recipe,proof=old.transform(base,{1:4},minimum_weight=0)
        self.assertIsNotNone(recipe,proof)
        return base,recipe

    def assembly(self, code):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'m.spv';p.write_bytes(code)
            subprocess.run(['spirv-val','--target-env','vulkan1.1',str(p)],check=True,capture_output=True)
            return subprocess.check_output(['spirv-dis','--raw-id',str(p)],text=True)

    def test_preserve_old_full_word_loop_and_extra_float_uses(self):
        base,recipe=self.source();before=(hashlib.sha256(base).digest(),hashlib.sha256(recipe).digest())
        blob,proof=transform(base,recipe,{1:4,2:5},minimum_weight=0)
        self.assertEqual(proof['predicates'],[[2,16,0,0],[1,2,2,1]])
        module,predicates=unpack(blob,base,recipe,proof);a=self.assembly(module);b=self.assembly(recipe[60:])
        self.assertEqual((proof['reg'],proof['offset']),(5,320))
        self.assertEqual((hashlib.sha256(base).digest(),hashlib.sha256(recipe).digest()),before)
        for op in ('OpFMul','OpLoopMerge','OpLoad','OpBitcast'):
            self.assertEqual(a.count('= '+op+' '),b.count('= '+op+' '))
        self.assertIn('SpecId 0',a);self.assertIn('SpecId 1',a)
        self.assertEqual(len(module)+MODULE_OFFSET,len(blob))
        self.assertEqual(transform(base,recipe,{1:4,2:5},minimum_weight=0),(blob,proof))

    def test_predicate_all_consumers_and_generic_binding_proof(self):
        base,recipe=self.source(prefix='bool yes=(b&16u)!=0u; if(yes && a!=8u)s+=7u;')
        blob,proof=transform(base,recipe,{1:4,2:5},minimum_weight=0)
        self.assertIsNotNone(blob,proof)
        self.assertEqual(len(proof['predicates']),2)
        for eligible in ({1:4},{1:4,2:2},{1:4,2:19}):
            self.assertIsNone(transform(base,recipe,eligible,minimum_weight=0)[0])
        self.assertIsNone(transform(base,recipe,{1:4,2:5},minimum_weight=10**8)[0])

    def test_float_or_integer_arithmetic_predicates_not_accepted(self):
        for condition in ('second[20].x>0.0','(b+1u)==17u','(b>>1u)==8u'):
            base,recipe=self.source(condition=condition,extra='')
            result,proof=transform(base,recipe,{1:4,2:5},minimum_weight=0)
            self.assertIsNone(result,proof)

    def test_commuted_constant_equality_and_fullword_integer_equal(self):
        for condition,expected in [('0u!=(16u&b)',[2,16,0,0]),('18u==b',[1,0xffffffff,18,0])]:
            base,recipe=self.source(condition=condition,extra='')
            blob,proof=transform(base,recipe,{1:4,2:5},minimum_weight=0)
            self.assertIsNotNone(blob,proof);self.assertEqual(proof['predicates'],[expected])

    def test_bound_four_predicates_without_discarding_original_uses(self):
        body=''.join(f'if((b&{1<<i}u)!=0u)s+={i+1}u;' for i in range(6))
        base,recipe=self.source(extra=body)
        blob,proof=transform(base,recipe,{1:4,2:5},minimum_weight=0)
        self.assertIsNotNone(blob);self.assertEqual(len(proof['predicates']),4)
        self.assertEqual({p[3] for p in proof['predicates']},{0,1,2,3})
        self.assembly(unpack(blob,base,recipe,proof)[0])

    def test_old_nonzero_specialization_id_and_legacy_recipe(self):
        previous=old.SPEC_ID
        try:
            old.SPEC_ID=7;base,recipe=self.source()
        finally:old.SPEC_ID=previous
        blob,proof=transform(base,recipe,{1:4,2:5},minimum_weight=0)
        self.assertEqual(proof['spec_id'],0)
        self.assertIn('SpecId 7',self.assembly(unpack(blob,base,recipe,proof)[0]))
        legacy=struct.pack('<2I',0x31505344,1)+recipe[8:]
        blob,proof=transform(base,legacy,{1:4,2:5},minimum_weight=0)
        self.assertIsNotNone(blob);unpack(blob,base,legacy,proof)

    def test_input_hash_protocol_bounds_and_proof_are_strict(self):
        base,recipe=self.source();blob,proof=transform(base,recipe,{1:4,2:5},minimum_weight=0)
        for wrongbase,wrongspec in [(base+b'xxxx',recipe),(base,recipe+b'xxxx')]:
            with self.assertRaises(ValueError):unpack(blob,wrongbase,wrongspec)
        changes=[(16,128),(32,4),(32,(32<<18)+1),(0,0),(4,2),(8,1),(12,0),(24,3),(28,0),(28,5),(100,3),(116+12,0),(132,1)]
        for offset,value in changes:
            data=bytearray(blob);struct.pack_into('<I',data,offset,value)
            with self.assertRaises(ValueError,msg=(offset,value)):unpack(data,base,recipe)
        bad=dict(proof);bad['module_sha256']='0'*64
        with self.assertRaises(ValueError):unpack(blob,base,recipe,bad)
        with self.assertRaises(ValueError):unpack(blob[:-4],base,recipe)

    def rewrite_module(self, module, amendment):
        text=self.assembly(module);text=amendment(text)
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'m.spvasm';path.write_text(text);output=path.with_suffix('.spv')
            subprocess.run(['spirv-as','--target-env','vulkan1.1',str(path),'-o',str(output)],check=True,capture_output=True)
            return output.read_bytes()

    def rewrite_recipe(self, recipe, amendment):
        module=self.rewrite_module(recipe[60:],amendment);header=list(struct.unpack_from('<7I',recipe));header[6]=len(module)//4
        return struct.pack('<7I',*header)+recipe[28:60]+module

    def test_qualifiers_on_full_uniform_type_path_fail_closed(self):
        base,recipe=self.source();defs,rows,decs,members=_parse(self.assembly(recipe[60:]))
        root=next(r for r,op,a,_ in rows if op=='OpVariable' and decs.get((r,'Binding'))==['2'])
        pointer=defs[root][2][0];structure=defs[pointer][2][1];array=defs[structure][2][0]
        vector=defs[array][2][0];scalar=defs[vector][2][0]
        access=next(r for r,op,a,_ in rows if op=='OpAccessChain' and a[1]==root)
        # Even unsupported/redundant qualifiers must be rejected rather than
        # silently relying on them being absent in the currently tested game.
        for annotation in [f'OpDecorate {x} RelaxedPrecision' for x in (root,pointer,structure,array,vector,scalar,access)]+[f'OpMemberDecorate {structure} 0 Volatile']:
            def amend(text):
                lines=text.splitlines();at=next(i for i,l in enumerate(lines) if '= OpType' in l);lines.insert(at,annotation);return '\n'.join(lines)+'\n'
            changed=self.rewrite_recipe(recipe,amend)
            result,proof=transform(base,changed,{1:4,2:5},minimum_weight=0)
            # Qualifying only one access can leave another independently proved
            # comparison; qualifying the complete type/root must reject all.
            if access not in annotation:self.assertIsNone(result,(annotation,proof))
            elif result is not None:self.assertLess(len(proof['predicates']),2)

    def test_real_group_applied_qualifiers_fail_closed(self):
        base,recipe=self.source();defs,rows,decs,_=_parse(self.assembly(recipe[60:]))
        root=next(r for r,op,a,_ in rows if op=='OpVariable' and decs.get((r,'Binding'))==['2'])
        structure=defs[defs[root][2][0]][2][1]
        group='%'+str(struct.unpack_from('<I',recipe,72)[0])
        for application in (f'OpGroupDecorate {group} {root}',f'OpGroupMemberDecorate {group} {structure} 0'):
            def amendment(text):
                lines=text.splitlines();at=next(i for i,l in enumerate(lines) if '= OpType' in l)
                lines[at:at]=[f'{group} = OpDecorationGroup',f'OpDecorate {group} RelaxedPrecision',application]
                return '\n'.join(lines)+'\n'
            changed=self.rewrite_recipe(recipe,amendment)
            # These are real valid SPIR-V group qualifiers, not malformed input.
            self.assembly(changed[60:])
            result,proof=transform(base,changed,{1:4,2:5},minimum_weight=0)
            self.assertIsNone(result);self.assertIn('group decorations',proof['skipped'])

    def test_duplicate_specialization_and_legacy_flags_rejected(self):
        base,recipe=self.source()
        def duplicate(text):
            row=next(l for l in text.splitlines() if ' SpecId ' in l);return text.replace(row,row+'\n'+row)
        changed=self.rewrite_recipe(recipe,duplicate)
        self.assertIsNone(transform(base,changed,{1:4,2:5},minimum_weight=0)[0])
        legacy_bad=struct.pack('<I',0x31505344)+recipe[4:]
        with self.assertRaises(ValueError):transform(base,legacy_bad,{1:4,2:5},minimum_weight=0)
        blob,proof=transform(base,recipe,{1:4,2:5},minimum_weight=0)
        module=self.rewrite_module(blob[MODULE_OFFSET:],duplicate);header=bytearray(blob[:MODULE_OFFSET]);struct.pack_into('<I',header,32,len(module)//4)
        with self.assertRaises(ValueError):unpack(bytes(header)+module,base,recipe)

    def test_default_options_legacy_compatible_and_explicit_dependency(self):
        import dmsh_vulkan
        with mock.patch.dict(os.environ,{},clear=True):
            baseline=dmsh_vulkan.compiler_options();self.assertNotIn('uniform_predicates',baseline)
            os.environ['DMSH_VK_UNIFORM_PREDICATES']='1'
            with self.assertRaises(ValueError):dmsh_vulkan.compiler_options()
            os.environ['DMSH_VK_UNIFORM_SPECIALIZATION']='1'
            self.assertTrue(dmsh_vulkan.compiler_options()['uniform_predicates'])
            os.environ['DMSH_VK_TEXEL_STORAGE_ALTERNATIVES']='1'
            os.environ['DMSH_VK_TEXEL_LOAD_MODEL']='rgba32f_quiet_nan_v1'
            self.assertTrue(dmsh_vulkan.compiler_options()['uniform_predicates'])
            os.environ['DMSH_VK_UNIFORM_PREDICATES']='2'
            with self.assertRaises(ValueError):dmsh_vulkan.compiler_options()

if __name__=='__main__':unittest.main()
