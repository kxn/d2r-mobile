#!/usr/bin/env python3
"""Check new runtime identity and honest raw-producer cache compatibility offline."""
from pathlib import Path
import copy,hashlib,importlib.util,json,unittest
HERE=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('gate',HERE/'candidate_gate.py');g=importlib.util.module_from_spec(s);s.loader.exec_module(g)
def read(p):return json.loads(Path(p).read_text())
def pinned(row):
 p=Path(row['path']);assert hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256'];return p
def require(v,m):
 if not v:raise RuntimeError(m)
class Combined(unittest.TestCase):
 def config(self):
  p=read('/home/dl/d2r-mobile/.local/build/native-cpu-combined-pair-v1/candidate-B-inputs-DRAFT.json');p['phases'][1]['composition_data']=read(pinned(p['phases'][1]['composition']));return p
 def test_actual_root_reviewed_cache_and_APK(self):g.validate(self.config(),pinned,read,require)
 def test_unreviewed_root(self):
  p=self.config();p['native_candidate_validation']['root_review']['sha256']='0'*64
  with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def test_wrong_raw_origin_or_incomplete_compatibility(self):
  for key,value in [('raw_producer_commit',g.RUNTIME),('guarded_runtime_commit',g.RUNTIME),('all_28_before_after_legal_cache_and_separate_verify_exact',False),('source_revision_provenance_preserved_not_relabelled',False)]:
   p=self.config();raw_path=Path(p['raw']['path'])
   def altered(path):
    d=read(path)
    if Path(path)==raw_path:d[key]=value
    return d
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(p,pinned,altered,require)
 def test_rule_difference(self):
  p=self.config();raw_path=Path(p['raw']['path'])
  def altered(path):
   d=read(path)
   if Path(path)==raw_path:d['source_and_rule_closure'][0]['same_current_guarded_source']=False
   return d
  with self.assertRaises(RuntimeError):g.validate(p,pinned,altered,require)
 def test_LLDB_sustained_or_GPU_tools_excluded(self):
  for key in ['counter_snapshot','sustained_play','official_gpu_tools']:
   p=self.config();p[key]={}
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
if __name__=='__main__':unittest.main()
