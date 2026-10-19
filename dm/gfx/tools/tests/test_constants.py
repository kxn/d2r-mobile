#!/usr/bin/env python3
"""Exercise real SPIR-V static/dynamic c1 accesses, bit patterns and fallback layouts."""
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from dmsh_constants import inline_c1

class ConstantFolding(unittest.TestCase):
    def test_static_bits_dynamic_and_other_bank(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);src=root/'test.frag';spv=root/'test.spv';out=root/'fold.spv'
            src.write_text("""#version 450
layout(set=1,binding=7,std140) uniform C1 { vec4 values[4096]; } c1;
layout(set=1,binding=9,std140) uniform C0 { vec4 value; } c0;
layout(location=0) out vec4 color;
void main() {
 int i=int(gl_FragCoord.x)&4095;
 color=c1.values[0]+vec4(c1.values[1].x,c1.values[2].y,c0.value.x,c1.values[i].w);
}
""")
            subprocess.run(['glslangValidator','-V',str(src),'-o',str(spv)],check=True,capture_output=True)
            raw=spv.read_bytes();payload=struct.pack('<4I',0x80000000,0x7fc01234,0x3f800000,0xff800000)
            folded,stats=inline_c1(raw,7,payload,1)
            self.assertEqual(stats,{'scalar_loads':2,'vector_loads':1})
            out.write_bytes(folded)
            subprocess.run(['spirv-val','--target-env','vulkan1.1',str(out)],check=True,capture_output=True)
            words=struct.unpack('<%dI'%(len(folded)//4),folded);at=5;bits=[];loads=0
            while at<len(words):
                size,op=words[at]>>16,words[at]&65535
                if op==43 and size==4:bits.append(words[at+3])
                if op==61:loads+=1
                at+=size
            for bits_expected in struct.unpack('<4I',payload):self.assertIn(bits_expected,bits)
            self.assertIn(0,bits) # fixed c1 buffer is zero-padded, not undefined
            self.assertGreaterEqual(loads,2) # dynamic c1 and unrelated c0 remain
            for binding,descriptor_set in [(None,1),(7,0),(999,1)]:
                same,unchanged=inline_c1(raw,binding,payload,descriptor_set)
                self.assertEqual(same,raw);self.assertFalse(any(unchanged.values()))
            # Do not fold an arbitrary Uniform block merely because its binding matches.
            src.write_text(src.read_text().replace('values[4096]','values[2048]').replace('&4095','&2047'))
            subprocess.run(['glslangValidator','-V',str(src),'-o',str(spv)],check=True,capture_output=True)
            short=spv.read_bytes();same,unchanged=inline_c1(short,7,payload,1)
            self.assertEqual(same,short);self.assertFalse(any(unchanged.values()))

if __name__=='__main__':unittest.main()
