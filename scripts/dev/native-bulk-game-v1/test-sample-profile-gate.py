"""Synthetic candidate metadata against real qualified f75/profile inputs.
These tests are not canonical builds, real candidate receipts, or device proof.
"""
from pathlib import Path
import copy
import ast
import hashlib
import importlib.util
import json
import unittest
D=Path(__file__).resolve().parent;R=Path('/home/dl/d2r-mobile')
s=importlib.util.spec_from_file_location('original_compiler_tests',D/'test-compiler-candidate-gate.py');old=importlib.util.module_from_spec(s);s.loader.exec_module(old)
g=old.g

def row(path):
 p=Path(path);return dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bytes=p.stat().st_size)

class Sample(unittest.TestCase):
    write=old.Compiler.write
    invoke=old.Compiler.invoke
    def setUp(self):
        old.Compiler.setUp(self)
        self.base_review=old.read(old.pinned(g.SAMPLE_BASE_REVIEW))
        self.can=copy.deepcopy(old.read(old.pinned(self.base_review['canonical_ready'])))
        self.raw=copy.deepcopy(old.read(old.pinned(self.base_review['raw_ready'])))
        self.manifest=copy.deepcopy(old.read(old.pinned(self.can['canonical_build_manifest'])))
        self.source=dict(commit=self.runtime,base=g.SAMPLE_BASE)
        self.can['commit']=self.raw['source_commit']=self.manifest['source_revision']=self.runtime
        inputs=old.read(R/'.local/logs/sample-profile-f75-2tu-consumption-v1/actual-builder-inputs-verified.json')
        self.profile=inputs['profile'];self.producer=inputs['producer_provenance']
        self.manifest['host_sample_profile']=dict(inputs,compiled_profile=self.profile)
        self.manifest['external_compiler_inputs']=[inputs['profile'],inputs['producer_provenance'],*inputs['producer_files']]
        # Synthetic archive metadata only, with hashes checked against real input
        # files. This fixture is explicitly not a new canonical archive receipt.
        files={r['path']:r for r in [self.profile,*self.manifest['external_compiler_inputs']]}
        archive={'strategy':'sample-profile','files':[dict(source=r,archive=r) for r in files.values()]}
        self.can['external_compiler_input_archive']=self.write('archive.json',archive)
        opts=['-fprofile-sample-use='+self.profile['path'],*g.SAMPLE_DIAGNOSTICS]
        self.manifest.update(host_thinlto=False,host_extra_compile_options=opts,host_extra_link_options=[])
        self.can['effective_build'].update(host_compiler_strategy='sample-profile',thinlto=False,
              sample_profile_compile_options=opts,sample_profile_link_options=[])
        self.review.update({key:copy.deepcopy(self.base_review[key]) for key in ('APK','host_ELF_SHA256','host_ELF_BuildID')})
        self.review.update(status='ROOT_REVIEWED_HOST_SAMPLE_PROFILE_CANONICAL_AND_FRESH_RAW28',base=g.SAMPLE_BASE,
              sample_profile_consumption=row(R/'.local/logs/sample-profile-f75-2tu-consumption-v1/two-TU-qualification.json'))
        self.p['native_compiler_experiment']=dict(strategy='sample-profile',base_runtime=g.SAMPLE_BASE,
              sample_profile=self.profile,producer_provenance=self.producer)
        self.p['serial']='10CG570J59001U1'
        self.p['prior_actual_game_qualification']=row(R/'.local/logs/driver-proof-fingerprint-f75de42-Q735-pair-root-functional-review.json')
    def test_same_source_profile_base_and_existing_qualification(self):
        self.assertEqual(self.invoke(),self.runtime)
    def test_actual_runner_AA_block_and_unchanged_compiler_gate_both_required(self):
        tree=ast.parse((D/'run-game.py').read_text())
        node=next(n for n in ast.walk(tree) if isinstance(n,ast.If) and
                  ast.unparse(n.test)=="'official_cpu_aa' in p")
        code=compile(ast.Module(body=[node],type_ignores=[]),'<actual-runner-AA-block>','exec')
        self.p.pop('official_l1i_record')
        self.p['official_cpu_aa']={'repetitions':3,'record_seconds':30,'sample_frequency_Hz':99,'callgraph':'fp'}
        self.assertEqual(self.invoke(),self.runtime)
        exec(code,{'p':self.p,'require':old.require})
        # Mere possession of the compiler-mode key must never grant qualification.
        self.review['runtime_C_CPP_source_equal_base']=False
        with self.assertRaises(RuntimeError):self.invoke()
        self.review['runtime_C_CPP_source_equal_base']=True
        self.p['native_host_experiment']={'kind':'resource-preparation','base_runtime':g.SAMPLE_BASE}
        with self.assertRaises(RuntimeError):exec(code,{'p':self.p,'require':old.require})
        with self.assertRaises(RuntimeError):self.invoke()

    def test_unknown_strategy_fields_or_wrong_parent_fail(self):
        for key,value in [('strategy','arbitrary'),('base_runtime',g.POOL_RUNTIME),('arbitrary_flags',['-O0'])]:
            original=copy.deepcopy(self.p['native_compiler_experiment']);self.p['native_compiler_experiment'][key]=value
            with self.subTest(key=key),self.assertRaises(RuntimeError):self.invoke()
            self.p['native_compiler_experiment']=original
    def test_profile_tamper_and_stale_compiled_copy_fail(self):
        self.p['native_compiler_experiment']=dict(self.p['native_compiler_experiment'],sample_profile=dict(self.profile,sha256='0'*64))
        with self.assertRaises(RuntimeError):self.invoke()
        self.p['native_compiler_experiment']['sample_profile']=self.profile
        self.manifest['host_sample_profile']['compiled_profile']=dict(self.profile,sha256='0'*64)
        with self.assertRaises(RuntimeError):self.invoke()
    def test_no_fastmath_lto_or_link_flag_escape(self):
        for key,value in [('sample_profile_compile_options',['-ffast-math']),('sample_profile_link_options',['-flto=thin']),('thinlto',True)]:
            original=copy.deepcopy(self.can['effective_build']);self.can['effective_build'][key]=value
            with self.subTest(key=key),self.assertRaises(RuntimeError):self.invoke()
            self.can['effective_build']=original
    def test_actual_positive_samples_and_formal_APP_needed(self):
        source=old.read(old.pinned(self.review['sample_profile_consumption']))
        for key,value in [('positive_AppliedSamples',0),('formal_APP_byteequal',False)]:
            j=copy.deepcopy(source);j['TUs'][0][key]=value;self.review['sample_profile_consumption']=self.write('bad-consumption.json',j)
            with self.subTest(key=key),self.assertRaises(RuntimeError):self.invoke()
    def test_existing_base_game_qualification_required(self):
        self.p.pop('prior_actual_game_qualification')
        with self.assertRaises((RuntimeError,KeyError)):self.invoke()
    def test_original_NSOs_and_APP_exclusions_still_required(self):
        self.raw['28_deployment_file_hashes']['main.patched.bin']='0'*64
        with self.assertRaises(RuntimeError):self.invoke()
        self.raw['28_deployment_file_hashes']['main.patched.bin']=old.read(old.pinned(self.base_review['raw_ready']))['28_deployment_file_hashes']['main.patched.bin']
        self.can['new_APP_fixture_exclusion']['actual_preprocessed_closure']=[]
        with self.assertRaises(RuntimeError):self.invoke()

if __name__=='__main__':unittest.main()
