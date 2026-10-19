#!/usr/bin/env python3
"""Owned shader negative controls, compiled/validated by official SPIR-V tools."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import dmsh_pixel_local as policy


class PixelLocal(unittest.TestCase):
    def setUp(self):self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self):self.temp.cleanup()
    def compile(self,source,stage='frag'):
        src=self.root/('source.'+stage);src.write_text(source);out=self.root/'source.spv'
        subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(src),'-o',str(out)],check=True,capture_output=True)
        return out.read_bytes()
    def fragment(self,expression='texture(src,uv.xy)',extra='',qualifier=''):
        return self.compile('''#version 450
layout(set=1,binding=1) uniform sampler2D src;
layout(location=1) '''+qualifier+''' in vec4 uv;
layout(location=0) out vec4 color;
'''+extra+'\nvoid main(){color='+expression+';}')
    def binding(self,code):return policy.analyze_fragment(code)['bindings'][0]
    def assertFallback(self,code):
        self.assertFalse(self.binding(code)['eligible'])
        with self.assertRaises(ValueError):policy.input_variant(code,{(1,1):0})
    def test_direct_implicit(self):
        d=self.binding(self.fragment());self.assertTrue(d['eligible']);self.assertEqual(d['samples'][0]['varying_location'],1)
        self.assertEqual(d['samples'][0]['lod'],'implicit-zero-required')
    def test_explicit_zero(self):
        d=self.binding(self.fragment('textureLod(src,uv.xy,0.0)'));self.assertTrue(d['eligible']);self.assertEqual(d['samples'][0]['lod'],'explicit-zero')
    def test_nonzero_lod(self):self.assertFallback(self.fragment('textureLod(src,uv.xy,1.0)'))
    def test_dynamic_lod(self):self.assertFallback(self.fragment('textureLod(src,uv.xy,uv.z)'))
    def test_bias(self):self.assertFallback(self.fragment('texture(src,uv.xy,0.0)'))
    def test_grad(self):self.assertFallback(self.fragment('textureGrad(src,uv.xy,vec2(0),vec2(0))'))
    def test_offset(self):self.assertFallback(self.fragment('textureOffset(src,uv.xy,ivec2(1,0))'))
    def test_neighbor(self):self.assertFallback(self.fragment('texture(src,uv.xy+vec2(0.001,0))'))
    def test_different_components(self):self.assertFallback(self.fragment('texture(src,uv.zw)'))
    def test_projective(self):self.assertFallback(self.fragment('textureProj(src,uv.xyz)'))
    def test_gather(self):self.assertFallback(self.fragment('textureGather(src,uv.xy)'))
    def test_noperspective_reject(self):self.assertFallback(self.fragment(qualifier='noperspective'))
    def test_centroid_reject(self):self.assertFallback(self.fragment(qualifier='centroid'))
    def test_nan_sanitation(self):
        code=self.fragment('texture(src,mix(uv.xy,vec2(0),isnan(uv.xy)))');self.assertTrue(self.binding(code)['eligible'])
    def test_select_not_nan(self):self.assertFallback(self.fragment('texture(src,uv.z>0.0?uv.xy:vec2(0))'))
    def test_mixed_branch_reject_entire_binding(self):
        self.assertFallback(self.fragment('uv.z>0.0?texture(src,uv.xy):texture(src,uv.xy+vec2(0.01))'))
    def test_all_branch_samples(self):
        code=self.fragment('uv.z>0.0?texture(src,uv.xy):textureLod(src,uv.xy,0.0)')
        d=self.binding(code);self.assertTrue(d['eligible']);self.assertEqual(len(d['samples']),2)
    def test_query_mixed(self):self.assertFallback(self.fragment('texture(src,uv.xy)+vec4(textureSize(src,0),0,0)'))
    def test_dref_and_array(self):
        for typ,expr in [('sampler2DShadow','vec4(texture(src,uv.xyz))'),('sampler2DArray','texture(src,uv.xyz)'),('sampler2DMS','texelFetch(src,ivec2(uv.xy),0)')]:
            code=self.compile('#version 450\nlayout(set=1,binding=1)uniform '+typ+' src;layout(location=1)in vec4 uv;layout(location=0)out vec4 color;void main(){color='+expr+';}')
            self.assertFallback(code)
    def vertex(self,body='',uv='fma(tex, coeff.v[3].xy, coeff.v[2].zw)',w='1.0'):
        return self.compile('''#version 450
layout(location=0)in vec3 pos;layout(location=3)in vec2 tex;
layout(set=0,binding=1,std140)uniform Coeff{vec4 v[4];}coeff;
layout(location=1)out vec2 uv;
void main(){gl_Position=vec4(pos,'''+w+');uv='+uv+';'+body+'}','vert')
    def test_vertex_fma_recipe(self):
        r=policy.analyze_vertex(self.vertex());self.assertEqual(r['position'],{'attribute':0,'w':1})
        self.assertEqual(r['uv_recipes'],[{'location':1,'attribute':3,'mode':'fma','scale':[(0,1,48),(0,1,52)],'bias':[(0,1,40),(0,1,44)]}])
    def test_vertex_direct(self):self.assertEqual(policy.analyze_vertex(self.vertex(uv='tex'))['uv_recipes'][0]['mode'],'direct')
    def test_vertex_branch(self):self.assertIn('rejected',policy.analyze(self.vertex('if(tex.x>0)gl_Position.x+=1;')))
    def test_vertex_w(self):self.assertIn('rejected',policy.analyze(self.vertex(w='2.0')))
    def test_vertex_dynamic_uniform(self):
        r=policy.analyze(self.vertex(uv='fma(tex,coeff.v[int(pos.z)&3].xy,coeff.v[2].zw)'))
        self.assertIn('rejected',r)
    def test_variant_preserves_other_binding(self):
        code=self.fragment('texture(src,uv.xy)+texture(other,uv.xyz)','layout(set=1,binding=2)uniform sampler3D other;')
        variant,proof=policy.input_variant(code,{(1,1):0});self.assertEqual(proof['replaced_samples'],1)
        src=self.root/'variant.spv';src.write_bytes(variant)
        text=subprocess.check_output(['spirv-dis',str(src)],text=True)
        self.assertEqual(text.count('OpImageRead '),1);self.assertEqual(text.count('OpImageSampleImplicitLod '),1)
        self.assertIn('InputAttachmentIndex 0',text);self.assertEqual(proof['source_sha256'],hashlib.sha256(code).hexdigest())
    def test_variant_reject_indices(self):
        for mapping in ({(1,2):0},{(1,1):8},{(1,1):-1},{}):
            with self.assertRaises(ValueError):policy.input_variant(self.fragment(),mapping)
    def test_manifest_hash_closure(self):
        code=self.fragment();(self.root/'owned.spv').write_bytes(code)
        r=policy.emit(self.root,'owned');report=json.loads((self.root/'owned.plocal.json').read_text())
        self.assertEqual(r['sha256'],hashlib.sha256((self.root/'owned.plocal.json').read_bytes()).hexdigest())
        self.assertEqual(report['artifacts'],{'spv':hashlib.sha256(code).hexdigest()})
        (self.root/'owned.pred').write_bytes(b'unknown')
        policy.emit(self.root,'owned');self.assertIn('closure_rejected',json.loads((self.root/'owned.plocal.json').read_text()))
    def test_optional_module_has_own_proof(self):
        base=self.fragment();neighbor=self.fragment('texture(src,uv.xy+vec2(0.001,0))')
        (self.root/'owned.spv').write_bytes(base)
        # Header integrity does not authorize the alternate: inspect its actual
        # SPIR-V independently. A spec branch may add a nonlocal access.
        recipe=struct.pack('<7I',0x31505344,1,1,4,0,0,len(neighbor)//4)+hashlib.sha256(base).digest()+neighbor
        (self.root/'owned.spec').write_bytes(recipe);policy.emit(self.root,'owned')
        r=json.loads((self.root/'owned.plocal.json').read_text())
        self.assertTrue(r['closure_verified']);self.assertTrue(r['modules']['base']['bindings'][0]['eligible'])
        self.assertFalse(r['modules']['spec']['bindings'][0]['eligible'])
        self.assertNotEqual(r['modules']['base']['source_sha256'],r['modules']['spec']['source_sha256'])
    def test_unverified_alternative_closure(self):
        (self.root/'owned.spv').write_bytes(self.fragment());(self.root/'owned.tssbo').write_bytes(b'unknown')
        policy.emit(self.root,'owned');r=json.loads((self.root/'owned.plocal.json').read_text())
        self.assertFalse(r['closure_verified'])
    def test_relaxed_coordinate_precision(self):
        code=self.fragment();src=self.root/'base.spv';src.write_bytes(code)
        text=subprocess.check_output(['spirv-dis','--raw-id',str(src)],text=True)
        m=policy.Module(code);root=next(r for r,op,a,_ in m.rows if op=='OpVariable' and a[1]=='Input')
        # Original and normalized IDs remain the same in this owned shader.
        text=text.replace('OpDecorate '+root+' Location 1','OpDecorate '+root+' Location 1\nOpDecorate '+root+' RelaxedPrecision')
        asm=self.root/'relaxed.asm';asm.write_text(text);out=self.root/'relaxed.spv'
        subprocess.run(['spirv-as','--target-env','vulkan1.1',str(asm),'-o',str(out)],check=True,capture_output=True)
        self.assertFallback(out.read_bytes())
    def test_compiler_switch_and_cache_integrity(self):
        from unittest.mock import patch
        import os
        from dmsh_vulkan import compiler_options,pixel_local_cache_valid
        with patch.dict(os.environ,{'DMSH_VK_PIXEL_LOCAL_REFLECTION':'0'}):
            self.assertNotIn('pixel_local_reflection',compiler_options())
        with patch.dict(os.environ,{'DMSH_VK_PIXEL_LOCAL_REFLECTION':'1'}):
            self.assertTrue(compiler_options()['pixel_local_reflection'])
        with patch.dict(os.environ,{'DMSH_VK_PIXEL_LOCAL_REFLECTION':'yes'}):
            with self.assertRaises(ValueError):compiler_options()
        self.assertTrue(pixel_local_cache_valid(self.root,'owned',{},False))
        self.assertFalse(pixel_local_cache_valid(self.root,'owned',{},True))
        (self.root/'owned.spv').write_bytes(self.fragment());record=policy.emit(self.root,'owned')
        cached={'pixel_local':record};self.assertTrue(pixel_local_cache_valid(self.root,'owned',cached,True))
        self.assertFalse(pixel_local_cache_valid(self.root,'owned',cached,False))
        (self.root/'owned.plocal.json').write_bytes(b'corrupt')
        self.assertFalse(pixel_local_cache_valid(self.root,'owned',cached,True))
    def test_variable_pointer_and_group_fallback(self):
        code=self.fragment();src=self.root/'base.spv';src.write_bytes(code)
        text=subprocess.check_output(['spirv-dis','--raw-id',str(src)],text=True)
        text=text.replace('OpCapability Shader','OpCapability Shader\nOpCapability VariablePointers\nOpExtension "SPV_KHR_variable_pointers"',1)
        asm=self.root/'cap.asm';asm.write_text(text);out=self.root/'cap.spv'
        subprocess.run(['spirv-as','--target-env','vulkan1.1',str(asm),'-o',str(out)],check=True,capture_output=True)
        self.assertIn('rejected',policy.analyze(out.read_bytes()))


if __name__=='__main__':unittest.main()
