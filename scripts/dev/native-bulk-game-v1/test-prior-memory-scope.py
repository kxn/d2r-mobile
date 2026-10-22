#!/usr/bin/env python3
"""Offline exact-f75 prior-game gate tests; mutated receipts are temp-only negatives."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
D=Path(__file__).resolve().parent;R=Path('/home/dl/d2r-mobile')
s=importlib.util.spec_from_file_location('prior_gate',D/'candidate_gate.py');g=importlib.util.module_from_spec(s);s.loader.exec_module(g)
def require(ok,message):
    if not ok:raise RuntimeError(message)
def read(p):return json.loads(Path(p).read_text())
def pin(row):
    p=Path(row['path']);require(hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256'],'pin changed');return p
class PriorMemory(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.out=Path(self.temp.name)
        self.prior=read(R/'.local/logs/driver-proof-fingerprint-f75de42-Q735-pair-root-functional-review.json')
        self.inputs=[read(pin(r['actual_inputs']))for r in self.prior['rounds']]
        self.results=[read(pin(r['result']))for r in self.prior['rounds']]
        self.saves=[read(pin(r['fresh_save']))for r in self.prior['rounds']]
        self.outer=read(pin(self.prior['outer_publication_and_restore']))
        self.p=copy.deepcopy(self.inputs[0])
        for key in ('official_l1i_record','official_cpu_aa','performance_tools'):self.p.pop(key,None)
        # The separate runner proof tests execute pinned fake--plan and all4
        # config identities. Here the control-flow argument represents that
        # completed proof; no input JSON marker can replace it.
        self.p['readonly_memory_snapshot']={'helper':{'path':'not-executed-by-gate'},'config':{}}
    def write(self,name,data):
        p=self.out/name;p.write_text(json.dumps(data));return {'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
    def invoke(self,validated=True,with_prior=True):
        for n,row in enumerate(self.prior['rounds']):
            row['actual_inputs']=self.write(f'input{n}.json',self.inputs[n]);self.results[n]['inputs_sha256']=row['actual_inputs']['sha256']
            row['result']=self.write(f'result{n}.json',self.results[n]);row['fresh_save']=self.write(f'save{n}.json',self.saves[n])
        self.prior['outer_publication_and_restore']=self.write('outer.json',self.outer)
        if with_prior:self.p['prior_actual_game_qualification']=self.write('prior.json',self.prior)
        return g.validate(self.p,pin,read,require,readonly_memory_snapshot_validated=validated)
    def test_actual_f75_pair_all_other_original_gates_pass(self):
        self.assertEqual(self.invoke(),self.p['runtime_commit'])
    def test_missing_prior_or_unvalidated_snapshot_requires_cpu(self):
        with self.assertRaisesRegex(RuntimeError,'official tools'):self.invoke(with_prior=False)
        for validated in (False,1,None):
            with self.assertRaisesRegex(RuntimeError,'validated read-only'):self.invoke(validated=validated)
        self.p.pop('readonly_memory_snapshot')
        with self.assertRaisesRegex(RuntimeError,'validated read-only'):self.invoke()
    def test_input_marker_cannot_forge_helper_validation(self):
        self.p['_readonly_memory_snapshot_validated']=True
        with self.assertRaisesRegex(RuntimeError,'validated read-only'):self.invoke(validated=False)
    def test_unknown_card_or_new_runtime_refused(self):
        self.prior['status']='ROOT_REVIEWED_UNKNOWN'
        with self.assertRaisesRegex(RuntimeError,'status/runtime'):self.invoke()
        self.setUp();self.p['runtime_commit']=self.prior['runtime']='a'*40
        with self.assertRaisesRegex(RuntimeError,'status/runtime'):self.invoke()
    def test_wrong_canonical_raw_runtime_and_root_review_refused(self):
        for key in ('canonical','raw'):
            old=copy.deepcopy(self.inputs[0][key]);self.inputs[0][key]['sha256']='0'*64
            with self.subTest(key=key),self.assertRaises(RuntimeError):self.invoke()
            self.inputs[0][key]=old
        self.inputs[0]['runtime_commit']='a'*40
        with self.assertRaisesRegex(RuntimeError,'inputs differ'):self.invoke()
        self.inputs[0]['runtime_commit']=self.p['runtime_commit']
        old=copy.deepcopy(self.inputs[1]['native_candidate_validation']['root_review'])
        self.inputs[1]['native_candidate_validation']['root_review']['sha256']='0'*64
        with self.assertRaisesRegex(RuntimeError,'inputs differ'):self.invoke()
        self.inputs[1]['native_candidate_validation']['root_review']=old
        self.prior['source_and_original_NSO_qualification']['sha256']='0'*64
        with self.assertRaisesRegex(RuntimeError,'source/raw review'):self.invoke()
    def test_cleanup_restore_and_actual_execution_required_for_both_rounds(self):
        for n in (0,1):
            for key,value in [('cleanup_errors',['failed']),('cleanup_complete',False),('original_restored',False)]:
                old=self.results[n][key];self.results[n][key]=value
                with self.subTest(n=n,key=key),self.assertRaisesRegex(RuntimeError,'restore/cleanup'):self.invoke()
                self.results[n][key]=old
            self.results[n]['phases'][0]['game_modules_executed']=False
            with self.assertRaisesRegex(RuntimeError,'execution/save2'):self.invoke()
            self.results[n]['phases'][0]['game_modules_executed']=True
    def test_save2_and_outer_restore_required(self):
        self.saves[1]['save_commits']=1
        with self.assertRaisesRegex(RuntimeError,'execution/save2'):self.invoke()
        self.saves[1]['save_commits']=2;self.outer['stage_cleanup_complete']=False
        with self.assertRaisesRegex(RuntimeError,'outer shader restore'):self.invoke()
    def test_compiler_only_still_requires_official_cpu(self):
        self.p.pop('native_host_experiment');self.p['native_compiler_experiment']={'strategy':'thinlto','base_runtime':g.POOL_RUNTIME}
        with self.assertRaisesRegex(RuntimeError,'official tools'):self.invoke()
    def test_original_driver_resource_and_window_gates_not_relaxed(self):
        old=self.p['pool_window_bytes'];self.p['pool_window_bytes']=0
        with self.assertRaisesRegex(RuntimeError,'f830/1MiB/full2838'):self.invoke()
        self.p['pool_window_bytes']=old;self.p['driver']['sha256']='0'*64
        with self.assertRaisesRegex(RuntimeError,'f830/1MiB/full2838'):self.invoke()
if __name__=='__main__':unittest.main()
