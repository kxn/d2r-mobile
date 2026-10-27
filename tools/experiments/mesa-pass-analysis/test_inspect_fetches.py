import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('fetches',Path(__file__).with_name('inspect_fetches.py'));M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)

BASE='''OpDecorate %buffer DescriptorSet 1
OpDecorate %buffer Binding 2
%buffer = OpVariable %pointer UniformConstant
%sampled = OpLoad %sampletype %buffer
%image = OpImage %imagetype %sampled
%fetch = OpImageFetch %float4 %image %index None
%component = OpCompositeExtract %float %fetch 0
'''
class Fetch(unittest.TestCase):
    def test_transparent_bitcast_to_integer_is_observed(self):
        r=M.inspect(BASE+'%bits = OpBitcast %uint %component\n%masked = OpBitwiseAnd %uint %bits %mask',1,2)
        self.assertEqual(r['fetch_count'],1);self.assertEqual(r['direct_integer_observation_count'],1)
        self.assertEqual(len(r['fetches'][0]['integer_paths'][0]['path']),4)
    def test_float_math_stops_direct_bit_observation_proof(self):
        r=M.inspect(BASE+'%math = OpFMul %float %component %other\n%bits = OpBitcast %uint %math\n%masked = OpBitwiseAnd %uint %bits %mask',1,2)
        self.assertEqual(r['direct_integer_observation_count'],0)
        self.assertEqual(r['fetches'][0]['first_numeric_uses'][0]['operation'],'OpFMul')
    def test_descriptor_identity_and_cycle_are_not_guessed(self):
        self.assertEqual(M.inspect(BASE,1,3)['fetch_count'],0)
        self.assertEqual(M.inspect(BASE.replace('%sampled = OpLoad %sampletype %buffer','%sampled = OpCopyObject %sampletype %cycle\n%cycle = OpCopyObject %sampletype %sampled'),1,2)['fetch_count'],0)

if __name__=='__main__':unittest.main(verbosity=2)
