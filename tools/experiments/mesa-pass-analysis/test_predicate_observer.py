"""Compile and exercise the actual observe-only C include without Vulkan/GPU."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
HERE=Path(__file__).resolve().parent

class Observer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='predicate_observer_contract_')
        cls.binary=Path(cls.temp.name)/'observer'
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            str(HERE/'predicate_observer_host.c'),'-lcrypto','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def run_mode(self,mode,setting=None):
        env=dict(os.environ);env.pop('DM_VULKAN_UNIFORM_PREDICATE_OBSERVE',None)
        env['ASAN_OPTIONS']='detect_leaks=1'
        if setting is not None:env['DM_VULKAN_UNIFORM_PREDICATE_OBSERVE']=setting
        return subprocess.run([str(self.binary),mode],env=env,text=True,capture_output=True)
    def test_no_configuration_remains_off_and_policy_is_cold(self):
        result=self.run_mode('off');self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)['rows'],[]);self.assertFalse(json.loads(result.stdout)['enabled'])
    def test_original_modules_values_pending_alias_and_bounded_tuples(self):
        result=self.run_mode('on','1');self.assertEqual(result.returncode,0,result.stderr)
        rows={r['key']:r for r in json.loads(result.stdout)['rows']};self.assertEqual(len(rows),3)
        row=rows['0123456789abcdef'];self.assertTrue(row['coherent']);self.assertEqual(row['ready'],4)
        self.assertEqual(row['base_fallback'],1);self.assertEqual(row['second_pending'],2)
        self.assertEqual(row['masks'][:4],[1,1,1,1]);self.assertEqual(sum(row['masks']),row['ready'])
        self.assertEqual(rows['1111111111111111']['ready'],1)
        self.assertEqual(rows['2222222222222222']['tuple_overflow'],1)
        self.assertEqual(sum(t['draws']for t in rows['2222222222222222']['tuples']),16)
    def test_invalid_policy_and_hybrid_conflict_are_rejected(self):
        self.assertEqual(self.run_mode('on','yes').returncode,86)
        self.assertEqual(self.run_mode('conflict','1').returncode,86)

if __name__=='__main__':unittest.main(verbosity=2)
