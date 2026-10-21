#!/usr/bin/env python3
"""Actual artifact gate acceptance and source/format/closure refusal offline."""
from pathlib import Path
import hashlib,importlib.util,json,unittest
D=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('gate',D/'candidate_gate.py');g=importlib.util.module_from_spec(s);s.loader.exec_module(g)
def read(p):return json.loads(Path(p).read_text())
def pinned(row):
 p=Path(row['path']);assert hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256'];return p
def require(v,m):
 if not v:raise RuntimeError(m)
class Cold(unittest.TestCase):
 def config(self):
  p=read('/home/dl/d2r-mobile/.local/build/native-cpu-combined-pair-v1/candidate-C-inputs-DRAFT.json');p['runtime_commit']=g.COLD_RUNTIME
  p['canonical']={'path':'/home/dl/d2r-mobile/.local/logs/cpu-cold-phase-outline-53c7d26-canonical-source-ready-agent.json','sha256':g.COLD_CANONICAL}
  p['raw']={'path':'/home/dl/d2r-mobile/.local/logs/cpu-cold-phase-outline-53c7d26-raw-nso-ready-agent.json','sha256':g.COLD_RAW}
  p['native_candidate_validation']['root_review']={'path':'/home/dl/d2r-mobile/.local/logs/cpu-cold-phase-outline-53c7d26-root-canonical-raw-review.json','sha256':g.COLD_REVIEW}
  for phase in p['phases']:phase['composition_data']=read(pinned(phase['composition']))
  return p
 def changed(self,target,key,value):
  def altered(path):
   d=read(path)
   if Path(path)==Path(target):
    at=d
    for part in key[:-1]:at=at[part]
    at[key[-1]]=value
   return d
  return altered
 def test_actual_closure_accepted_and_control_named(self):
  p=self.config();g.validate(p,pinned,read,require);self.assertEqual(p['cold_phase_comparison']['reference_runtime'],g.STORE_RUNTIME)
 def test_pending_wrong_root_rejected(self):
  p=self.config();p['native_candidate_validation']['root_review']['sha256']='0'*64
  with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def test_compile_reference_or_missing_APP_closure_rejected(self):
  p=self.config();target=p['canonical']['path']
  for key,value in [(['effective_build','cold_phase_compile_reference'],True),(['effective_build','cold_phase_default_outline'],False),(['new_APP_fixture_exclusion','literals_absent'],[]),(['new_APP_fixture_exclusion','actual_preprocessed_closure'],[])]:
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(self.config(),pinned,self.changed(target,key,value),require)
 def test_raw_revision_or_artifacts_rejected(self):
  p=self.config();target=p['raw']['path']
  for key,value in [(['source_commit'],g.STORE_RUNTIME),(['all_23_module_rule_artifact_bytes_equal_d148'],False),(['executables_equal_d148'],False),(['revision_provenance_fresh'],False),(['28_deployment_file_hashes'],{})]:
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(self.config(),pinned,self.changed(target,key,value),require)
 def test_ASTC6_same_count_does_not_inherit_CPU_gate(self):
  p=self.config();p['phases'][1]['composition']['sha256']='0'*64
  with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def test_actual_control_cleanup_required(self):
  target='/home/dl/d2r-mobile/.local/logs/native-cpu-combined-pair-v1-candidate-C-root-actual-review.json'
  for key,value in [(['game_modules_executed'],False),(['route_completed'],0),(['fresh_save_then_stopped'],False),(['cleanup_followup_all_owned_not_executing'],False)]:
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(self.config(),pinned,self.changed(target,key,value),require)
if __name__=='__main__':unittest.main()
