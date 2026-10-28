"""Real parent+FS pairs, independently regenerated closure and tamper controls."""
from pathlib import Path
import json,shutil,struct,sys,tempfile,unittest
TOOLS=Path(__file__).resolve().parents[1];sys.path.insert(0,str(TOOLS))
import dmsh_derived_bounded_uniform_prefetch as D
ROOT=TOOLS.parents[2];BASE=ROOT/'.local/build/adreno-shader-upstream-rewrites-v2/vk-29e56b51bce5'
VS=['3deaf68fd65b5298','54c93fa22abff9ee'];FS=['5c1ca4e7c3e4657c','38f344adf801b153'];KEYS=VS+FS
class BoundedPrefetch(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  if not BASE.is_dir():raise unittest.SkipTest('Original real cache unavailable')
  cls.tmp=tempfile.TemporaryDirectory();cls.root=Path(cls.tmp.name);raw=cls.root/'original';raw.mkdir()
  for k in KEYS:
   for suffix in D.P.COMMON+('meta',)+(('spec',)if k in FS else()):shutil.copyfile(BASE/(k+'.'+suffix),raw/(k+'.'+suffix))
  manifest=json.loads((BASE/'_vk_manifest.json').read_text());manifest['expected']=manifest['validated']=4;(raw/'_vk_manifest.json').write_text(json.dumps(manifest));(raw/'.chain').write_text(manifest['chain']+'\n');(raw/'_vk_build.json').write_text(json.dumps([{'key':k,'st':'ok'}for k in KEYS]));receipt=cls.root/'receipt.json';receipt.write_text(json.dumps({'chain':manifest['chain'],'validated_game_and_builtin_shaders':4,'original_proofs_unchanged':True,'artifacts':{n:D.artifact(p)for n,p in D.B.files(raw).items()}}))
  parent=D.P.build(raw,receipt,D.sha(receipt),KEYS,cls.root/'parent',cls.root/'parent.json',expected_shaders=4);cls.parent=Path(parent['output']);cls.parent_sha=D.sha(cls.parent/D.RECORD)
  cls.record=D.build(cls.parent,cls.parent_sha,FS,cls.root/'output',cls.root/'report.json',expected_shaders=4);cls.cache=Path(cls.record['output'])
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def clone(self):
  td=tempfile.TemporaryDirectory();self.addCleanup(td.cleanup);p=Path(td.name)/self.cache.name;shutil.copytree(self.cache,p);return p
 def ledger(self,out,name):
  p=out/D.RECORD;r=json.loads(p.read_text());r['artifacts'][name]=D.artifact(out/name);p.write_text(json.dumps(r))
 def verify(self,p):return D.verify(p,self.parent_sha,self.parent,4)
 def test_full_real_recipe_and_closure_and_actual_qualified_payload(self):
  self.assertEqual(self.record['independent_verification']['status'],'passed-not-deployed')
  qualified=ROOT/'.local/logs/fs-bounded-uniform-table-proof-v2'
  for k in FS:
   original=(self.parent/(k+'.spec')).read_bytes();envelope=(self.cache/(k+'.spec')).read_bytes()
   legacy,candidate,guard=D.unpack((self.cache/(k+'.spv')).read_bytes(),envelope,(self.cache/(k+'.meta')).read_bytes())
   self.assertEqual(legacy,original);self.assertEqual(guard,{'binding':1,'reg':13,'min_effective_bytes':192})
   self.assertEqual(candidate,(qualified/(k+'-prefetch.spv')).read_bytes())
   for s in ['meta','spv']:self.assertEqual((self.cache/(k+'.'+s)).read_bytes(),(self.parent/(k+'.'+s)).read_bytes())
 def test_envelope_rejects_corrupt_guard_hash_size_and_reserved(self):
  k=FS[0];base=(self.cache/(k+'.spv')).read_bytes();metadata=(self.cache/(k+'.meta')).read_bytes();source=(self.cache/(k+'.spec')).read_bytes()
  for offset in [0,4,8,12,16,20,24,28,60,92,124,128,len(source)-4]:
   blob=bytearray(source);blob[offset]^=1
   with self.assertRaises(ValueError):D.unpack(base,bytes(blob),metadata)
 def test_payload_and_selector_tamper_rejected_even_with_updated_ledger(self):
  for position in [16,60]:
   p=self.clone();name=FS[0]+'.spec';raw=bytearray((p/name).read_bytes());raw[position]^=1;(p/name).write_bytes(raw);self.ledger(p,name)
   with self.assertRaisesRegex(ValueError,'regeneration mismatch'):self.verify(p)
 def test_closure_tamper_rejected_even_with_updated_ledger(self):
  p=self.clone();name=FS[0]+'.dkey';raw=bytearray((p/name).read_bytes());raw[-1]^=1;(p/name).write_bytes(raw);self.ledger(p,name)
  with self.assertRaisesRegex(ValueError,'closure regeneration'):self.verify(p)
 def test_unrelated_base_cannot_change_even_with_updated_ledger(self):
  p=self.clone();name=VS[0]+'.spv';(p/name).write_bytes((p/name).read_bytes()+b'\0'*4);self.ledger(p,name)
  with self.assertRaisesRegex(ValueError,'Unrelated parent artifact'):self.verify(p)
 def test_archived_parent_input_and_record_authority(self):
  p=self.clone();name=FS[0]+'.prefetch-input-spec';(p/name).write_bytes((p/name).read_bytes()+b'\0'*4);self.ledger(p,name)
  with self.assertRaisesRegex(ValueError,'Archived selected parent'):self.verify(p)
  p=self.clone();r=json.loads((p/D.PARENT).read_text());r['chain']='bad';(p/D.PARENT).write_text(json.dumps(r));self.ledger(p,D.PARENT)
  with self.assertRaisesRegex(ValueError,'Pinned archived parent'):self.verify(p)
 def test_archived_verifier_tamper_is_rejected_even_with_updated_ledger(self):
  p=self.clone();name=D.PARENT_VERIFIER;(p/name).write_bytes((p/name).read_bytes()+b'\n# forged\n');self.ledger(p,name)
  with self.assertRaisesRegex(ValueError,'Exact recorded parent verifier'):self.verify(p)
 def test_unknown_file_and_symlink_rejected(self):
  p=self.clone();(p/'unknown').write_bytes(b'x')
  with self.assertRaisesRegex(ValueError,'Undeclared'):self.verify(p)
  p=self.clone();name=VS[0]+'.meta';(p/name).unlink();(p/name).symlink_to(self.parent/name)
  with self.assertRaisesRegex(ValueError,'ordinary immediate'):self.verify(p)
 def test_build_refuses_parent_links_or_unknown_files_before_copy(self):
  for symlink in [True,False]:
   td=tempfile.TemporaryDirectory();self.addCleanup(td.cleanup);root=Path(td.name);parent=root/self.parent.name;shutil.copytree(self.parent,parent)
   if symlink:
    name=FS[0]+'.spec';(parent/name).unlink();(parent/name).symlink_to(self.parent/name)
   else:(parent/'unknown').write_bytes(b'x')
   with self.assertRaisesRegex(ValueError,'ordinary immediate|Parent file set'):
    D.build(parent,self.parent_sha,FS,root/'output',root/'report.json',expected_shaders=4)
   self.assertFalse((root/'output').exists())
 def test_parent_qualification_and_wrong_stage_rejected(self):
  with self.assertRaisesRegex(ValueError,'Parent record identity'):D.parent_record(self.parent,'0'*64,4)
  with self.assertRaisesRegex(ValueError,'existing fragment pair'):D.selected(self.parent,VS[0])
if __name__=='__main__':unittest.main(verbosity=2)
