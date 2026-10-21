#!/usr/bin/env python3
"""Exact new runtime closure, independently reviewed assets, no device calls."""
import copy,importlib.util,unittest
from pathlib import Path
from unittest.mock import patch
class PinnedName(str):
 def stat(self):return type('Stat',(),{'st_size':4339})()
D=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('gate',D/'candidate_gate.py');g=importlib.util.module_from_spec(s);s.loader.exec_module(g)
def require(v,m):
 if not v:raise RuntimeError(m)
class PoolCombined(unittest.TestCase):
 def setUp(self):
  self.constants=patch.multiple(g,POOL_REVIEW='1'*64,POOL_CANONICAL='2'*64,POOL_RAW='3'*64);self.constants.start();self.addCleanup(self.constants.stop)
  self.p={'official_l1i_record':True,'performance_tools':{'reviewed':True},'runtime_commit':g.POOL_RUNTIME,'pool_window_bytes':1048576,'driver':{'sha256':g.DRIVER},'qualified_native_asset_validation':{'root_asset_review':{'sha256':g.POOL_ASSETS}},'canonical':{'path':'canonical','sha256':g.POOL_CANONICAL},'raw':{'path':'raw','sha256':g.POOL_RAW},'native_candidate_validation':{'root_review':{'path':'review','sha256':g.POOL_REVIEW},'qualified_base_pair':{},'qualified_predecessor_short':{}}}
  self.can={'commit':g.POOL_RUNTIME,'canonical_APK':{'archive':{'path':'APK','sha256':'6'*64}},'canonical_native_library_sha256':{'libdmhost_android.so':'7'*64,**{str(n):str(n)*64 for n in range(5)}},'canonical_host_ELF_build_id':'8'*40,'effective_build':{'cold_phase_compile_reference':False,'cold_phase_default_outline':True,'pool_pin_compile_reference':False,'pool_pin_default_borrow':True},'new_APP_fixture_exclusion':{'symbols_absent':['vk_test_pool_operation_pin_counts']},'source_only_candidate':{'path':'source','sha256':g.POOL_SOURCE,'bytes':4339}}
  self.raw={'source_commit':g.POOL_RUNTIME,'28_deployment_file_hashes':{str(n):'a'*64 for n in range(28)},'raw_original_inputs':{str(n):{}for n in range(3)},'all_23_module_rule_artifact_bytes_equal_53c':True,'executables_equal_53c':True,'revision_provenance_fresh':True}
  self.review={'status':'ROOT_REVIEWED_179C82D_CANONICAL_AND_FRESH_RAW28','runtime':g.POOL_RUNTIME,'base':'53c7d26628142a383c9c5f8d39a3edc23a1674c3','tool_ready':True,'actual_device_evidence':False,'canonical_ready':self.p['canonical'],'raw_ready':self.p['raw'],'APK':self.can['canonical_APK']['archive'],'host_ELF_SHA256':'7'*64,'host_ELF_BuildID':'8'*40,'source_ready':self.can['source_only_candidate'],'component_source_reviews':[{'path':'pinreview'},{'path':'schemareview'}],'actual_APP_preprocessed_units':6,**{k:True for k in ['raw28_hashes_verified','six_APK_libraries_verified','new_fixture_symbols_absent_APP','compile_reference_OFF']}}
 def read(self,name):
  if str(name).endswith('native-qualified-astc6-full-game-d148-root-actual-review.json'):
   return {'status':'ROOT_REVIEWED_ACTUAL_FULL2838_ORIGINAL_GAME_SHORT_QUALIFICATION',**{k:True for k in ['game_modules_executed','original_three_routes_complete','fresh_save2_then_stopped','original_restored','cleanup_complete','visual_root_actually_viewed_before_after']},'cleanup_errors':[],**{k:{'path':k}for k in ['facts','result','source']}}
  return {'canonical':self.can,'raw':self.raw,'review':self.review,'/home/dl/d2r-mobile/.local/logs/cpu-store-phase-combined-d14808f-root-canonical-raw-review.json':{'canonical_ready':{'path':'oldcanonical'},'raw_ready':{'path':'oldraw'}}}[str(name)]
 def invoke(self):
  with patch.object(g,'validate_store')as old:
   g.validate(self.p,lambda r:PinnedName(r['path']),self.read,require)
   self.assertEqual(old.call_count,1)
   prior=old.call_args.args[0];self.assertEqual(prior['runtime_commit'],g.STORE_RUNTIME);self.assertEqual(prior['canonical']['path'],'oldcanonical');self.assertEqual(self.p['runtime_commit'],g.POOL_RUNTIME)
 def test_exact_new_closure_calls_unmodified_prior_prerequisite(self):self.invoke()
 def test_pending_or_unreviewed_refused(self):
  self.p['native_candidate_validation']['root_review']['sha256']='0'*64
  with self.assertRaises(RuntimeError):self.invoke()
 def test_wrong_effective_flags_refused(self):
  for key in self.can['effective_build']:
   with self.subTest(key=key):
    self.can['effective_build'][key]=not self.can['effective_build'][key]
    with self.assertRaises(RuntimeError):self.invoke()
    self.can['effective_build'][key]=not self.can['effective_build'][key]
 def test_incomplete_root_or_APP_closure_refused(self):
  for key in ['raw28_hashes_verified','new_fixture_symbols_absent_APP','compile_reference_OFF']:
   with self.subTest(key=key):
    self.review[key]=False
    with self.assertRaises(RuntimeError):self.invoke()
    self.review[key]=True
  self.can['new_APP_fixture_exclusion']['symbols_absent']=[]
  with self.assertRaises(RuntimeError):self.invoke()
 def test_raw_revision_or_28_integrity_refused(self):
  for key,value in [('source_commit',g.STORE_RUNTIME),('revision_provenance_fresh',False),('all_23_module_rule_artifact_bytes_equal_53c',False),('28_deployment_file_hashes',{})]:
   with self.subTest(key=key):
    old=self.raw[key];self.raw[key]=value
    with self.assertRaises(RuntimeError):self.invoke()
    self.raw[key]=old
 def test_source_ready_byte_count_is_verified(self):
  self.can['source_only_candidate']['bytes']=1
  with self.assertRaises(RuntimeError):self.invoke()
 def test_assets_remain_independent_and_full_only(self):
  self.p['qualified_native_asset_validation']['root_asset_review']['sha256']='0'*64
  with self.assertRaises(RuntimeError):self.invoke()
 def test_window0_or_wrong_driver_refused(self):
  self.p['pool_window_bytes']=0
  with self.assertRaises(RuntimeError):self.invoke()
  self.p['pool_window_bytes']=1048576;self.p['driver']['sha256']='0'*64
  with self.assertRaises(RuntimeError):self.invoke()
if __name__=='__main__':unittest.main()
