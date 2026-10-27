"""Default-off compiler contract and actual original VS/no-op binary handling."""
from pathlib import Path
import os
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch
TOOLS=Path(__file__).resolve().parents[1];sys.path.insert(0,str(TOOLS))
import dmsh_vulkan as V
ROOT=TOOLS.parents[2]
FROZEN=ROOT/'.local/build/adreno-shader-upstream-rewrites-v2/vk-29e56b51bce5'

class UboCompiler(unittest.TestCase):
    def test_default_dictionary_unchanged(self):
        with patch.dict(os.environ, {}, clear=True): self.assertNotIn('ubo_address_normalize', V.compiler_options())
    def test_explicit_option_and_invalid_values(self):
        with patch.dict(os.environ, {'DMSH_VK_UBO_ADDRESS_NORMALIZE':'1'}, clear=True):
            self.assertIs(V.compiler_options()['ubo_address_normalize'], True)
        for value in ('true','yes','2','', 'false'):
            with patch.dict(os.environ, {'DMSH_VK_UBO_ADDRESS_NORMALIZE':value}, clear=True):
                with self.assertRaises(ValueError): V.compiler_options()
    def test_real_vs_matches_actual_native_compile_input(self):
        p=FROZEN/'3deaf68fd65b5298.spv'
        if not p.is_file(): self.skipTest('Original frozen input not installed')
        code, proof=V.normalize_ubo_address(p)
        reference=ROOT/'.local/logs/robustness-buffer-access-audit-20261028/3deaf-ready-v1/candidate.spv'
        self.assertEqual(code, reference.read_bytes());self.assertEqual(proof['changed_shift_results'],48)
    def test_no_match_preserves_original_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);p=root/'no-match.spv';source=root/'no-match.vert'
            source.write_text('#version 450\nvoid main(){gl_Position=vec4(0,0,0,1);}')
            subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(source),'-o',str(p)],check=True,capture_output=True)
            code, proof=V.normalize_ubo_address(p)
            self.assertEqual(proof['changed_shift_results'],0);self.assertEqual(code,p.read_bytes())
    def test_normalizer_source_in_repository(self):
        self.assertTrue(V.ubo_address_normalizer_source().is_file())
        self.assertTrue(V.ubo_address_normalizer_source().resolve().is_relative_to(ROOT.resolve()))

if __name__=='__main__':unittest.main(verbosity=2)
