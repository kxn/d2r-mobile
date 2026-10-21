#!/usr/bin/env python3
"""Allocation policy/actual-source gates; synthetic evidence is never baked output."""
import copy, hashlib, importlib.util, json, tempfile, unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('composition',Path(__file__).with_name('compose-qualified-native-astc6.py'))
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)

class FullProfile(unittest.TestCase):
 def test_pilot_default_keeps_original_inputs_and_pins(self):
  p,a,n,pins=c.profile_inputs('pilot32')
  self.assertEqual((p,a,n,pins),(c.NEW_PREP,c.ACTUAL,32,c.PINS))
 def test_pilot_does_not_take_new_actual_overrides(self):
  with self.assertRaises(ValueError):c.profile_inputs('pilot32','a'*64,'b'*64)
 def test_full_missing_or_malformed_actual_SHA_rejected(self):
  for a,b in [(None,None),('a'*64,None),('A'*64,'b'*64),('x','y')]:
   with self.subTest(a=a,b=b),self.assertRaises(ValueError):c.profile_inputs('qualified-full2838-v1',a,b)
 def test_full_pins_new_prepare_selection_and_actual(self):
  p,a,n,pins=c.profile_inputs('qualified-full2838-v1','a'*64,'b'*64)
  self.assertEqual((p,a,n),(c.FULL_PREP,c.FULL_ACTUAL,2838));self.assertNotIn(c.NEW_PREP,pins)
  self.assertEqual(pins[c.FULL_PREP],c.FULL_PREP_SHA)
  self.assertEqual(pins[c.FULL_PREP.parent.parent/'selection.json'],c.FULL_SELECTION_SHA)
  self.assertEqual(pins[c.FULL_ACTUAL/'result.json'],'a'*64)
  self.assertEqual(pins[c.FULL_ACTUAL/'actual-batch-receipt-index.json'],'b'*64)
 def test_unknown_profile_rejected(self):
  with self.assertRaises(ValueError):c.profile_inputs('all')
 def test_allocation_larger_never_replaces_even_when_blocks_smaller(self):
  self.assertEqual(c.allocation_decision(4096,8192,4096,1024,5000,2000)[:2],('larger',False))
 def test_allocation_smaller_uses_actual_not_source_padding(self):
  self.assertEqual(c.allocation_decision(8192,4096,4096,4096,5000,5000)[:2],('strictsmaller',True))
 def test_equal_retains_useful_block_or_source_reduction_only(self):
  for blocks,source,keep in [(2048,5000,True),(4096,3000,True),(4096,5000,False),(5000,6000,False)]:
   with self.subTest(blocks=blocks,source=source):self.assertEqual(c.allocation_decision(8192,8192,4096,blocks,5000,source)[:2],('equal',keep))
 def test_bad_actual_or_source_counts_rejected(self):
  for v in [0,-1,True,1.0,None]:
   for k in range(6):
    args=[4096]*6;args[k]=v
    with self.subTest(value=v,index=k),self.assertRaises(ValueError):c.allocation_decision(*args)
 def metadata(self):
  source={'VkFormat':165,'width':16,'height':16,'depth':1,'layers':1,'levels':1,'block':[6,6],
   'linear_ASTC_bytes':144,'mips':[dict(level=0,width=16,height=16,linear_bytes=144,sha256='c'*64)]}
  entry={'source':{'sha256':'a'*64},'texture':{'sha256':'b'*64}}
  meta={'complete':True,'producer_source_sha256':c.PRODUCER,'declaration_sha256':'a'*64,'texture_sha256':'b'*64,
   'recipe':{'VkFormat':165,'width':16,'height':16,'depth':1,'arrayLayers':1,'mipLevels':1},
   'original_driver_roundtrip_compressed_blocks_exact':True,'physical_image_bytes':4096,
   'mips':[dict(level=0,width=16,height=16,linear_ASTC_sha256='c'*64)]}
  return meta,source,entry
 def test_actual_metadata_source_accepted(self):self.assertEqual(c.actual_allocation_bytes(*self.metadata()),4096)
 def test_wrong_actual_source_or_recipe_rejected(self):
  for key,value in [('declaration_sha256','x'*64),('texture_sha256','x'*64),('producer_source_sha256','x'*64),('original_driver_roundtrip_compressed_blocks_exact',False),('physical_image_bytes',1<<64)]:
   meta,source,entry=self.metadata();meta[key]=value
   with self.subTest(key=key),self.assertRaises(ValueError):c.actual_allocation_bytes(meta,source,entry)
  for key,value in [('VkFormat',157),('width',17),('arrayLayers',2),('mipLevels',2)]:
   meta,source,entry=self.metadata();meta['recipe'][key]=value
   with self.subTest(key=key),self.assertRaises(ValueError):c.actual_allocation_bytes(meta,source,entry)
 def test_wrong_actual_mip_or_useful_count_rejected(self):
  for key,value in [('sha','d'*64),('width',15),('blocks',128)]:
   meta,source,entry=self.metadata()
   if key=='sha':meta['mips'][0]['linear_ASTC_sha256']=value
   elif key=='width':meta['mips'][0]['width']=value
   else:source['linear_ASTC_bytes']=value
   with self.subTest(key=key),self.assertRaises(ValueError):c.actual_allocation_bytes(meta,source,entry)
 def test_old32_actual_cannot_be_full2838(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)
   result={'stage':'actual-turnip-native-resource-batches-owned-passed','cleanup_errors':[],
    'original_before':{},'original_after':{},'original_unchanged':True,'fixture_PID_absent':True,
    'fixture_uninstalled':True,'fixture_tmp_absent':True,'owned_clients_stopped':True,
    'prepare_manifest_sha256':'a'*64,'entry_count':32,'batch_count':1,'first_batch':0,
    'producer_source_sha256':c.PRODUCER,'driver_sha256':'b'*64}
   (root/'result.json').write_text(json.dumps(result))
   class Hashes:
    def file(self,p):return 'a'*64
   with self.assertRaisesRegex(ValueError,'actual complete selection'):
    c.actual_gate(None,root,{'entries':[None]*2838,'batches':[None]*23},Path('new-full-prep'),{}, {'driver_sha256':'b'*64},Hashes())

if __name__=='__main__':unittest.main()
