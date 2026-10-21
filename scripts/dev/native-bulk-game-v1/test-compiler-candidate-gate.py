#!/usr/bin/env python3
"""Host-only synthetic candidate metadata; real, pinned179 baseline evidence.

No synthetic metadata here is an actual ThinLTO artifact or device qualification.
"""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

D=Path(__file__).resolve().parent
R=Path('/home/dl/d2r-mobile')
s=importlib.util.spec_from_file_location('compiler_gate',D/'candidate_gate.py')
g=importlib.util.module_from_spec(s);s.loader.exec_module(g)
s=importlib.util.spec_from_file_location('asset_gate',D/'qualified_asset_gate.py')
a=importlib.util.module_from_spec(s);s.loader.exec_module(a)

def require(v,m):
    if not v:raise RuntimeError(m)
def read(p):return json.loads(Path(p).read_text())
def pinned(row):
    p=Path(row['path'])
    require(hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256'],'pin changed')
    return p

class Compiler(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='compiler-gate-test-');self.addCleanup(self.temp.cleanup)
        self.out=Path(self.temp.name);self.runtime='a'*40
        self.base_review=read(R/'.local/logs/cpu-pool-pin-coherent-179c82d-root-canonical-raw-review.json')
        self.can=copy.deepcopy(read(pinned(self.base_review['canonical_ready'])))
        self.raw=copy.deepcopy(read(pinned(self.base_review['raw_ready'])))
        self.manifest=copy.deepcopy(read(pinned(self.can['canonical_build_manifest'])))
        self.source={'commit':self.runtime,'base':g.POOL_RUNTIME}
        self.can['commit']=self.raw['source_commit']=self.manifest['source_revision']=self.runtime
        self.can['effective_build'].update(host_compiler_strategy='thinlto',thinlto=True,
            thinlto_backend_jobs=2,thinlto_compile_options=g.THIN_COMPILE[:],thinlto_link_options=g.THIN_LINK[:])
        self.manifest.update(host_thinlto=True,host_thinlto_backend_jobs=2,
            host_extra_compile_options=g.THIN_COMPILE[:],host_extra_link_options=g.THIN_LINK[:])
        self.review={k:copy.deepcopy(self.base_review[k]) for k in ('APK','host_ELF_SHA256','host_ELF_BuildID',
            'tool_ready','actual_device_evidence','actual_APP_preprocessed_units','raw28_hashes_verified',
            'six_APK_libraries_verified','new_fixture_symbols_absent_APP','compile_reference_OFF')}
        self.review.update(status='ROOT_REVIEWED_HOST_THINLTO_CANONICAL_AND_FRESH_RAW28',runtime=self.runtime,
            base=g.POOL_RUNTIME,compiler_only_changes=True,runtime_C_CPP_source_equal_base=True)
        self.p={'runtime_commit':self.runtime,'native_compiler_experiment':{'strategy':'thinlto','base_runtime':g.POOL_RUNTIME},
            'native_candidate_validation':{},'pool_window_bytes':1048576,'driver':{'sha256':g.DRIVER},
            'official_l1i_record':True,'performance_tools':{'reviewed':True},
            'qualified_native_asset_validation':{'root_asset_review':{'sha256':g.POOL_ASSETS}}}
    def write(self,name,data):
        path=self.out/name;path.write_text(json.dumps(data));b=path.read_bytes()
        return {'path':str(path),'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b)}
    def invoke(self):
        self.can['source_only_candidate']=self.write('source.json',self.source)
        # Root cards intentionally omit the optional bytes field.
        self.review['source_ready']={k:v for k,v in self.can['source_only_candidate'].items() if k!='bytes'}
        self.can['canonical_build_manifest']=self.write('manifest.json',self.manifest)
        for key,data in [('canonical',self.can),('raw',self.raw)]:
            self.p[key]=self.write(key+'.json',data);self.review[key+'_ready']=self.p[key]
        self.p['native_candidate_validation']['root_review']=self.write('review.json',self.review)
        return g.validate(self.p,pinned,read,require)
    def test_actual179_baseline_and_synthetic_compiler_metadata(self):
        self.assertEqual(self.invoke(),self.runtime)
        self.assertNotIn('_validated_compiler_runtime',self.p)
    def test_AA_uses_original_artifacts_without_independent_L1I_window(self):
        self.p.pop('official_l1i_record')
        self.p['official_cpu_aa'] = {'repetitions': 3, 'record_seconds': 30,
            'sample_frequency_Hz': 99, 'DWARF_bytes': 49152, 'keep_failed_debug_info': True}
        self.assertEqual(self.invoke(), self.runtime)

    def test_root_diff_and_closure_required(self):
        for key in ('compiler_only_changes','runtime_C_CPP_source_equal_base','raw28_hashes_verified',
                    'new_fixture_symbols_absent_APP','compile_reference_OFF'):
            with self.subTest(key=key):
                self.review[key]=False
                with self.assertRaises(RuntimeError):self.invoke()
                self.review[key]=True
    def test_wrong_source_base_or_runtime_refused(self):
        for key in ('base','commit'):
            old=self.source[key];self.source[key]='b'*40
            with self.assertRaises(RuntimeError):self.invoke()
            self.source[key]=old
    def test_modules_and_original_inputs_cannot_change(self):
        self.raw['28_deployment_file_hashes']['main.patched.bin']='0'*64
        with self.assertRaises(RuntimeError):self.invoke()
        self.raw=copy.deepcopy(read(pinned(self.base_review['raw_ready'])))
        self.raw['source_commit']=self.runtime;self.raw['raw_original_inputs']['main']['sha256']='0'*64
        with self.assertRaises(RuntimeError):self.invoke()
    def test_fresh_provenance_required(self):
        self.raw['revision_provenance_fresh']=False
        with self.assertRaises(RuntimeError):self.invoke()
    def test_effective_backend_optimization_and_job_limit(self):
        for target,key,value in [(self.manifest,'host_extra_link_options',['-flto=thin']),
                (self.manifest,'host_thinlto_backend_jobs',4),
                (self.can['effective_build'],'thinlto_link_options',['-flto=thin','-Ofast']),
                (self.can['effective_build'],'pool_pin_compile_reference',True)]:
            old=target[key];target[key]=value
            with self.assertRaises(RuntimeError):self.invoke()
            target[key]=old
    def test_APP_fixture_exclusions_and_six_actual_units(self):
        self.can['new_APP_fixture_exclusion']['literals_absent']=[]
        with self.assertRaises(RuntimeError):self.invoke()
    def test_exact_resources_and_strategy_remain_required(self):
        self.p['pool_window_bytes']=0
        with self.assertRaises(RuntimeError):self.invoke()
        self.p['pool_window_bytes']=1048576;self.p['native_compiler_experiment']['strategy']='arbitrary'
        with self.assertRaises(RuntimeError):self.invoke()
    def test_input_cannot_forge_candidate_gate_result_for_asset_gate(self):
        self.p['_validated_compiler_runtime']=self.runtime
        self.p['qualified_native_asset_validation']['dual_NVN_review']={}
        with self.assertRaisesRegex(RuntimeError,'bounded asset'):
            a.validate(self.p,pinned,read,require)

class HostPreparation(unittest.TestCase):
    """Synthetic source mode using the same real179 artifact prerequisites."""
    write=Compiler.write
    invoke=Compiler.invoke
    def setUp(self):
        Compiler.setUp(self)
        original=read(pinned(self.base_review['canonical_ready']))
        self.can['effective_build']=copy.deepcopy(original['effective_build'])
        self.manifest=read(pinned(original['canonical_build_manifest']))
        self.manifest['source_revision']=self.runtime
        self.p.pop('native_compiler_experiment')
        self.p['native_host_experiment']={'kind':'resource-preparation','base_runtime':g.POOL_RUNTIME}
        for key in ('compiler_only_changes','runtime_C_CPP_source_equal_base'):self.review.pop(key)
        self.review.update(status='ROOT_REVIEWED_HOST_RESOURCE_PREPARATION_CANONICAL_AND_FRESH_RAW28',
                           host_only_changes=True,game_logic_unchanged=True)
    def test_host_mode_common_provenance_passes(self):
        self.assertEqual(self.invoke(),self.runtime)
    def test_independent_host_review_required(self):
        for key in ('host_only_changes','game_logic_unchanged','raw28_hashes_verified'):
            self.review[key]=False
            with self.assertRaises(RuntimeError):self.invoke()
            self.review[key]=True
    def test_exact_kind_and_one_experiment(self):
        self.p['native_host_experiment']['kind']='arbitrary'
        with self.assertRaises(RuntimeError):self.invoke()
        self.p['native_host_experiment']['kind']='resource-preparation'
        self.p['native_compiler_experiment']={'strategy':'thinlto','base_runtime':g.POOL_RUNTIME}
        with self.assertRaises(RuntimeError):self.invoke()
    def test_original_compiler_policy_required(self):
        for target,key,value in [(self.can['effective_build'],'thinlto',True),
                                 (self.manifest,'host_thinlto',True),
                                 (self.manifest,'host_extra_link_options',['-Wl,--icf=all'])]:
            old=target.get(key);target[key]=value
            with self.assertRaises(RuntimeError):self.invoke()
            if old is None:target.pop(key)
            else:target[key]=old
    def test_raw_and_base_still_checked(self):
        self.raw['28_deployment_file_hashes']['main.patched.bin']='0'*64
        with self.assertRaises(RuntimeError):self.invoke()
    def test_no_direct_asset_runtime_approval_from_input(self):
        self.p['_validated_compiler_runtime']=self.runtime
        self.p['qualified_native_asset_validation']['dual_NVN_review']={}
        with self.assertRaises(RuntimeError):a.validate(self.p,pinned,read,require)

if __name__=='__main__':unittest.main()
