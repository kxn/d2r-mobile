"""Actual original VS2/FS2 plus DSP2 selectors, no synthetic game identities."""
from pathlib import Path
import json,shutil,struct,sys,tempfile,unittest
TOOLS=Path(__file__).resolve().parents[1];sys.path.insert(0,str(TOOLS))
import dmsh_derived_address_combined as D
ROOT=TOOLS.parents[2];BASE=ROOT/'.local/build/adreno-shader-upstream-rewrites-v2/vk-29e56b51bce5'
VS=['3deaf68fd65b5298','54c93fa22abff9ee'];FS=['5c1ca4e7c3e4657c','38f344adf801b153'];KEYS=VS+FS
class Combined(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  if not BASE.is_dir():raise unittest.SkipTest('Actual original cache unavailable')
  cls.t=tempfile.TemporaryDirectory();cls.root=Path(cls.t.name);cls.base=cls.root/'input';cls.base.mkdir()
  for k in KEYS:
   for s in D.COMMON+('meta',)+(('spec',)if k in FS else()):shutil.copyfile(BASE/(k+'.'+s),cls.base/(k+'.'+s))
  manifest=json.loads((BASE/'_vk_manifest.json').read_text());manifest['expected']=manifest['validated']=4
  (cls.base/'_vk_manifest.json').write_text(json.dumps(manifest));(cls.base/'.chain').write_text(manifest['chain']+'\n')
  (cls.base/'_vk_build.json').write_text(json.dumps([{'key':k,'st':'ok'}for k in KEYS]))
  cls.receipt=cls.root/'receipt.json';cls.receipt.write_text(json.dumps({'chain':manifest['chain'],'validated_game_and_builtin_shaders':4,'original_proofs_unchanged':True,'artifacts':{n:D.artifact(p)for n,p in D.B.files(cls.base).items()}}));cls.digest=D.sha(cls.receipt)
  cls.record=D.build(cls.base,cls.receipt,cls.digest,KEYS,cls.root/'out',cls.root/'report.json',expected_shaders=4);cls.cache=Path(cls.record['output'])
 @classmethod
 def tearDownClass(cls):cls.t.cleanup()
 def clone(self):
  t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);out=Path(t.name)/self.cache.name;shutil.copytree(self.cache,out);return out
 def ledger(self,out,name):
  p=out/D.RECORD;r=json.loads(p.read_text());r['artifacts'][name]=D.artifact(out/name);p.write_text(json.dumps(r))
 def test_all_real_modules_and_closures(self):
  self.assertEqual(self.record['independent_verification']['status'],'passed-not-deployed')
  for k in KEYS:self.assertEqual((self.cache/(k+'.meta')).read_bytes(),(BASE/(k+'.meta')).read_bytes())
  for k in VS:self.assertEqual((self.cache/(k+'.spv')).read_bytes(),(ROOT/'.local/build/ubo-address-derived-v3/vk-1065929e0840'/(k+'.spv')).read_bytes())
  for k in FS:
   before=struct.unpack_from('<7I',(BASE/(k+'.spec')).read_bytes());after=struct.unpack_from('<7I',(self.cache/(k+'.spec')).read_bytes());self.assertEqual(before[:6],after[:6])
   self.assertEqual(self.record['address_proofs'][k]['base']['changed_shift_results'],23)
   self.assertEqual(self.record['address_proofs'][k]['specialized_payload']['changed_shift_results'],23)
 def test_changed_selector_rejected_even_with_rewritten_artifact_ledger(self):
  out=self.clone();name=FS[0]+'.spec';raw=bytearray((out/name).read_bytes());struct.pack_into('<I',raw,16,4);(out/name).write_bytes(raw);self.ledger(out,name)
  with self.assertRaisesRegex(ValueError,'selector recipe regeneration'):D.verify(out,self.digest,4)
 def test_changed_base_rejected_even_with_rewritten_artifact_ledger(self):
  out=self.clone();name=FS[0]+'.spv';(out/name).write_bytes((BASE/name).read_bytes());self.ledger(out,name)
  with self.assertRaisesRegex(ValueError,'normalization regeneration'):D.verify(out,self.digest,4)
 def test_original_metadata_and_manifest_cannot_be_changed(self):
  out=self.clone();name=FS[0]+'.meta';(out/name).write_bytes((out/name).read_bytes()+b'\0'*4);self.ledger(out,name)
  with self.assertRaisesRegex(ValueError,'reflection identity|original artifact changed'):D.verify(out,self.digest,4)
  out=self.clone();name=D.BASE;m=json.loads((out/name).read_text());m['compiler_options']['pixel_local_runtime']=False;(out/name).write_text(json.dumps(m));self.ledger(out,name)
  with self.assertRaisesRegex(ValueError,'manifest identity'):D.verify(out,self.digest,4)
 def test_original_recipe_archive_and_extra_files_rejected(self):
  out=self.clone();name=FS[0]+'.address-input-spec';(out/name).write_bytes((out/name).read_bytes()+b'\0'*4);self.ledger(out,name)
  with self.assertRaisesRegex(ValueError,'archived recipe identity'):D.verify(out,self.digest,4)
  out=self.clone();(out/'unknown').write_bytes(b'x')
  with self.assertRaisesRegex(ValueError,'Undeclared'):D.verify(out,self.digest,4)
 def test_missing_fragment_pair_rejected(self):
  t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);p=Path(t.name)/'input';shutil.copytree(self.base,p);(p/(FS[0]+'.spec')).unlink()
  with self.assertRaisesRegex(ValueError,'Fragment requires'):D.selected(p,FS[0])
 def test_structural_packer_rejects_wrong_original_identity(self):
  k=FS[0];base=(BASE/(k+'.spv')).read_bytes();recipe=(BASE/(k+'.spec')).read_bytes()
  with self.assertRaises(ValueError):D.REPACK.rebuild_equivalent_recipe(base+b'\0'*4,recipe,base,recipe[60:])
if __name__=='__main__':unittest.main(verbosity=2)
