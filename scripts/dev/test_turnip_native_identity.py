#!/usr/bin/env python3
"""Metadata guards only. These synthetic values are not device query evidence."""
import importlib.util,json,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('pack_native',Path(__file__).with_name('pack-native-texture.py'));pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
class Identity(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.path=Path(self.temp.name)/'native-resource.json'
  self.base={'contract':'DM native image producer v1','complete':True,'driverID':18,'layout_UUID':'01'*16,'device_UUID':'02'*16,'driver_UUID':'03'*16,'producer_source_sha256':'ab'*32}
 def tearDown(self):self.temp.cleanup()
 def read(self,**change):
  self.path.write_text(json.dumps(self.base|change));return pack.queried_identity(self.path)
 def test_distinct_properties_preserved(self):
  value,producer=self.read();self.assertEqual(value,bytes.fromhex('01'*16+'02'*16+'03'*16));self.assertEqual(producer,bytes.fromhex('ab'*32))
 def test_panvk_does_not_masquerade(self):
  with self.assertRaises(ValueError):self.read(driverID=20)
 def test_incomplete(self):
  with self.assertRaises(ValueError):self.read(complete=False)
 def test_zero(self):
  with self.assertRaises(ValueError):self.read(layout_UUID='00'*16)
 def test_nonhex(self):
  with self.assertRaises(ValueError):self.read(device_UUID='gg'*16)
 def test_missing(self):
  with self.assertRaises(ValueError):self.read(driver_UUID=None)
 def test_short_source(self):
  with self.assertRaises(ValueError):self.read(producer_source_sha256='ab')
 def test_wrong_contract(self):
  with self.assertRaises(ValueError):self.read(contract='other')
if __name__=='__main__':unittest.main()
