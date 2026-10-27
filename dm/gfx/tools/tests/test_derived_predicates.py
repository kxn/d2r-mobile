"""Real original generators on two actual immutable SPIR-V inputs; no GPU/device."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
TOOLS=Path(__file__).resolve().parents[1];sys.path.insert(0,str(TOOLS))
import dmsh_derived_predicates as D
ROOT=TOOLS.parents[2]
FROZEN=ROOT/'.local/build/adreno-shader-upstream-rewrites-v2/vk-29e56b51bce5'
KEYS=['5c1ca4e7c3e4657c','38f344adf801b153']

class Derived(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not FROZEN.is_dir():raise unittest.SkipTest('Actual frozen cache is not installed; do not synthesize a shader identity')
        cls.temp=tempfile.TemporaryDirectory(prefix='derived_predicate_contract_');cls.root=Path(cls.temp.name)
        cls.base=cls.root/'input';cls.base.mkdir()
        for key in KEYS:
            for suffix in ('spv','meta','spec','json','dkey','dspan','plocal.json','plocal'):
                shutil.copyfile(FROZEN/(key+'.'+suffix),cls.base/(key+'.'+suffix))
        manifest=json.loads((FROZEN/'_vk_manifest.json').read_text());manifest['expected']=manifest['validated']=2
        (cls.base/'_vk_manifest.json').write_text(json.dumps(manifest))
        (cls.base/'.chain').write_text(manifest['chain']+'\n')
        (cls.base/'_vk_build.json').write_text(json.dumps([{'key':key,'st':'ok'}for key in KEYS]))
        cls.receipt=cls.root/'input-receipt.json'
        cls.receipt.write_text(json.dumps({'chain':manifest['chain'],'validated_game_and_builtin_shaders':2,
            'original_proofs_unchanged':True,'artifacts':{n:D.artifact(p)for n,p in D.files(cls.base).items()}}))
        cls.digest=D.sha(cls.receipt)
        cls.record=D.build(cls.base,cls.receipt,cls.digest,KEYS,cls.root/'output',cls.root/'result.json',expected_shaders=2)
        cls.cache=Path(cls.record['output'])
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def copied(self):
        temp=tempfile.TemporaryDirectory(prefix='derived_negative_');self.addCleanup(temp.cleanup)
        target=Path(temp.name)/self.cache.name;shutil.copytree(self.cache,target);return target
    def test_real_two_shader_build_and_independent_verification(self):
        self.assertEqual(self.record['independent_verification']['status'],'passed-not-deployed')
        self.assertEqual(self.record['independent_verification']['predicate_keys_regenerated'],sorted(KEYS))
        for key in KEYS:
            for suffix in ('spv','meta','spec','json'):
                self.assertEqual((self.cache/(key+'.'+suffix)).read_bytes(),(FROZEN/(key+'.'+suffix)).read_bytes())
    def test_wrong_input_receipt_digest(self):
        with self.assertRaisesRegex(ValueError,'receipt identity'):D.validate_input(self.base,self.receipt,'0'*64,2)
    def test_changed_input_byte_or_extra_file_rejected(self):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);base=Path(temp.name)/'input';shutil.copytree(self.base,base)
        p=base/(KEYS[0]+'.spv');p.write_bytes(p.read_bytes()+b'\0')
        with self.assertRaisesRegex(ValueError,'Artifact mismatch'):D.validate_input(base,self.receipt,self.digest,2)
        shutil.copyfile(self.base/p.name,p);(base/'extra.bin').write_bytes(b'x')
        with self.assertRaisesRegex(ValueError,'file set'):D.validate_input(base,self.receipt,self.digest,2)
    def test_immutable_spec_rejected_even_if_outer_ledger_is_rewritten(self):
        target=self.copied();name=KEYS[0]+'.spec';p=target/name;p.write_bytes(p.read_bytes()+b'\0')
        r=json.loads((target/D.RECORD).read_text());r['artifacts'][name]=D.artifact(p);(target/D.RECORD).write_text(json.dumps(r))
        with self.assertRaisesRegex(ValueError,'input artifact changed'):D.verify(target,self.digest,2)
    def test_changed_predicate_rejected_even_if_outer_ledger_is_rewritten(self):
        target=self.copied();name=KEYS[0]+'.pred';p=target/name;raw=bytearray(p.read_bytes());raw[-1]^=1;p.write_bytes(raw)
        r=json.loads((target/D.RECORD).read_text());r['artifacts'][name]=D.artifact(p);(target/D.RECORD).write_text(json.dumps(r))
        with self.assertRaisesRegex(ValueError,'DPR1 regeneration'):D.verify(target,self.digest,2)
    def test_extra_output_and_wrong_source_closure_rejected(self):
        target=self.copied();(target/'extra.bin').write_bytes(b'x')
        with self.assertRaisesRegex(ValueError,'Undeclared derived output'):D.verify(target,self.digest,2)
        (target/'extra.bin').unlink();r=json.loads((target/D.RECORD).read_text());r['identity']['sources']['source_files']['dmsh_uniform_predicates.py']='0'*64;(target/D.RECORD).write_text(json.dumps(r))
        with self.assertRaisesRegex(ValueError,'source/tool identity'):D.verify(target,self.digest,2)
    def test_base_manifest_options_cannot_be_forged_to_skip_closure_checks(self):
        target=self.copied();record=json.loads((target/D.RECORD).read_text())
        record['identity']['base_manifest']['compiler_options']['pixel_local_runtime']=False
        record['chain']=D.identity_chain(record['identity']);renamed=target.with_name('vk-'+record['chain']);target.rename(renamed);target=renamed
        (target/'.chain').write_text(record['chain']+'\n')
        manifest=json.loads((target/'_vk_manifest.json').read_text());manifest['chain']=record['chain'];manifest['derived_identity']=record['identity'];(target/'_vk_manifest.json').write_text(json.dumps(manifest))
        for name in ('.chain','_vk_manifest.json'):record['artifacts'][name]=D.artifact(target/name)
        (target/D.RECORD).write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError,'verified original base manifest'):D.verify(target,self.digest,2)
    def test_unknown_or_duplicate_selected_key_rejected(self):
        for keys in [[KEYS[0],KEYS[0]],['0'*16]]:
            with self.assertRaisesRegex(ValueError,'selected input keys'):
                D.build(self.base,self.receipt,self.digest,keys,self.root/'invalid',self.root/'invalid.json',True,2)

if __name__=='__main__':unittest.main(verbosity=2)
