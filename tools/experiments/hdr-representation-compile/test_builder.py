"""Owned host checks of exact original PSO reuse, not GPU correctness tests."""
import importlib.util
from pathlib import Path
import unittest
P=Path(__file__).with_name('build-probe.py')
spec=importlib.util.spec_from_file_location('hdr_compile_builder',P)
M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
class Builder(unittest.TestCase):
 def test_emit_rejects_unknown_pointer_and_extension(self):
  for v in ({'pUnknown':[1]},{'pNext':{'sType':'UNKNOWN'}}):
   with self.assertRaises(ValueError):M.Emitter().declare('VkRenderPassCreateInfo','r',v)
 def test_literal_rejects_injected_c(self):
  for value in ('VK_TEST;abort()','some_function()', 'VK_TEST\n;'):
   with self.assertRaises(ValueError):M.Emitter().literal(value)
 def test_raw_exact_original_matches_current(self):
  if not (M.A/'matched-pso-0.json').is_file():self.skipTest('Qualified official GFXR extraction not present')
  for index,fs,vs in [(0,'5c1ca4e7c3e4657c','54f730a6c0efc118'),(2,'38f344adf801b153','af64b417689666bf')]:
   match=M.original_match(index,fs,vs)
   self.assertTrue(match['pipeline']['index']>0)
 def test_storage_changes_only_one_layout_binding_and_no_game_calls(self):
  if not (M.A/'matched-pso-0.json').is_file():self.skipTest('Actual PSO not present')
  match=M.original_match(0,'5c1ca4e7c3e4657c','54f730a6c0efc118')
  case={'index':0,'vs_file':'vs.spv','file':'fs.spv','representation':'original','predicate_mask':None}
  original=M.generate_pipeline(case,match);case['representation']='identity';identity=M.generate_pipeline(case,match)
  expected=original.replace('VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER','VK_DESCRIPTOR_TYPE_STORAGE_BUFFER')
  self.assertEqual(expected,identity)
  self.assertIn('VK_FORMAT_B10G11R11_UFLOAT_PACK32',identity)
  self.assertIn('VK_FORMAT_D24_UNORM_S8_UINT',identity)
  self.assertIn('VK_COMPARE_OP_EQUAL',identity)
  self.assertNotIn('vkQueueSubmit(',identity);self.assertNotIn('vkCmdDraw(',identity)
 def test_predicate_adds_second_spec_keeps_original_one(self):
  if not (M.A/'matched-pso-0.json').is_file():self.skipTest('Actual PSO not present')
  match=M.original_match(0,'5c1ca4e7c3e4657c','54f730a6c0efc118')
  c={'index':0,'vs_file':'vs.spv','file':'fs.spv','representation':'predicate','predicate_mask':3,'predicate_id':1}
  text=M.generate_pipeline(c,match)
  self.assertIn('.constantID=0,.offset=0,.size=4',text)
  self.assertIn('.constantID=1,.offset=4,.size=4',text)
  self.assertIn('.dataSize=8',text)
if __name__=='__main__':unittest.main(verbosity=2)
