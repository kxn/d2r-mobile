"""Original DPL1 serializer, actual kind2 legacy/kind6 candidate identities."""
from pathlib import Path
import hashlib,struct,subprocess,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import dmsh_derived_bounded_uniform_prefetch as D

class PixelLocal6(unittest.TestCase):
 def test_original_transform_with_explicit_legacy_and_candidate_kinds(self):
  with tempfile.TemporaryDirectory()as tmp:
   root=Path(tmp);source=root/'fixture.frag';binary=root/'fixture.spv'
   source.write_text('#version 450\nlayout(set=1,binding=1)uniform sampler2D src;layout(location=1)in vec2 uv;layout(location=0)out vec4 color;void main(){color=texture(src,uv);}')
   subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(source),'-o',str(binary)],check=True,capture_output=True)
   code=binary.read_bytes();blobs={'spv':code,'meta':b'input-metadata-for-wire-hash','spec':b'validated-DSP3-envelope-by-caller'}
   report,wire,proof=D.pixel_closures(blobs,{'descriptor_set':1},bytes(60)+code,code)
   self.assertEqual(struct.unpack_from('<8I',wire)[:6],(D.P.PW.MAGIC,D.P.PW.VERSION,len(wire),1,1,3))
   offset=D.P.PW.HEADER_BYTES;kinds=[]
   for _ in range(3):
    h=struct.unpack_from('<8I',wire,offset);kinds.append(h[0]);self.assertEqual(wire[offset+32:offset+64],hashlib.sha256(code).digest())
    payload=wire[offset+h[1]-h[6]*4:offset+h[1]];p=root/'plocal.spv';p.write_bytes(payload)
    subprocess.run(['spirv-val','--target-env','vulkan1.1',str(p)],check=True,capture_output=True)
    self.assertEqual(hashlib.sha256(payload).digest(),wire[offset+64:offset+96]);offset+=h[1]
   self.assertEqual(kinds,[1,2,6]);self.assertEqual(offset,len(wire));self.assertEqual(proof['records'],3)
   self.assertEqual(wire[96:128],hashlib.sha256(blobs['spec']).digest())
   self.assertEqual(report['modules']['spec']['source_sha256'],hashlib.sha256(code).hexdigest())
 def test_full32_index_payload_proof(self):
  self.assertEqual(D.W.selection_equivalence()['counterexample'],'UNSAT')

if __name__=='__main__':unittest.main(verbosity=2)
