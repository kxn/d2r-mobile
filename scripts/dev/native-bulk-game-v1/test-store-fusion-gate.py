#!/usr/bin/env python3
"""Reject unqualified predecessor, altered host or raw provenance offline."""
from pathlib import Path
import hashlib,importlib.util,json,unittest
D=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('gate',D/'candidate_gate.py');g=importlib.util.module_from_spec(s);s.loader.exec_module(g)
def read(p):return json.loads(Path(p).read_text())
def pinned(r):
 p=Path(r['path']);assert hashlib.sha256(p.read_bytes()).hexdigest()==r['sha256'];return p
def require(v,m):
 if not v:raise RuntimeError(m)
class Store(unittest.TestCase):
 def config(self):
  p=read('/home/dl/d2r-mobile/.local/build/native-cpu-combined-pair-v1/candidate-C-inputs-DRAFT.json');p['phases'][1]['composition_data']=read(pinned(p['phases'][1]['composition']));return p
 def test_actual_closure_accepted(self):g.validate(self.config(),pinned,read,require)
 def test_raw_not_relabelled_or_altered(self):
  for key,val in [('source_commit',g.COMBINED_RUNTIME),('executables_equal959',False),('revision_provenance_fresh',False)]:
   p=self.config();rp=Path(p['raw']['path'])
   def changed(path):
    d=read(path)
    if Path(path)==rp:d[key]=val
    return d
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(p,pinned,changed,require)
 def test_predecessor_game_or_save_missing(self):
  rp=Path('/home/dl/d2r-mobile/.local/logs/native-cpu-combined-pair-v1-candidate-B-root-actual-review.json')
  for key,val in [('route_completed',0),('game_modules_executed',False),('fresh_save_then_stopped',False)]:
   def changed(path):
    d=read(path)
    if Path(path)==rp:d[key]=val
    return d
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(self.config(),pinned,changed,require)
 def test_predecessor_original_not_restored(self):
  rp=Path('/home/dl/d2r-mobile/.local/logs/native-cpu-combined-pair-v1-candidate-B/result.json')
  def changed(path):
   d=read(path)
   if Path(path)==rp:d['original_restored']=False
   return d
  with self.assertRaises(RuntimeError):g.validate(self.config(),pinned,changed,require)
if __name__=='__main__':unittest.main()
