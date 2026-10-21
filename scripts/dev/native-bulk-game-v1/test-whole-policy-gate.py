#!/usr/bin/env python3
"""Real root authorization/control accepted; scope and old gate refusal preserved."""
from pathlib import Path
import copy,hashlib,importlib.util,json,unittest
D=Path(__file__).resolve().parent
def module(n):
 s=importlib.util.spec_from_file_location(n,D/(n+'.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
g=module('whole_policy_gate');c=module('candidate_gate')
def read(p):return json.loads(Path(p).read_text())
def pinned(row):
 p=Path(row['path']);assert hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256'];return p
def require(v,m):
 if not v:raise RuntimeError(m)
class Whole(unittest.TestCase):
 def config(self):
  p=read('/home/dl/d2r-mobile/.local/build/shared-whole-d148-policy-probe-v1/inputs-DRAFT.json')
  for v in p['phases']:v['composition_data']=read(pinned(v['composition']))
  return p
 def test_real_root_card_and_actual_control(self):
  p=self.config();g.validate(p,pinned,read,require,c)
  self.assertEqual(p['pool_window_bytes'],0);self.assertTrue(p['shared_whole_policy_scope']['finite_gate_used_as_prerequisite_only'])
 def test_old_exact_finite_gate_still_refuses_zero(self):
  with self.assertRaises(RuntimeError):c.validate(self.config(),pinned,read,require)
 def test_no_wrong_root_or_runtime_or_policy_or_assets(self):
  for field,value in [('runtime_commit',c.COLD_RUNTIME),('pool_window_bytes',1048576),('official_l1i_record',False),('official_memory_map',None)]:
   p=self.config();p[field]=value
   with self.subTest(field=field),self.assertRaises(RuntimeError):g.validate(p,pinned,read,require,c)
  for field in ['shared_whole_policy_probe','native_candidate_validation']:
   p=self.config();p[field]['root_authorization'if field=='shared_whole_policy_probe'else'root_review']['sha256']='0'*64
   with self.subTest(field=field),self.assertRaises((RuntimeError,AssertionError)):g.validate(p,pinned,read,require,c)
  p=self.config();p['phases'][1]['composition']['sha256']='0'*64
  with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require,c)
 def test_control_cleanup_or_shared_authorization_cannot_be_faked(self):
  for key,value in [('cleanup_complete',False),('original_restored',False),('new_default',True),('legacy_fallback_allowed',True)]:
   p=self.config();auth=p['shared_whole_policy_probe']['root_authorization']['path'];control=read(auth)['actual_1MiB_control']['path']
   def changed(path):
    j=read(path)
    if str(path)==(control if key in('cleanup_complete','original_restored')else auth):j[key]=value
    return j
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(p,pinned,changed,require,c)
if __name__=='__main__':unittest.main()
