"""Negative and wire-contract tests; these do not create GPU/driver qualification."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('compatibility_rebind',HERE/'rebind.py')
M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)

class Contract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.query=json.loads((M.ROOT/'.local/logs/native-bc3-gpu-probe-v2/actual/bc3-unorm/native-resource.json').read_text())
        root=M.ROOT/'.local/build/native-original-bc3-linear-pack-v2/native-pack'
        cls.manifest=json.loads((root/'native-pack.json').read_text());cls.table=(root/'native-layouts.bin').read_bytes()
    def test_missing_actual_query_refused(self):
        with tempfile.TemporaryDirectory() as t:
            out=Path(t)/'out';r=subprocess.run(['python3',str(HERE/'rebind.py'),'--output',str(out)],capture_output=True,text=True)
            self.assertNotEqual(r.returncode,0);self.assertFalse(out.exists());self.assertIn('missing actual consumer driver query',r.stderr)
    def test_pending_never_publishes_table(self):
        with tempfile.TemporaryDirectory() as t:
            out=Path(t)/'out';subprocess.run(['python3',str(HERE/'rebind.py'),'--pending','--output',str(out)],check=True)
            self.assertFalse(json.loads((out/'pending.json').read_text())['accepted']);self.assertEqual([p.name for p in out.iterdir()],['pending.json'])
    def test_layout_device_driver_mismatch_each_refused(self):
        for key in M.UUIDS:
            q=copy.deepcopy(self.query);q[key]='01'*16
            with self.subTest(key=key),self.assertRaisesRegex(ValueError,'UUID mismatch'):M.compare_queries(self.query,q)
    def test_producer_mismatch_refused(self):
        q=copy.deepcopy(self.query);q['producer_source_sha256']='01'*32
        with self.assertRaisesRegex(ValueError,'producer source mismatch'):M.compare_queries(self.query,q)
    def test_no_query_authentication_receipt_refused(self):
        with self.assertRaisesRegex(ValueError,'missing actual consumer query authentication'):
            M.authenticate_query(Path('/missing-query'),None,self.query)
    def test_changed_source_record_refused(self):
        with tempfile.TemporaryDirectory() as t:
            bad=Path(t)/'build.json';q=json.loads((M.ROOT/'.local/build/turnip-internal-queue-sync-v1/build.json').read_text());q['source_changes'][M.POLICY]['after']='00'*32;bad.write_text(json.dumps(q))
            with self.assertRaisesRegex(ValueError,'source/build record mismatch'):M.source_transition(M.ROOT/'.local/build/turnip-native-bc3-v1/build.json',bad)
    def test_corrupt_file_digest_refused(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'file';p.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'file digest mismatch'):M.pin({'path':str(p),'sha256':'00'*32})
    def test_original_table_digest_refused(self):
        bad=bytearray(self.table);bad[-1]^=1;manifest=copy.deepcopy(self.manifest);manifest['table_sha256']=M.digest(bad)
        with self.assertRaisesRegex(ValueError,'original table digest mismatch'):M.rewrite_table(bad,manifest,M.NEW_DRIVER)
    def test_only_consumer_header_and_digest_changes(self):
        new=M.rewrite_table(self.table,self.manifest,M.NEW_DRIVER)
        self.assertEqual(new[:64],self.table[:64]);self.assertEqual(new[96:128],self.table[96:128]);self.assertEqual(new[160:],self.table[160:]);self.assertEqual(new[64:96].hex(),M.NEW_DRIVER)
        self.assertNotEqual(new[128:160],self.table[128:160])

if __name__=='__main__':unittest.main(verbosity=2)
