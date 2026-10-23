#!/usr/bin/env python3
"""Actual parent cards and in-memory refusals; no child approval/device."""
import copy
import importlib.util
from pathlib import Path
import unittest
HERE=Path(__file__).resolve().parent
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v);return v
prep=load('combined_prepare',HERE/'prepare-combined-cpu.py');g=prep.g
c=load('combined_gate',HERE/'candidate_gate.py')
class Contract(unittest.TestCase):
    def setUp(self):
        self.p=prep.make_input('PARENT1',prep.R/'.local/logs/combined-cpu-unit-parent')
        self.review=g.read(prep.PARENT_REVIEW);self.canpath=g.pinned(self.p['canonical'])
        self.can=g.read(self.canpath)
    def invoke(self,canonical=None,review=None):
        def read(p):return canonical if canonical is not None and Path(p)==self.canpath else g.read(p)
        return c.reviewed_artifacts(self.p,review or self.review,g.pinned,read,g.require)
    def test_actual_parent_CPU_without_memory_scope(self):
        self.assertNotIn('native_memory_validation',self.p)
        self.assertEqual(c.validate(self.p,g.pinned,g.read,g.require),prep.PARENT)
        self.assertEqual(self.p['baseline']['sha256'],'848944b64a993eb52775aca655ad7a703f7d2ce2bceeb60b966b4143b14b2b75')
        self.assertFalse(self.p['execution_ready'])
        self.assertEqual(self.p['official_cpu_aa'],{'repetitions':3,'record_seconds':30,'sample_frequency_Hz':99,'callgraph':'fp'})
    def test_all_seven_preprocessed_hashes_checked(self):
        for i in range(7):
            can=copy.deepcopy(self.can)
            rows=can['new_APP_fixture_exclusion']['actual_preprocessed_closure']+can['additional_native_test_APP_closure']
            rows[i]['output']['sha256']='0'*64
            with self.subTest(unit=i),self.assertRaises(RuntimeError):self.invoke(can)
    def test_seven_count_names_and_fixture_absence_required(self):
        changes=[('actual_APP_preprocessed_translation_units_total',6),('additional_native_test_APP_closure',[])]
        for key,value in changes:
            can=copy.deepcopy(self.can);can[key]=value
            with self.subTest(key=key),self.assertRaises(RuntimeError):self.invoke(can)
        can=copy.deepcopy(self.can);can['new_APP_fixture_exclusion']['actual_preprocessed_closure'][0]['source']='unreviewed.c'
        with self.assertRaises(RuntimeError):self.invoke(can)
        for native in (False,True):
            can=copy.deepcopy(self.can);rows=can['additional_native_test_APP_closure']if native else can['new_APP_fixture_exclusion']['actual_preprocessed_closure']
            rows[0]['new_fixture_closure_absent']=False
            with self.assertRaises(RuntimeError):self.invoke(can)
    def test_RAW_alias_cannot_hide_failure(self):
        review=copy.deepcopy(self.review);review['raw28_hashes_verified']=False
        with self.assertRaises(RuntimeError):self.invoke(review=review)
        review.pop('raw28_hashes_verified');review.pop('raw28_files_hash_verified')
        with self.assertRaises(RuntimeError):self.invoke(review=review)
    def test_child_requires_real_artifact_review(self):
        with self.assertRaisesRegex(RuntimeError,'candidate requires actual'):
            prep.make_input('CANDIDATE1',prep.R/'.local/logs/combined-cpu-unit-child')
        with self.assertRaisesRegex(RuntimeError,'actual parent/combined'):
            prep.make_input('CANDIDATE1',prep.R/'.local/logs/combined-cpu-unit-child',prep.PARENT_REVIEW)
if __name__=='__main__':unittest.main(verbosity=2)
