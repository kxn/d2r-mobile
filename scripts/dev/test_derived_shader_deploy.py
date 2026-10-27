"""Derived-cache routing rejects before Android operations; no device required."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT=Path(__file__).with_name('deploy-android-shaders.py')
spec=importlib.util.spec_from_file_location('derived_shader_deployment',SCRIPT);D=importlib.util.module_from_spec(spec);spec.loader.exec_module(D)
import dmsh_derived_verify as V

class Deploy(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.cache=self.root/'cache';self.cache.mkdir();self.report=self.root/'report.json'
        self.base=[str(SCRIPT),'--cache',str(self.cache),'--report',str(self.report)]
    def main(self,extra):
        with patch.object(sys,'argv',self.base+extra):D.main()
    def test_missing_explicit_receipt_sha_rejects_before_tools_or_adb(self):
        with patch.object(D.subprocess,'run',side_effect=AssertionError('unexpected tool/ADB')):
            with self.assertRaises(SystemExit):self.main(['--verified-cache-derived','--deploy'])
    def test_derived_sha_without_explicit_mode_cannot_bypass_legacy(self):
        with patch.object(D.subprocess,'run',side_effect=AssertionError('unexpected tool/ADB')):
            with self.assertRaises(SystemExit):self.main(['--derived-input-receipt-sha256','0'*64])
    def test_failed_independent_verification_cannot_install(self):
        with patch.object(V,'verify',side_effect=ValueError('actual input/payload mismatch')),patch.object(D.subprocess,'run',side_effect=AssertionError('ADB before validation')):
            with self.assertRaisesRegex(ValueError,'payload mismatch'):
                self.main(['--verified-cache-derived','--derived-input-receipt-sha256','0'*64,'--deploy'])
        self.assertFalse(self.report.exists())
    def test_explicit_verified_stage_includes_self_receipt_and_never_deploys_by_default(self):
        (self.cache/'_derived_build.json').write_text(json.dumps({'artifacts':{}}))
        result={'status':'passed-not-deployed','chain':'123456789abc','complete_shaders':6931}
        with patch.object(V,'verify',return_value=result)as verify,patch.object(D,'deploy_verified_artifacts',side_effect=AssertionError('not authorized by CLI')):
            self.main(['--verified-cache-derived','--derived-input-receipt-sha256','0'*64])
        verify.assert_called_once_with(self.cache.resolve(),'0'*64)
        receipt=json.loads(self.report.read_text());self.assertFalse(receipt['deployed'])
        self.assertIn('_derived_build.json',receipt['artifacts']);self.assertTrue(receipt['verified_cache_derived'])
    def test_registry_rejects_unknown_stage_instead_of_importing_arbitrary_code(self):
        (self.cache/'_derived_build.json').write_text(json.dumps({'schema':'untrusted.module'}))
        with patch.object(V.importlib,'import_module',side_effect=AssertionError('arbitrary import')):
            with self.assertRaisesRegex(ValueError,'Unknown'):V.verify(self.cache,'0'*64)

if __name__=='__main__':unittest.main(verbosity=2)
