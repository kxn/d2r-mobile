#!/usr/bin/env python3
"""Offline exact-evidence gates and strict-packer membership refusal."""
import copy, hashlib, importlib.util, json, struct, unittest
from pathlib import Path
D=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('asset', D/'qualified_asset_gate.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
R=Path('/home/dl/d2r-mobile')
def read(p):return json.loads(Path(p).read_text())
def require(v,m):
 if not v:raise RuntimeError(m)
def pinned(r):
 p=Path(r['path']).resolve();require(hashlib.sha256(p.read_bytes()).hexdigest()==r['sha256'],'pin');return p
class Asset(unittest.TestCase):
 def config(self):
  p=read(R/'.local/build/native-qualified-astc6-game-v1/inputs-DRAFT.json')
  p['phases'][1]['composition_data']=read(pinned(p['phases'][1]['composition']))
  return p
 def test_explicit_mode_and_review(self):
  for key,value in [('runtime_commit','wrong')]:
   p=self.config();p[key]=value
   with self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
  for key in ('root_asset_review','dual_NVN_review'):
   p=self.config();p['qualified_native_asset_validation'][key]['sha256']='0'*64
   with self.subTest(review=key),self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def test_old_table_or_old_composition_not_accepted(self):
  for key in ('composition','native_table'):
   p=self.config();p['phases'][1][key]['sha256']='0'*64
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def test_scope_or_authorization_mutation(self):
  p=self.config();reviewpath=Path(p['qualified_native_asset_validation']['root_asset_review']['path'])
  for key,value in [('allow_bounded_original_game_qualification',False),('CPU_matched_asset_AB',True),('game_acceptance',True),('replacement_count',33),('production_default_enable',True)]:
   def changed(path):
    d=read(path)
    if Path(path)==reviewpath:d[key]=value
    return d
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(p,pinned,changed,require)
 def test_UI_TVFS_and_historical_flags(self):
  for key in ('deploy_allowed','game_acceptance','original_CASC_roundtrip','upstream_UI_preserved','original_packer_unchanged','game_binary_modified'):
   p=self.config();d=p['phases'][1]['composition_data'];d[key]=not d[key]
   with self.subTest(key=key),self.assertRaises(RuntimeError):g.validate(p,pinned,read,require)
 def members(self):
  old=[dict(id=i+1,logical=f'x/{i}',image_size=94208) for i in range(6217)]
  new=copy.deepcopy(old);a=bytearray(160+6217*1056);b=bytearray(a)
  changes=[]
  for i in range(32):
   new[i]['image_size']=53248
   struct.pack_into('<I',a,160+i*1040+80,157);struct.pack_into('<I',b,160+i*1040+80,165)
   changes.append(dict(id=i+1,logical=old[i]['logical'],old_native_bytes=94208,new_native_bytes=53248))
  return old,new,a,b,changes
 def test_membership_positive(self):g.validate_membership(*self.members(),require)
 def test_unselected_recipe_or_family_or_detail(self):
  for kind in ('recipe','family','detail'):
   old,new,a,b,rows=self.members()
   if kind=='recipe':b[160+33*1040+10]=1
   elif kind=='family':b[160+6217*1040+33*16+2]=1
   else:new[33]['extra']='changed'
   with self.subTest(kind=kind),self.assertRaises(RuntimeError):g.validate_membership(old,new,a,b,rows,require)
 def test_extra_or_missing_or_duplicate_replacement(self):
  for kind in ('missing','duplicate','outside'):
   old,new,a,b,rows=self.members()
   if kind=='missing':rows.pop()
   elif kind=='duplicate':rows[-1]=rows[0]
   else:rows[-1]['id']=6218
   with self.subTest(kind=kind),self.assertRaises(RuntimeError):g.validate_membership(old,new,a,b,rows,require)
 def test_order_identity_or_format(self):
  for kind in ('order','name','format','size'):
   old,new,a,b,rows=self.members()
   if kind=='order':new[100]['id']=2
   elif kind=='name':new[0]['logical']='other'
   elif kind=='format':struct.pack_into('<I',b,240,158)
   else:new[0]['image_size']=94208
   with self.subTest(kind=kind),self.assertRaises(RuntimeError):g.validate_membership(old,new,a,b,rows,require)
if __name__=='__main__':unittest.main()
