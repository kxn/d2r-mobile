#!/usr/bin/env python3
"""Cold fixture adapter contracts; production JNI is read but never modified."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location('prepare_nvn_fixture', Path(__file__).with_name('prepare_nvn_fixture.py'))
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class EntryTests(unittest.TestCase):
    def setUp(self):
        self.original = (ROOT / 'dm/host/android/jni.c').read_text()

    def test_default_keeps_original_runtime_policy_without_expect_or_lazy_request(self):
        result = tool.native_fixture_entry(self.original)
        before, after = self.original.split('JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTests(', 1)
        self.assertTrue(result.startswith(before))
        self.assertNotIn('DM_NATIVE_RESOURCE_EXPECT_', result)
        self.assertEqual(result.count('"DM_VK_SHARED_LAZY_WINDOWS"'), self.original.count('"DM_VK_SHARED_LAZY_WINDOWS"'))
        self.assertEqual(result.count('setenv("TU_DM_HOST_IMPORT","true",1)'), 1)
        self.assertEqual(result.count('setenv("DM_SHARED_GUEST_HEAP_FIXTURE","1",1)'), 1)

    def test_positive_mode_selects_real_policy_and_asserts_it_in_same_cold_entry(self):
        result = tool.native_fixture_entry(self.original, True)
        first = result.index('JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTests(')
        body = result[first:result.index('native_resource_environment();', first)]
        for name in ('DM_NATIVE_RESOURCE_EXPECT_RESIDENT', 'DM_NATIVE_RESOURCE_EXPECT_LAZY', 'DM_VK_SHARED_LAZY_WINDOWS'):
            self.assertEqual(body.count('setenv("' + name + '","1",1)'), 1)
        self.assertEqual(body.count('vk_pool_window_bytes(65536);'), 1)
        self.assertEqual(result.count('vk_test_native_resident_provider'), self.original.count('vk_test_native_resident_provider'))
        self.assertIn('if(operation_mode_fixed || vk_loader_is_initialized())', body)
        self.assertIn('setenv("DM_SHARED_GUEST_HEAP_FIXTURE","1",1)', body)

    def test_missing_and_ambiguous_source_anchor_are_refused(self):
        for text in ('', self.original + self.original):
            with self.assertRaisesRegex(ValueError, 'anchor changed'):
                tool.native_fixture_entry(text, True)


if __name__ == '__main__':
    unittest.main()
