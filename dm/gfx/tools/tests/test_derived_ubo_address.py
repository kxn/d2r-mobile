"""Real original VS inputs, full hash-bound closure regeneration, negative domains."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import dmsh_derived_ubo_address as D
ROOT = TOOLS.parents[2]
FROZEN = ROOT/'.local/build/adreno-shader-upstream-rewrites-v2/vk-29e56b51bce5'
KEYS = ['3deaf68fd65b5298', '54c93fa22abff9ee']

class DerivedUBO(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not FROZEN.is_dir():
            raise unittest.SkipTest('Actual frozen cache unavailable; do not invent original shader identities')
        cls.temp = tempfile.TemporaryDirectory(prefix='derived_ubo_contract_')
        cls.root = Path(cls.temp.name); cls.base = cls.root/'input'; cls.base.mkdir()
        for key in KEYS:
            for suffix in ('spv', 'meta', 'json', 'dkey', 'dspan', 'plocal.json', 'plocal'):
                shutil.copyfile(FROZEN/(key+'.'+suffix), cls.base/(key+'.'+suffix))
        manifest = json.loads((FROZEN/'_vk_manifest.json').read_text())
        manifest['expected'] = manifest['validated'] = 2
        (cls.base/'_vk_manifest.json').write_text(json.dumps(manifest))
        (cls.base/'.chain').write_text(manifest['chain']+'\n')
        (cls.base/'_vk_build.json').write_text(json.dumps([{'key': k, 'st': 'ok'} for k in KEYS]))
        cls.receipt = cls.root/'receipt.json'
        cls.receipt.write_text(json.dumps({'chain': manifest['chain'], 'validated_game_and_builtin_shaders': 2,
            'original_proofs_unchanged': True, 'artifacts': {n: D.artifact(p) for n,p in D.base.files(cls.base).items()}}))
        cls.digest = D.sha(cls.receipt)
        cls.record = D.build(cls.base, cls.receipt, cls.digest, KEYS, cls.root/'output',
                             cls.root/'result.json', expected_shaders=2)
        cls.cache = Path(cls.record['output'])
    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def copied(self):
        t = tempfile.TemporaryDirectory(); self.addCleanup(t.cleanup)
        result = Path(t.name)/self.cache.name; shutil.copytree(self.cache, result); return result
    def rewrite_ledger(self, cache, name):
        p = cache/D.RECORD; r = json.loads(p.read_text()); r['artifacts'][name] = D.artifact(cache/name)
        p.write_text(json.dumps(r))
    def test_real_modules_and_original_closures(self):
        self.assertEqual(self.record['independent_verification']['status'], 'passed-not-deployed')
        for key in KEYS:
            self.assertEqual((self.cache/(key+'.meta')).read_bytes(), (FROZEN/(key+'.meta')).read_bytes())
            self.assertNotEqual((self.cache/(key+'.spv')).read_bytes(), (FROZEN/(key+'.spv')).read_bytes())
            self.assertEqual(self.record['ubo_address_proofs'][key]['changed_shift_results'], 48)
            meta = json.loads((self.cache/(key+'.json')).read_text())
            self.assertEqual(meta['spirv_sha256'], D.sha(self.cache/(key+'.spv')))
            self.assertEqual(meta['chain'], self.record['chain'])
    def test_wrong_original_receipt_rejected(self):
        with self.assertRaisesRegex(ValueError, 'receipt identity'):
            D.verify(self.cache, '0'*64, 2)
    def test_changed_original_metadata_rejected_even_with_new_ledger(self):
        target = self.copied(); name = KEYS[0]+'.meta'; p=target/name
        p.write_bytes(p.read_bytes()+b'\0\0\0\0'); self.rewrite_ledger(target, name)
        with self.assertRaisesRegex(ValueError, 'Unrelated input artifact changed'):
            D.verify(target, self.digest, 2)
    def test_changed_candidate_rejected_even_with_new_ledger(self):
        target = self.copied(); name = KEYS[0]+'.spv'; p=target/name
        p.write_bytes((FROZEN/name).read_bytes()); self.rewrite_ledger(target, name)
        with self.assertRaisesRegex(ValueError, 'Normalization regeneration'):
            D.verify(target, self.digest, 2)
    def test_changed_original_archive_rejected_even_with_new_ledger(self):
        target = self.copied(); name = KEYS[0]+'.ubo-input'; p=target/name
        p.write_bytes(p.read_bytes()+b'\0'); self.rewrite_ledger(target, name)
        with self.assertRaisesRegex(ValueError, 'Original selected SPIR-V identity'):
            D.verify(target, self.digest, 2)
    def test_changed_original_manifest_rejected_even_with_new_ledger(self):
        target = self.copied(); name = D.BASE_MANIFEST; p=target/name
        m=json.loads(p.read_text());m['compiler_options']['pixel_local_runtime']=False
        p.write_text(json.dumps(m)); self.rewrite_ledger(target, name)
        with self.assertRaisesRegex(ValueError, 'Original manifest identity'):
            D.verify(target, self.digest, 2)
    def test_extra_file_rejected(self):
        target = self.copied(); (target/'unexpected').write_bytes(b'x')
        with self.assertRaisesRegex(ValueError, 'Undeclared derived output'):
            D.verify(target, self.digest, 2)
    def test_non_vertex_or_paired_input_rejected(self):
        t = tempfile.TemporaryDirectory(); self.addCleanup(t.cleanup)
        target = Path(t.name)/'input'; shutil.copytree(self.base, target)
        p=target/(KEYS[0]+'.json'); m=json.loads(p.read_text());m['stage']='ps';p.write_text(json.dumps(m))
        with self.assertRaisesRegex(ValueError, 'vertex set0'): D.selected_reflection(target, KEYS[0])
        shutil.copyfile(self.base/p.name, p); (target/(KEYS[0]+'.spec')).write_bytes(b'not a qualified pair')
        with self.assertRaisesRegex(ValueError, 'paired alternate'): D.selected_reflection(target, KEYS[0])
    def test_changed_closure_rejected_even_with_new_ledger(self):
        target = self.copied(); name = KEYS[0]+'.dkey'; p=target/name
        data=bytearray(p.read_bytes());data[-1]^=1;p.write_bytes(data); self.rewrite_ledger(target, name)
        with self.assertRaisesRegex(ValueError, 'closure regeneration'):
            D.verify(target, self.digest, 2)

if __name__ == '__main__': unittest.main(verbosity=2)
