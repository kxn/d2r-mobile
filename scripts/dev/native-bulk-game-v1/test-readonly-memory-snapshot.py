#!/usr/bin/env python3
"""Host-only pinned helper plan/target/result tests; no Android calls."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
D=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('memory_snapshot_runner',D/'run-game.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
def rec(p):return {'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
class Snapshot(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.helper=self.root/'fake-helper.py';self.helper.write_text("import sys\nassert sys.argv[1:3]==['12345','/tmp'] and sys.argv[-1]=='--plan'\nprint('host fake plan only')\n")
        self.config=self.root/'config.json';self.identity={'serial':'owned-test','APK_sha256':'a'*64,'ELF_sha256':'b'*64,'canonical_source_ready_sha256':'c'*64};self.config.write_text(json.dumps(self.identity))
        self.p={'serial':'owned-test','new_apk':'a'*64,'new_elf':'b'*64,'canonical':{'sha256':'c'*64},'readonly_memory_snapshot':{'helper':rec(self.helper),'config':rec(self.config)}}
    def test_absent_preserves_old_path(self):
        p={}
        with patch.object(g.subprocess,'run',side_effect=AssertionError('must not run')):self.assertIsNone(g.readonly_memory_snapshot(p))
        self.assertEqual(p,{})
    def test_pinned_fake_plan_positive(self):
        original=copy.deepcopy(self.p);g.readonly_memory_snapshot(self.p);self.assertEqual(self.p,original)
    def test_each_runtime_identity_mismatch_rejected_before_plan(self):
        for key in self.identity:
            j=dict(self.identity);j[key]='mismatch';self.config.write_text(json.dumps(j));p=copy.deepcopy(self.p);p['readonly_memory_snapshot']['config']=rec(self.config)
            with self.subTest(key=key),patch.object(g.subprocess,'run',side_effect=AssertionError('must not run')),self.assertRaisesRegex(RuntimeError,'target differs'):g.readonly_memory_snapshot(p)
    def test_helper_and_config_pin_tamper_rejected(self):
        for key in ['helper','config']:
            p=copy.deepcopy(self.p);p['readonly_memory_snapshot'][key]['sha256']='d'*64
            with self.subTest(key=key),self.assertRaisesRegex(RuntimeError,'pinned input changed'):g.readonly_memory_snapshot(p)
    def test_counter_and_unknown_fields_rejected(self):
        for p in [dict(self.p,counter_snapshot={}),dict(self.p,counter_snapshot={'ready':{}}),dict(self.p,readonly_memory_snapshot=dict(self.p['readonly_memory_snapshot'],server={}))]:
            with self.assertRaisesRegex(RuntimeError,'fields / counter conflict'):g.readonly_memory_snapshot(p)
    def test_failed_helper_plan_is_not_accepted(self):
        self.helper.write_text('raise SystemExit(3)\n');self.p['readonly_memory_snapshot']['helper']=rec(self.helper)
        with self.assertRaises(subprocess.CalledProcessError):g.readonly_memory_snapshot(self.p)
    def test_result_requires_complete_no_calls_and_cleanup(self):
        ok={'snapshot_complete':True,'inferior_calls':False,'owned_PID_TracerPid_zero_after_cleanup':True,'cleanup_errors':[]};g.readonly_memory_snapshot_result(ok)
        for key,value in [('snapshot_complete',False),('snapshot_complete',1),('inferior_calls',True),('inferior_calls',0),('owned_PID_TracerPid_zero_after_cleanup',False),('cleanup_errors',['owned helper failed'])]:
            bad=dict(ok);bad[key]=value
            with self.subTest(key=key,value=value),self.assertRaisesRegex(RuntimeError,'detach incomplete'):g.readonly_memory_snapshot_result(bad)
        for key in ok:
            bad=dict(ok);bad.pop(key)
            with self.assertRaises(RuntimeError):g.readonly_memory_snapshot_result(bad)
    def test_position_is_after_route_and_all_tools_before_meminfo(self):
        text=(D/'run-game.py').read_text();start=text.index('def phase_game');hook=text.index("        if p.get('readonly_memory_snapshot'):",start)
        self.assertLess(text.index('m.original_route(',start),hook);self.assertLess(text.index("        if p.get('counter_snapshot'):",start),hook)
        self.assertLess(hook,text.index("(out / 'official-dumpsys-meminfo.txt')",hook));self.assertNotIn('shaders',dict(g.DIRS))
if __name__=='__main__':unittest.main()
