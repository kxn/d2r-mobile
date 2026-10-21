#!/usr/bin/env python3
"""Existing full composition identity/preservation; no game/GPU sampling claim."""
import copy,importlib.util,json,unittest
from pathlib import Path
D=Path(__file__).resolve().parent;R=Path('/home/dl/d2r-mobile')
s=importlib.util.spec_from_file_location('asset',D/'qualified_asset_gate.py');g=importlib.util.module_from_spec(s);s.loader.exec_module(g)
def require(v,m):
 if not v:raise RuntimeError(m)
class FullAsset(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  old=R/'.local/build/turnip-native-bulk-6217-f830-v1';new=R/'.local/build/native-qualified-astc6-full2838-compose-v1'
  cls.before=json.loads((old/'native-pack/native-pack.json').read_text())['entries']
  cls.after=json.loads((new/'native-pack/native-pack.json').read_text())['entries']
  cls.old_table=(old/'native-overlay/files/native-layouts.bin').read_bytes();cls.new_table=(new/'native-overlay/files/native-layouts.bin').read_bytes()
  cls.proof=json.loads((R/'.local/logs/native-qualified-astc6-full2838-compose-v1/family-cache-preservation.json').read_text())
 def test_old32_profile_exact(self):
  p=g.asset_profile(g.ASSET_REVIEW,require)
  self.assertEqual((p['count'],p['unchanged'],p['outputs'],p['composition'],p['source']),
   (32,6185,12447,g.COMPOSITION,g.SOURCE_READY))
 def test_unknown_or_pending_full_authority_refused(self):
  with self.assertRaises(RuntimeError):g.asset_profile('0'*64,require)
 def test_actual_full_family_same6217_ids_3379_unchanged(self):
  g.validate_membership(self.before,self.after,self.old_table,self.new_table,self.proof['replacements'],require,2838)
 def test_full_requires_all_exact_replacements(self):
  with self.assertRaises(RuntimeError):g.validate_membership(self.before,self.after,self.old_table,self.new_table,self.proof['replacements'][:-1],require,2838)
 def test_unselected_remaining_entry_mutation_refused(self):
  after=copy.deepcopy(self.after);after[6000]['image_size']+=4
  with self.assertRaises(RuntimeError):g.validate_membership(self.before,after,self.old_table,self.new_table,self.proof['replacements'],require,2838)
 def test_real_four_equal_allocations_retained_not_counted_as_saved(self):
  equal=[r for r in self.proof['replacements'] if r['category']=='equal'];self.assertEqual(len(equal),4)
  for r in equal:
   self.assertEqual(r['new_native_bytes'],r['old_native_bytes'])
   self.assertIn(tuple(r['formats']),[(157,165),(158,166)])
 def test_larger_selected_entry_refused(self):
  after=copy.deepcopy(self.after);rows=copy.deepcopy(self.proof['replacements']);r=rows[0]
  after[r['id']-1]['image_size']=r['new_native_bytes']=r['old_native_bytes']+4096
  with self.assertRaises(RuntimeError):g.validate_membership(self.before,after,self.old_table,self.new_table,rows,require,2838)
if __name__=='__main__':unittest.main()
