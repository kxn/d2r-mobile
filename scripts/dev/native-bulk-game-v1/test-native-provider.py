#!/usr/bin/env python3
"""Offline actual table/header refusal checks; never Android execution."""
from pathlib import Path
import copy,hashlib,importlib.util,json,tempfile,unittest
D=Path(__file__).resolve().parent;R=Path('/home/dl/d2r-mobile');s=importlib.util.spec_from_file_location('provider_under_test',D/'run-game.py');t=importlib.util.module_from_spec(s);s.loader.exec_module(t)
class Provider(unittest.TestCase):
 def actual(self):
  p=R/'.local/build/native-astc-linear-NVN-positive-v2';r=json.loads((p/'pack/native-pack.json').read_text());table=(p/'resource4/native-layouts.bin').read_bytes()
  c={'driver_identity':r['identity'],'native_pack_content_sha256':r['pack_content_sha256'],'driver_sha256':r['driver_binary_sha256'],'producer_source_sha256':r['producer_source_sha256'],'entry_count':len(r['entries'])};return table,c
 def test_real_AA_6217_table_still_passes(self):
  p=R/'.local/build/turnip-native-bulk-6217-aa185-v1/native-overlay';c=json.loads((p/'composition.json').read_text());self.assertEqual(t.native_table_gate((p/'files/native-layouts.bin').read_bytes(),c,t.AA_DRIVER),6217)
 def test_real_new_f830_one_table_passes_not_bulk_claim(self):
  data,c=self.actual();self.assertEqual(t.native_table_gate(data,c,t.LINEAR_DRIVER),1)
 def test_root_fixture_closed(self):
  t.provider_gate(t.AA_DRIVER,None);t.provider_gate(t.LINEAR_DRIVER,{'path':str(R/'.local/logs/native-astc-linear-NVN-root-actual-review.json'),'sha256':'9089fbe5d64b3842e8ae387506e6a22c9cc2034881a76e2711a675407f05af0f'},{'path':str(R/'.local/logs/native-astc-linear-filter-reference-q-v1-root-actual-review.json'),'sha256':'02eff2027890cf9aff0ad479def9ae780e0fa966ec9f3a6a474824855c4f6261'})
 def test_new_driver_requires_actual_root_receipt(self):
  for a in (None,{'sha256':'0'*64}):
   with self.assertRaises(RuntimeError):t.provider_gate(t.LINEAR_DRIVER,a)
 def test_unknown_driver_refused(self):
  with self.assertRaises(RuntimeError):t.provider_gate('0'*64,None)
 def test_old_layout_cannot_be_relabeled(self):
  data,c=self.actual();c['driver_identity']='4529bbb870cdbdd02db214742b3e4170'+c['driver_identity'][32:]
  with self.assertRaises(RuntimeError):t.native_table_gate(data,c,t.LINEAR_DRIVER)
 def test_other_driver_refused(self):
  data,c=self.actual()
  with self.assertRaises(RuntimeError):t.native_table_gate(data,c,t.AA_DRIVER)
 def test_pack_driver_producer_checksum_extent_and_uuid_refused(self):
  data,c=self.actual()
  for offset in (32,64,96,128,12,24,320,336,352,496):
   bad=bytearray(data);bad[offset]^=1
   # Recompute both checksums to ensure mismatched identities still reject.
   if offset>=320:
    bad[160+48+960:160+1040]=hashlib.sha256(bad[208:208+960]+bytes(32)).digest()
    bad[128:160]=bytes(32);bad[128:160]=hashlib.sha256(bad).digest()
   with self.subTest(offset=offset),self.assertRaises(RuntimeError):t.native_table_gate(bad,c,t.LINEAR_DRIVER)
if __name__=='__main__':unittest.main()
