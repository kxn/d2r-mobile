#!/usr/bin/env python3
"""Offline actual pinned metadata and negative logical-admission checks."""
from pathlib import Path
import copy
import hashlib
import importlib.util
import json
import unittest

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('candidate_gate_test',HERE/'candidate_gate.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
R=Path('/home/dl/d2r-mobile')


def require(v,msg):
 if not v:raise RuntimeError(msg)
def read(path):return json.loads(Path(path).read_text())
def pinned(row):
 path=Path(row['path']).resolve()
 require(hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256'],'actual source pin differs')
 return path


class Gate(unittest.TestCase):
 def config(self):
  p=read(R/'.local/build/native-candidate-source-reuse-8480-v1/inputs-DRAFT.json')
  p['phases'][1]['composition_data']=read(pinned(p['phases'][1]['composition']))
  return p
 def test_actual_reviewed8480_ebe_and6f_refs_are_accepted(self):
  g.validate(self.config(),pinned,read,require)
 def test_wrong_runtime_provider_window_or_selection_rejected(self):
  for key,value in [('runtime_commit',g.BASE),('driver',{'sha256':'0'*64}),('pool_window_bytes',16777216)]:
   with self.subTest(key=key):
    p=self.config();p[key]=value
    with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
  p=self.config();p['phases'][1]['composition_data']['entry_count']=1
  with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def test_LLDB_SDP_or_sustained_conflict_rejected(self):
  for key in ['counter_snapshot','official_gpu_tools','sustained_play']:
   with self.subTest(key=key):
    p=self.config();p[key]={}
    with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def test_CPU_tools_are_required(self):
  p=self.config();p.pop('performance_tools')
  with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def test_wrong_root_review_and_canonical_rejected(self):
  p=self.config();p['native_candidate_validation']['root_review']['sha256']='0'*64
  with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
  p=self.config();p['canonical']=p['native_candidate_validation']['qualified_base_pair']
  with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def test_qualified_base_runtime_and_cleanup_required(self):
  p=self.config();q=Path(p['native_candidate_validation']['qualified_base_pair']['path'])
  for key,value in [('runtime',g.RUNTIME),('cleanup_followup_all_owned_not_executing',False)]:
   def altered(path):
    value_read=read(path)
    if Path(path)==q:value_read[key]=value
    return value_read
   with self.subTest(key=key):
    with self.assertRaises(RuntimeError):g.validate(p,pinned,altered,require)


 def test_actual_predecessor_pin_required(self):
  p=self.config();p['native_candidate_validation']['qualified_predecessor_short']['sha256']='0'*64
  with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def test_predecessor_restore_and_saved_progress_required(self):
  p=self.config();q=Path(p['native_candidate_validation']['qualified_predecessor_short']['path'])
  for key,value in [('runtime',g.RUNTIME),('game_modules_executed',False),('route_completed',2)]:
   def altered(path):
    data=read(path)
    if Path(path)==q:data[key]=value
    return data
   with self.subTest(key=key):
    with self.assertRaises(RuntimeError):g.validate(p,pinned,altered,require)
 def test_actual_predecessor_phase_must_be_complete(self):
  p=self.config();actual=R/'.local/logs/native-candidate-source-span-ebe-Q735-v1/result.json'
  for key,value in [('original_route_completed',2),('fresh_save_then_stopped',False),('official_performance_complete',False),('game_modules_executed',False)]:
   def altered(path):
    data=read(path)
    if Path(path)==actual:data['phases'][0][key]=value
    return data
   with self.subTest(key=key):
    with self.assertRaises(RuntimeError):g.validate(p,pinned,altered,require)
 def test_root_artifact_extent_exact(self):
  p=self.config();q=Path(p['native_candidate_validation']['root_review']['path'])
  for key,value in [('base',g.BASE),('actual_device_authorized',True),('tool_ready_authorized',False),('APK',{}),('host_ELF_SHA256','0'*64),('host_ELF_BuildID','0'*40)]:
   def altered(path):
    data=read(path)
    if Path(path)==q:data[key]=value
    return data
   with self.subTest(key=key):
    with self.assertRaises(RuntimeError):g.validate(p,pinned,altered,require)
 def test_actual_complete_build_and_separate_verify_required(self):
  p=self.config();review=read(pinned(p['native_candidate_validation']['root_review']))
  q=Path(review['complete_audit']['path'])
  for key,value in [('commit',g.BASE),('separate_original_NSO_rebuild_and_verify_only_pass',False),('new_host_cache_getter_and_epoch_death_symbols_absent_APP',False),('native_APP_death_env_literal_absent',False),('operation_geometry_APP_probes_absent',False),('texture_retirement_index_APP_probes_absent',False),('operation_image_source_APP_probes_absent',False),('pool_span_search_APP_probes_absent',False),('source_refresh_functional_observer_absent_APP',False),('six_library_hashes_and_LOAD_alignment',[]),('raw_deployment_files',27)]:
   def altered(path):
    data=read(path)
    if Path(path)==q:data[key]=value
    return data
   with self.subTest(key=key):
    with self.assertRaises(RuntimeError):g.validate(p,pinned,altered,require)
 def test_qualified_predecessor_does_not_fake_stable_performance(self):
  p=self.config();q=R/'.local/logs/native-candidate-source-span-ebe-Q735-v1/result.json'
  self.assertFalse(read(q)['phases'][0]['performance_stable_target60_PSO_variants_all_windows'])
  g.validate(p,pinned,read,require)

if __name__=='__main__':unittest.main(verbosity=2)
