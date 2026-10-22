"""Explicit input/dependency policy tests; no fabricated compiler profile."""
from pathlib import Path
import hashlib
import importlib.util
import json
import tempfile
import types
import unittest
import subprocess
import shutil

D=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('profile_policy',D/'native_sample_profile.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
R=Path('/home/dl/d2r-mobile');READY=R/'.local/logs/google-autofdo-nonlbr-f75-qualification-v1/ready.json'

def pin(path):return dict(path=str(path),sha256=p.sha(path))

class Profile(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.out=Path(self.tmp.name)
        proof=json.loads(READY.read_text());self.original=proof
        self.profile=self.out/'profile.prof';shutil.copyfile(proof['sample_profile']['path'],self.profile)
        proof['sample_profile']=dict(path=str(self.profile),sha256=p.sha(self.profile),bytes=self.profile.stat().st_size)
        self.proof=self.out/'producer.json';self.proof.write_text(json.dumps(proof))
        self.args=types.SimpleNamespace(sample_profile=self.profile,sample_profile_sha256=p.sha(self.profile),sample_profile_provenance=self.proof,sample_profile_provenance_sha256=p.sha(self.proof),thinlto=False)
    def test_default_has_no_extra_input(self):
        a=types.SimpleNamespace(sample_profile=None,sample_profile_sha256=None,sample_profile_provenance=None,sample_profile_provenance_sha256=None,thinlto=False)
        self.assertIsNone(p.verify(a))
        a.thinlto=True;self.assertIsNone(p.verify(a))
    def test_real_official_producer_profile_and_closure(self):
        row=p.verify(self.args);self.assertEqual(row['profile']['sha256'],self.original['sample_profile']['sha256']);self.assertGreater(len(row['producer_files']),100)
    def test_every_parameter_required_and_no_lto_combination(self):
        for key in ('sample_profile','sample_profile_sha256','sample_profile_provenance','sample_profile_provenance_sha256'):
            a=types.SimpleNamespace(**vars(self.args));setattr(a,key,None)
            with self.subTest(key=key),self.assertRaisesRegex(ValueError,'all be explicit'):p.verify(a)
        self.args.thinlto=True
        with self.assertRaisesRegex(ValueError,'qualified separately'):p.verify(self.args)
    def test_actual_profile_and_producer_tamper_fail(self):
        for name in ('sample_profile_sha256','sample_profile_provenance_sha256'):
            a=types.SimpleNamespace(**vars(self.args));setattr(a,name,'0'*64)
            with self.subTest(name=name),self.assertRaisesRegex(ValueError,'changed'):p.verify(a)
    def test_wrong_ELF_uncompleted_or_different_output_fail(self):
        for mutate in (lambda j:j.update(completed=False),lambda j:j['ELF'].update(build_id='0'*40),lambda j:j['sample_profile'].update(sha256='0'*64)):
            j=json.loads(self.proof.read_text());mutate(j);tmp=self.out/'bad.json';tmp.write_text(json.dumps(j));a=types.SimpleNamespace(**vars(self.args));a.sample_profile_provenance=tmp;a.sample_profile_provenance_sha256=p.sha(tmp)
            with self.assertRaises(ValueError):p.verify(a)
    def test_archive_captures_compiled_profile_and_full_declared_closure(self):
        info=p.verify(self.args);manifest={'host_sample_profile':dict(info,compiled_profile=p.record(self.profile)),
              'external_compiler_inputs':[info['profile'],info['producer_provenance'],*info['producer_files']]}
        archive=p.archive_inputs(manifest,self.out/'archive');saved=json.loads(Path(archive['path']).read_text())
        self.assertGreater(len(saved['files']),100)
        self.assertTrue(all(p.sha(r['archive']['path'])==r['source']['sha256'] for r in saved['files']))
        self.assertIsNone(p.archive_inputs({},self.out/'unused'))

    def configure(self,enabled=True,extra=()):
        src=self.out/'src';src.mkdir(exist_ok=True)
        (src/'x.c').write_text('int x;\n');(src/'x.S').write_text('')
        (src/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(profile_policy C CXX ASM)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\ninclude("'+str(D.parent.parent/'dm/host/native_sample_profile.cmake')+'")\nadd_library(app OBJECT x.c x.S)\nadd_library(untouched OBJECT x.c)\ndm_native_sample_profile(app)\n')
        cmd=['cmake','-S',str(src),'-B',str(self.out/'build'),'-G','Ninja','-DCMAKE_C_COMPILER=clang-19','-DCMAKE_CXX_COMPILER=clang++-19']
        if enabled:cmd+=['-DDM_NATIVE_SAMPLE_PROFILE='+str(self.profile),'-DDM_NATIVE_SAMPLE_PROFILE_SHA256='+p.sha(self.profile)]
        return subprocess.run(cmd+list(extra),text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    def test_CMake_default_no_flags_and_enabled_only_C_target(self):
        result=self.configure(False);self.assertEqual(result.returncode,0,result.stdout)
        rows=json.loads((self.out/'build/compile_commands.json').read_text());self.assertTrue(all('-fprofile' not in r['command'] for r in rows))
        result=self.configure(True);self.assertEqual(result.returncode,0,result.stdout)
        rows=json.loads((self.out/'build/compile_commands.json').read_text())
        for row in rows:
            prof='-fprofile-sample-use=' in row['command']
            self.assertEqual(prof,'CMakeFiles/app.dir/' in row['command'] and row['file'].endswith('.c'))
        self.assertIn(str(self.profile),(self.out/'build/build.ninja').read_text())
        result=self.configure(False,['-DDM_NATIVE_SAMPLE_PROFILE=','-DDM_NATIVE_SAMPLE_PROFILE_SHA256=']);self.assertEqual(result.returncode,0,result.stdout)
        self.assertTrue(all('-fprofile' not in r['command'] for r in json.loads((self.out/'build/compile_commands.json').read_text())))
    def test_CMake_hash_tamper_and_LTO_rejected(self):
        bad=self.configure(True,['-DDM_NATIVE_SAMPLE_PROFILE_SHA256='+'0'*64]);self.assertNotEqual(bad.returncode,0);self.assertIn('SHA256 differs',bad.stdout)
        bad=self.configure(True,['-DDM_NATIVE_THINLTO=ON']);self.assertNotEqual(bad.returncode,0);self.assertIn('separate compiler experiments',bad.stdout)

if __name__=='__main__':unittest.main()
