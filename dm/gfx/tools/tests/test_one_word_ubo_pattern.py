"""Real FS pattern qualification: readonly Block vs legacy writable BufferBlock."""
from pathlib import Path
import sys,subprocess,unittest,tempfile
TOOLS=Path(__file__).resolve().parents[1];sys.path.insert(0,str(TOOLS));import dmsh_one_word_ubo as W
R=TOOLS.parents[2];Q=R/'.local/logs/fs-one-word-load-experiment-v2'
class Pattern(unittest.TestCase):
 def text(self):return subprocess.check_output(['spirv-dis','--raw-id',str(Q/'5c1ca4e7c3e4657c.original.spv')],text=True)
 def test_real_readonly_block_accepted(self):
  _,p=W.transform(self.text());self.assertEqual(p['nested_branchconditional_headers'],3)
 def test_same_pattern_legacy_writable_BufferBlock_rejected(self):
  text=self.text();self.assertIn('OpDecorate %44 Block',text)
  with self.assertRaisesRegex(ValueError,'uniquely proved'):W.transform(text.replace('OpDecorate %44 Block','OpDecorate %44 BufferBlock'))
 def test_unhandled_group_decorations_refused(self):
  with self.assertRaisesRegex(ValueError,'grouped decorations'):W.transform(self.text().replace('OpMemoryModel','%99999 = OpDecorationGroup\nOpMemoryModel',1))
 def validate_actual_mutation(self,text):
  with tempfile.TemporaryDirectory()as directory:
   asm=Path(directory)/'actual-mutated.spvasm';binary=Path(directory)/'actual-mutated.spv';asm.write_text(text)
   subprocess.run(['spirv-as','--preserve-numeric-ids','--target-env','spv1.3',str(asm),'-o',str(binary)],check=True,capture_output=True)
   subprocess.run(['spirv-val','--target-env','vulkan1.1',str(binary)],check=True,capture_output=True)
 def actual_external_write(self,instruction):
  # Retain the complete real FS. Its existing BufferBlock binding4 could
  # reference the same allocation as the selected Uniform Block binding1.
  # Place the write AFTER all four old loads but BEFORE their later root.
  text=self.text().replace('OpDecorate %22 NonWritable','')
  return text.replace('%483 = OpBitcast','%99980 = OpAccessChain %131 %22 %109 %145\n'+instruction+'\n%483 = OpBitcast',1)
 def test_actual_full_FS_with_intervening_alias_SSBO_write_rejected(self):
  text=self.actual_external_write('OpStore %99980 %147');self.validate_actual_mutation(text)
  with self.assertRaisesRegex(ValueError,'external or unknown memory store'):W.transform(text)
 def test_actual_full_FS_with_memory_barrier_rejected(self):
  text=self.text().replace('%2 = OpFunction','%99981 = OpConstant %68 72\n%2 = OpFunction',1).replace('%483 = OpBitcast','OpMemoryBarrier %147 %99981\n%483 = OpBitcast',1);self.validate_actual_mutation(text)
  with self.assertRaisesRegex(ValueError,'synchronization'):W.transform(text)
 def test_actual_full_FS_with_alias_atomic_rejected(self):
  text=self.actual_external_write('%99982 = OpAtomicIAdd %68 %99980 %147 %145 %147');self.validate_actual_mutation(text)
  with self.assertRaisesRegex(ValueError,'atomic'):W.transform(text)
 def test_actual_full_FS_with_alias_copy_rejected(self):
  text=self.actual_external_write('OpCopyMemory %99980 %99980');self.validate_actual_mutation(text)
  with self.assertRaisesRegex(ValueError,'external or unknown memory copy'):W.transform(text)
if __name__=='__main__':unittest.main()
