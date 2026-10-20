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
  p=read(R/'.local/build/native-candidate-short-26b-v1/inputs-DRAFT.json')
  p['phases'][1]['composition_data']=read(pinned(p['phases'][1]['composition']))
  return p
 def test_actual_reviewed26b_and6f_refs_are_accepted(self):
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

if __name__=='__main__':unittest.main(verbosity=2)
