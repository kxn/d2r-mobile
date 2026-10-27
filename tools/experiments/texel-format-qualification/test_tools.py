#!/usr/bin/env python3
"""Offline input/receipt boundaries. These tests are not GPU qualification."""
import contextlib
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

HERE=Path(__file__).resolve().parent

def load(name):
    spec=importlib.util.spec_from_file_location('texel_qualification_'+name,HERE/(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

build=load('build');runner=load('run')


def ready():
    return {'package':runner.P,'activity':runner.P+'.ProbeActivity','no_game_modules':True,
            'native_libraries':{n:'1'*64 for n in build.HOOKS|{'libtexelformatprobe.so'}},
            'source_fingerprint':'a'*64}


def output(status='PASS',mismatches=0,full=False):
    count=1<<32 if full else 4096
    rows=[{'event':'app_entry','pid':42,'uid':10334},
          {'event':'context','source_fingerprint':'a'*64,'format':'R32G32B32A32_SFLOAT','comparison_stage':'fragment'},
          {'event':'actual_driver_map','line':'rw-p /data/user/0/'+runner.P+'/files/runtime/driver/libvulkan_freedreno.so'},
          {'event':'result','status':status,'tested_texels':count,'tested_components':count*4,
           'mismatch_texels':mismatches,'mismatched_components':mismatches,'source_mismatch_texels':0,
           'coverage_errors':0,'full_domain_completed':full},
          {'event':'app_terminal','rc':0 if status=='PASS' else 3}]
    return rows


def text(rows):
    return '[loader] native log\n'+'\n'.join(json.dumps(r) for r in rows)+'\n'


class InputTests(unittest.TestCase):
    def test_root_is_inferred_and_all_fixed_sources_match(self):
        self.assertEqual(build.ROOT,runner.R)
        self.assertTrue((build.ROOT/'dm/gfx/vulkan/nvn_vk_loader.c').is_file())
        self.assertEqual(set(build.checked_shader_sources()['shaders']),{x[0]for x in build.SHADERS})

    def test_main_or_unknown_native_payload_is_rejected(self):
        for change in [{'package':'com.d2rmobile.nativeport'},{'activity':'other.Activity'},
                       {'no_game_modules':False},{'source_fingerprint':''},
                       {'native_libraries':{'libdmhost_android.so':'1'*64}}]:
            value=ready();value.update(change)
            with self.subTest(change=change),self.assertRaises(ValueError):runner.validate_ready(value)

    def test_spirv_include_has_exact_little_endian_words(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp)
            for n,_ in build.SHADERS:(folder/(n+'.spv')).write_bytes(struct.pack('<2I',0x07230203,0xaabbccdd))
            result=build.shader_include(folder)
            self.assertEqual(result.count('0x7230203u,0xaabbccddu,'),4)
            (folder/'generate.comp.spv').write_bytes(b'bad')
            with self.assertRaisesRegex(ValueError,'alignment'):build.shader_include(folder)

    def test_default_plan_never_invokes_subprocess_or_adb(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);artifact=folder/'dummy';artifact.write_bytes(b'only a dry run')
            value=ready();value.update({key:{'path':str(artifact),'sha256':hashlib.sha256(artifact.read_bytes()).hexdigest()}
                                       for key in ['APK','driver']})
            path=folder/'ready.json';path.write_text(json.dumps(value));stdout=io.StringIO()
            with patch('sys.argv',[str(HERE/'run.py'),'PLAN','--ready',str(path)]),patch.object(runner.subprocess,'run')as adb,contextlib.redirect_stdout(stdout),self.assertRaises(SystemExit)as done:
                runner.main()
            self.assertEqual(done.exception.code,0);adb.assert_not_called()
            self.assertEqual(json.loads(stdout.getvalue())['status'],'PLAN_ONLY')


class ReceiptTests(unittest.TestCase):
    def test_partial_json_does_not_become_terminal(self):
        s='{"event":"progress","completed_chunks":64}\n{"event":"app_ter'
        self.assertEqual(runner.parse_events(s),[{'event':'progress','completed_chunks':64}])
        with self.assertRaisesRegex(ValueError,'app_entry'):runner.qualify_output(s,42,ready())

    def test_raw_quick_and_full_are_distinct(self):
        self.assertTrue(runner.qualify_output(text(output()),42,ready())['comparison_passed'])
        with self.assertRaisesRegex(ValueError,'full-domain'):runner.qualify_output(text(output()),42,ready(),full=True)
        full=runner.qualify_output(text(output(full=True)),42,ready(),full=True)
        self.assertEqual(full['result']['tested_components'],17179869184)

    def test_injected_error_must_be_a_real_failure(self):
        value=runner.qualify_output(text(output('FAIL',4096)),42,ready(),inject_error=True)
        self.assertTrue(value['negative_control_passed']);self.assertFalse(value['comparison_passed'])
        with self.assertRaisesRegex(ValueError,'negative control'):runner.qualify_output(text(output()),42,ready(),inject_error=True)

    def test_quiet_mismatch_is_recorded_without_being_a_pass(self):
        value=runner.qualify_output(text(output('FAIL',4095)),42,ready())
        self.assertFalse(value['comparison_passed']);self.assertFalse(value['negative_control_passed'])

    def test_identity_and_stage_mismatch_refuse_qualification(self):
        for field,value in [('source_fingerprint','b'*64),('format','R32_SFLOAT'),('comparison_stage','compute')]:
            rows=output();rows[1][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):runner.qualify_output(text(rows),42,ready())
        for field,value in [('pid',43),('uid',2000),('pid',True)]:
            rows=output();rows[0][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):runner.qualify_output(text(rows),42,ready())

    def test_stock_map_and_ambiguous_or_missing_events_refuse(self):
        rows=output();rows[2]['line']='r-xp /vendor/lib64/hw/vulkan.adreno.so'
        with self.assertRaisesRegex(ValueError,'private Turnip'):runner.qualify_output(text(rows),42,ready())
        for idx in [0,1,3,4]:
            rows=output();rows.append(copy.deepcopy(rows[idx]))
            with self.subTest(duplicate=idx),self.assertRaisesRegex(ValueError,'ambiguous'):runner.qualify_output(text(rows),42,ready())
        rows=output();del rows[1]
        with self.assertRaisesRegex(ValueError,'context'):runner.qualify_output(text(rows),42,ready())

    def test_source_coverage_and_invalid_counter_failures_cannot_qualify(self):
        for field,value in [('coverage_errors',1),('source_mismatch_texels',1),('tested_texels',True),
                            ('tested_components',100),('mismatch_texels',-1),('mismatched_components',1)]:
            rows=output();rows[3][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):runner.qualify_output(text(rows),42,ready())


if __name__=='__main__':unittest.main()
