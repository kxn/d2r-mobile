#!/usr/bin/env python3
"""Protect non-color data/control dependencies in experimental FP16 fragments."""
import os,re,struct,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import dmsh_fragment_precision as policy
import dmsh_vulkan

class FragmentPrecision(unittest.TestCase):
    def test_color_gate_and_non_color_effects(self):
        common='''#version 450
layout(set=0,binding=0) uniform sampler2D tex;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=1,std430) buffer Result { float result; };
void main(){float x=texture(tex,uv).x;float gate=x*0.25+0.123;'''
        cases={
            'color':'color=vec4(gate,gate,gate,1);if(gate>0.2)color.rgb*=0.5;',
            'discard':'if(gate>0.2)discard;color=vec4(1);',
            'alpha':'color=vec4(1,1,1,gate);',
            'alpha_phi':'float a=1;if(gate>0.2)a=0;color=vec4(1,1,1,a);',
            'coordinate_phi':'vec2 pos=uv;if(gate>0.2)pos=uv*0.5;color=texture(tex,pos);',
            'conditional_sample':'color=vec4(1);if(gate>0.2)color=texture(tex,uv);',
            'storage':'if(gate>0.2)result=1;color=vec4(1);',
            'loop_coordinate':'vec2 pos=uv;for(int i=0;i<3;i++){if(gate>float(i)*0.1)pos*=0.5;}color=texture(tex,pos);',
            'loop_color':'vec3 c=vec3(gate);for(int i=0;i<3;i++){if(gate>float(i)*0.1)c*=0.5;}color=vec4(c,1);'}
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            for name,body in cases.items():
                with self.subTest(case=name):
                    src=root/(name+'.frag');src.write_text(common+body+'}\n')
                    spv=src.with_suffix('.src.spv');dst=src.with_suffix('.candidate.spv')
                    subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(src),'-o',str(spv)],check=True,capture_output=True)
                    result=policy.transform(spv,dst)
                    self.assertNotIn('skipped',result)
                    text=subprocess.check_output(['spirv-dis','--raw-id',str(dst.with_suffix('.normalized.spv'))],text=True)
                    literal=re.search(r'(%\d+) = OpConstant %\d+ 0.25\s*$',text,re.M)
                    self.assertIsNotNone(literal)
                    gates=re.findall(r'(%\d+) = OpFMul %\d+ %\d+ '+re.escape(literal[1])+r'\s*$',text,re.M)
                    self.assertEqual(len(gates),1)
                    self.assertEqual(gates[0] in result['eligible_ids'],name in ['color','loop_color'])
                    if name=='color':
                        code,proof=dmsh_vulkan.protected_color_half(spv)
                        self.assertTrue(proof['explicit_float16'])
                        self.assertLessEqual(set(proof['added_capabilities']),{9})
                        dst.write_bytes(code)
                        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(dst)],check=True,capture_output=True)

    def test_opt_in_and_no_sample_precision_combination(self):
        with patch.dict(os.environ,{'DMSH_VK_PROTECTED_COLOR_HALF':'0'}):
            self.assertFalse(dmsh_vulkan.compiler_options()['protected_color_half'])
        with patch.dict(os.environ,{'DMSH_VK_PROTECTED_COLOR_HALF':'1','DMSH_VK_RELAX_SAMPLED_RESULTS':'0'}):
            self.assertTrue(dmsh_vulkan.compiler_options()['protected_color_half'])
        with patch.dict(os.environ,{'DMSH_VK_PROTECTED_COLOR_HALF':'1','DMSH_VK_RELAX_SAMPLED_RESULTS':'1'}):
            with self.assertRaises(ValueError):dmsh_vulkan.compiler_options()

if __name__=='__main__':unittest.main()
