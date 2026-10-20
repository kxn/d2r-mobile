#!/usr/bin/env python3
"""Read actual prior receipts and source-only split; never fake actual bulk data."""
import copy,importlib.util,json,tempfile,unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=Path('/home/dl/d2r-mobile')
def module(path,name):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
c=module(HERE/'compose-bulk-turnip-native-overlay.py','composer');w=module(HERE/'compose-native-linear-bulk.py','workflow')
class Linear(unittest.TestCase):
 def accepted(self):
  return c.receipt_gate(ROOT/'.local/logs/native-astc-linear-NVN-root-actual-review.json','9089fbe5d64b3842e8ae387506e6a22c9cc2034881a76e2711a675407f05af0f',c.Fingerprints())
 def test_actual_NVNanfilter_receipts_not_game(self):
  d=self.accepted();self.assertFalse(d['actual_game_accepted']);self.assertFalse(d['bulk_assets_accepted']);self.assertIn('not-game-or-bulk',d['acceptance_scope'])
 def test_current_ready_retains_exact_historical_canonical(self):
  r=w.current_ready();old=json.loads(Path(r['historical_compiled_producer_ready']['path']).read_text());self.assertEqual(r['canonical'],old['canonical']);self.assertEqual(r['producer_source_identity'],old['producer_source_identity']);self.assertNotEqual(r['canonical']['driver_sha256'],r['active_driver']['sha256']);self.assertTrue(c.producer_gate.native_linear_acceptance.historical_producer_driver(r,self.accepted(),c.Fingerprints()))
 def test_source_ready_actual_source_APK_producer_gate(self):
  r=w.current_ready()
  with tempfile.TemporaryDirectory()as td:
   p=Path(td)/'ready.json';p.write_text(json.dumps(r));out=c.producer_gate.source_ready_gate(p,w.sha(p),w.P,self.accepted(),c.Fingerprints());self.assertEqual(out['canonical']['APK']['sha256'],'106dcdb88359be6a6c5285b84cd94851cd87d660708ad2216dbfb96a4940204a')
 def test_relabeling_historical_driver_refused(self):
  r=w.current_ready();r['canonical']['driver_sha256']=r['active_driver']['sha256']
  with self.assertRaises(ValueError):c.producer_gate.native_linear_acceptance.historical_producer_driver(r,self.accepted(),c.Fingerprints())
 def test_changed_historical_compiled_metadata_refused(self):
  for key in ['source_commit','ELF_sha256']:
   r=w.current_ready();r['canonical'][key]='0'*64
   with self.subTest(key=key),self.assertRaises(ValueError):c.producer_gate.native_linear_acceptance.historical_producer_driver(r,self.accepted(),c.Fingerprints())
 def test_changed_active_driver_or_controller_refused(self):
  for key in ['active_driver','ordinary_UID_controller']:
   r=w.current_ready();r[key]['sha256']='0'*64
   with self.subTest(key=key),self.assertRaises(ValueError):c.producer_gate.native_linear_acceptance.historical_producer_driver(r,self.accepted(),c.Fingerprints())
 def test_wrong_root_receipt_hash_refused(self):
  with self.assertRaises(ValueError):c.receipt_gate(ROOT/'.local/logs/native-astc-linear-NVN-root-actual-review.json','0'*64,c.Fingerprints())
if __name__=='__main__':unittest.main()
