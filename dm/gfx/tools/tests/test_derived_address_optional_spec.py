"""Real paired/unpaired FS contracts; no invented shader identities or GPU use."""
from pathlib import Path
import copy,json,shutil,struct,sys,tempfile,unittest

TOOLS=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(TOOLS))
import dmsh_derived_address_combined as D

ROOT=TOOLS.parents[2]
BASE=ROOT/'.local/build/adreno-shader-upstream-rewrites-v2/vk-29e56b51bce5'
VS='3deaf68fd65b5298'
PAIRED='5c1ca4e7c3e4657c'
UNPAIRED='60f3906f2280721c'
KEYS=[VS,PAIRED,UNPAIRED]


class OptionalFragmentRecipe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BASE.is_dir():raise unittest.SkipTest('Actual original cache unavailable')
        cls.tmp=tempfile.TemporaryDirectory();cls.root=Path(cls.tmp.name)
        cls.input=cls.root/'input';cls.input.mkdir()
        for key in KEYS:
            for suffix in D.COMMON+('meta',)+(('spec',)if key==PAIRED else()):
                shutil.copyfile(BASE/(key+'.'+suffix),cls.input/(key+'.'+suffix))
        manifest=json.loads((BASE/'_vk_manifest.json').read_text())
        manifest['expected']=manifest['validated']=len(KEYS)
        (cls.input/'_vk_manifest.json').write_text(json.dumps(manifest))
        (cls.input/'.chain').write_text(manifest['chain']+'\n')
        (cls.input/'_vk_build.json').write_text(json.dumps([{'key':k,'st':'ok'}for k in KEYS]))
        cls.receipt=cls.root/'receipt.json'
        cls.receipt.write_text(json.dumps({'chain':manifest['chain'],
            'validated_game_and_builtin_shaders':len(KEYS),'original_proofs_unchanged':True,
            'artifacts':{n:D.artifact(p)for n,p in D.B.files(cls.input).items()}}))
        cls.digest=D.sha(cls.receipt)
        cls.record=D.build(cls.input,cls.receipt,cls.digest,KEYS,cls.root/'out',
                           cls.root/'report.json',expected_shaders=len(KEYS))
        cls.output=Path(cls.record['output'])

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    def clone(self,source):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        dest=Path(tmp.name)/source.name;shutil.copytree(source,dest);return dest

    def change_meta(self,source,key,proof):
        path=source/(key+'.json');meta=json.loads(path.read_text())
        if proof is ...:meta.pop('uniform_specialization',None)
        else:meta['uniform_specialization']=proof
        path.write_text(json.dumps(meta))

    def rewrite_artifact_ledger(self,source,name):
        path=source/D.RECORD;record=json.loads(path.read_text())
        record['artifacts'][name]=D.artifact(source/name);path.write_text(json.dumps(record))

    def test_real_paired_and_unpaired_build_and_verify(self):
        self.assertEqual(self.record['independent_verification']['status'],'passed-not-deployed')
        key=UNPAIRED
        self.assertEqual(set(self.record['address_proofs'][key]),{'base'})
        for suffix in ('spec','address-input-spec'):
            self.assertFalse((self.output/(key+'.'+suffix)).exists())
            self.assertNotIn(key+'.'+suffix,self.record['artifacts'])
        before=json.loads((BASE/(key+'.json')).read_text())['uniform_specialization']
        after=json.loads((self.output/(key+'.json')).read_text())['uniform_specialization']
        self.assertEqual(before,after)
        self.assertEqual(after,{'skipped':'branch region below threshold','weight':23})
        for k in KEYS:self.assertEqual((self.output/(k+'.meta')).read_bytes(),(BASE/(k+'.meta')).read_bytes())
        original=(BASE/(PAIRED+'.spec')).read_bytes();derived=(self.output/(PAIRED+'.spec')).read_bytes()
        self.assertEqual(struct.unpack_from('<7I',original)[:6],struct.unpack_from('<7I',derived)[:6])
        self.assertEqual(self.record['address_proofs'][PAIRED]['specialized_payload']['changed_shift_results'],23)

    def test_no_specialization_declaration_is_also_accepted(self):
        source=self.clone(self.input);self.change_meta(source,UNPAIRED,...)
        self.assertEqual(D.selected(source,UNPAIRED)['stage'],'ps')

    def test_missing_declared_pair_is_still_rejected(self):
        source=self.clone(self.input);(source/(PAIRED+'.spec')).unlink()
        with self.assertRaisesRegex(ValueError,'Fragment requires'):D.selected(source,PAIRED)

    def test_unpaired_residual_or_malformed_metadata_is_rejected(self):
        original=json.loads((self.input/(UNPAIRED+'.json')).read_text())['uniform_specialization']
        proofs=[None,{}, {'skipped':''},{'skipped':'reason','weight':True},
                {'skipped':'reason','weight':-1}]
        for field,value in [('binding',1),('reg',4),('offset',0),('spec_id',0),
                            ('base_sha256','0'*64),('specialization_sha256','0'*64)]:
            item=copy.deepcopy(original);item[field]=value;proofs.append(item)
        for proof in proofs:
            with self.subTest(proof=proof):
                source=self.clone(self.input);self.change_meta(source,UNPAIRED,proof)
                with self.assertRaisesRegex(ValueError,'residual or malformed'):D.selected(source,UNPAIRED)

    def test_unpaired_cannot_acquire_an_undeclared_original_recipe(self):
        source=self.clone(self.input)
        shutil.copyfile(source/(PAIRED+'.spec'),source/(UNPAIRED+'.spec'))
        with self.assertRaisesRegex(ValueError,'Fragment requires'):D.selected(source,UNPAIRED)

    def test_derived_unpaired_recipe_injection_rejected_even_with_ledger(self):
        output=self.clone(self.output);name=UNPAIRED+'.spec'
        shutil.copyfile(output/(PAIRED+'.spec'),output/name);self.rewrite_artifact_ledger(output,name)
        with self.assertRaisesRegex(ValueError,'mutation domain'):D.verify(output,self.digest,len(KEYS))

    def test_unpaired_driver_closures_still_require_exact_regeneration(self):
        for suffix in ('dkey','dspan'):
            with self.subTest(suffix=suffix):
                output=self.clone(self.output);name=UNPAIRED+'.'+suffix;path=output/name
                value=bytearray(path.read_bytes());value[-1]^=1;path.write_bytes(value)
                self.rewrite_artifact_ledger(output,name)
                with self.assertRaisesRegex(ValueError,'closure regeneration'):D.verify(output,self.digest,len(KEYS))

    def test_unpaired_does_not_allow_other_alternative_schemas(self):
        for suffix in ('pred','tssbo'):
            with self.subTest(suffix=suffix):
                source=self.clone(self.input);(source/(UNPAIRED+'.'+suffix)).write_bytes(b'not a declared alternative')
                with self.assertRaisesRegex(ValueError,'outside this stage'):D.selected(source,UNPAIRED)


if __name__=='__main__':unittest.main(verbosity=2)
