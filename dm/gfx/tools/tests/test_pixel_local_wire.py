#!/usr/bin/env python3
"""Deployable module closure and official-validator controls for DPL1."""
from pathlib import Path
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import dmsh_pixel_local_wire as wire


class PixelLocalWire(unittest.TestCase):
    def setUp(self):self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self):self.temp.cleanup()
    def compile(self,text,stage='frag'):
        source=self.root/('source.'+stage);source.write_text(text);out=self.root/'source.spv'
        subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(source),'-o',str(out)],check=True,capture_output=True)
        return out.read_bytes()
    def fragment(self,bindings=4,neighbor=False):
        declarations=''.join(f'layout(set=1,binding={i+1})uniform sampler2D src{i};\n' for i in range(bindings))
        return self.compile('#version 450\n'+declarations+'layout(location=1)in vec2 uv;layout(location=0)out vec4 color;void main(){color='+
            '+'.join(f'texture(src{i},uv'+('+vec2(0.001)' if neighbor else '')+')' for i in range(bindings))+';}')
    def records(self,blob):
        self.assertEqual(struct.unpack_from('<2I',blob),(wire.MAGIC,wire.VERSION))
        self.assertEqual(struct.unpack_from('<I',blob,8)[0],len(blob))
        records=[];offset=192
        for i in range(struct.unpack_from('<I',blob,20)[0]):
            h=struct.unpack_from('<8I',blob,offset);end=offset+h[1]
            code=blob[end-h[6]*4:end] if h[6] else b''
            records.append((h,blob[offset+32:offset+64],blob[offset+64:offset+96],code))
            offset=end
        self.assertEqual(offset,len(blob));return records
    def validate(self,code):
        p=self.root/'variant.spv';p.write_bytes(code)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(p)],check=True,capture_output=True)
        return subprocess.check_output(['spirv-dis',str(p)],text=True)
    def test_full_four_binding_group_and_each_single_omission(self):
        source=self.fragment();blob,proof=wire.build({'spv':source},{'descriptor_set':1})
        records=self.records(blob);self.assertEqual(len(records),5);h,src,dst,code=records[0]
        self.assertEqual((h[2],h[3],h[4]),(4,0,1))
        self.assertEqual(src,hashlib.sha256(source).digest());self.assertEqual(dst,hashlib.sha256(code).digest())
        text=self.validate(code);self.assertEqual(text.count('OpImageRead '),4)
        self.assertNotIn('OpImageSampleImplicitLod ',text)
        self.assertEqual(proof['records'],5)
        self.assertEqual([x[0][7] for x in records],[0x40f,0x40e,0x40d,0x40b,0x407])
        for h,src,dst,code in records[1:]:
            text=self.validate(code);self.assertEqual(text.count('OpImageRead '),3)
            self.assertEqual(text.count('OpImageSampleImplicitLod '),1)
            self.assertEqual(dst,hashlib.sha256(code).digest())
    def test_single_binding_positive(self):
        records=self.records(wire.build({'spv':self.fragment(1)},{'descriptor_set':1})[0])
        self.assertEqual(records[0][0][2],1);self.validate(records[0][3])
    def test_no_partial_truncation_of_oversized_group(self):
        self.assertEqual(self.records(wire.build({'spv':self.fragment(9)})[0]),[])
    def test_neighbor_rejected(self):
        self.assertEqual(self.records(wire.build({'spv':self.fragment(1,True)})[0]),[])
    def test_spec_has_independent_proof_not_base_inheritance(self):
        base=self.fragment(1);other=self.fragment(1,True)
        spec=struct.pack('<7I',0x31505344,1,1,4,0,0,len(other)//4)+hashlib.sha256(base).digest()+other
        records=self.records(wire.build({'spv':base,'spec':spec})[0])
        self.assertEqual([r[0][0] for r in records],[1])
        exact=struct.pack('<7I',0x31505344,1,1,4,0,0,len(base)//4)+hashlib.sha256(base).digest()+base
        records=self.records(wire.build({'spv':base,'spec':exact})[0])
        self.assertEqual([r[0][0] for r in records],[1,2])
        for r in records:self.validate(r[3])
    def test_unverified_pair_rejected(self):
        with self.assertRaises(ValueError):wire.build({'spv':self.fragment(1),'tssbo':b'invalid'})
    def test_metadata_hash_is_in_closure(self):
        source=self.fragment(1);a=wire.build({'spv':source,'meta':b'first'})[0];b=wire.build({'spv':source,'meta':b'second'})[0]
        self.assertNotEqual(a,b);self.assertEqual(a[64:96],hashlib.sha256(b'first').digest())
    def test_vertex_exact_fma_uniform_references(self):
        source=self.compile('''#version 450
layout(location=0)in vec3 pos;layout(location=3)in vec2 tex;
layout(set=0,binding=1,std140)uniform Coeff{vec4 v[4];}coeff;
layout(location=1)out vec2 uv;
void main(){gl_Position=vec4(pos,1.0);uv=fma(tex,coeff.v[3].xy,coeff.v[2].zw);}''','vert')
        blob,_=wire.build({'spv':source});h,src,dst,code=self.records(blob)[0]
        self.assertEqual((h[2],h[3],h[5],h[6]),(0,1,0,0));self.assertFalse(code);self.assertEqual(dst,bytes(32))
        self.assertEqual(struct.unpack_from('<16I',blob,192+96),(1,3,1,0,0,1,48,0,1,52,0,1,40,0,1,44))
    def test_runtime_option_cache_and_content_binding(self):
        from dmsh_vulkan import compiler_options,pixel_local_runtime_cache_valid
        with patch.dict(os.environ,{'DMSH_VK_PIXEL_LOCAL_RUNTIME':'0','DMSH_VK_PIXEL_LOCAL_REFLECTION':'0'}):self.assertNotIn('pixel_local_runtime',compiler_options())
        with patch.dict(os.environ,{'DMSH_VK_PIXEL_LOCAL_RUNTIME':'1'}):self.assertTrue(compiler_options()['pixel_local_runtime'])
        with patch.dict(os.environ,{'DMSH_VK_PIXEL_LOCAL_RUNTIME':'yes'}):
            with self.assertRaises(ValueError):compiler_options()
        (self.root/'key.spv').write_bytes(self.fragment(1));proof=wire.emit(self.root,'key',{})
        self.assertTrue(pixel_local_runtime_cache_valid(self.root,'key',{'pixel_local_runtime':proof},True))
        self.assertFalse(pixel_local_runtime_cache_valid(self.root,'key',{'pixel_local_runtime':proof},False))
        (self.root/'key.plocal').write_bytes(b'corrupt')
        self.assertFalse(pixel_local_runtime_cache_valid(self.root,'key',{'pixel_local_runtime':proof},True))


if __name__=='__main__':unittest.main()
