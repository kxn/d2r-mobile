"""Read-only snapshot contracts. Actual target DWARF is separately checked with LLDB."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('hook',HERE/'run-debugger.py');hook=importlib.util.module_from_spec(spec);spec.loader.exec_module(hook)

class Error:
    def __init__(self):self.bad=False
    def Fail(self):return self.bad
    def __str__(self):return 'readerror'if self.bad else 'success'

fake_lldb=types.SimpleNamespace(eStateStopped=5,SBError=Error)
with patch.dict(sys.modules,{'lldb':fake_lldb}):
    spec=importlib.util.spec_from_file_location('capture',HERE/'capture_counters.py');capture=importlib.util.module_from_spec(spec);spec.loader.exec_module(capture)

class Process:
    def __init__(self,pid=12345,state=5,short=False,bad=False):self.pid=pid;self.state=state;self.short=short;self.bad=bad;self.reads=[]
    def GetProcessID(self):return self.pid
    def GetState(self):return self.state
    def ReadMemory(self,address,size,error):
        self.reads.append((address,size));error.bad=self.bad
        return (address//8).to_bytes(8,'little')[:7 if self.short else 8]

class Guards(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.env=patch.dict(os.environ,DM_NATIVE_COUNTER_OUTPUT=self.tmp.name,DM_NATIVE_COUNTER_PID='12345',DM_NATIVE_COUNTER_ELF_SHA256='a'*64,DM_NATIVE_COUNTER_BUILD_ID='b'*40);self.env.start();self.addCleanup(self.env.stop)
    def run_snapshot(self,p,uuid='b'*40,missing=False):
        target=types.SimpleNamespace(GetProcess=lambda:p);debugger=types.SimpleNamespace(GetSelectedTarget=lambda:target)
        meta={'module_UUID':uuid,'globals':{n:{'load_address':8*(i+1)}for i,n in enumerate(capture.NAMES)},'sizeof':{'native_image_allocation':120,'native_address_range':32}}
        with patch.object(capture,'metadata',side_effect=ValueError('missing')if missing else None,return_value=meta):capture.snapshot(debugger)
        return json.loads((Path(self.tmp.name)/'native-import-counters.json').read_text())
    def test_one_stop_four_exact_reads(self):
        p=Process();r=self.run_snapshot(p)
        self.assertEqual(r['status'],'EXISTING_GLOBALS_READ_ONCE');self.assertEqual(p.reads,[(8,8),(16,8),(24,8),(32,8)]);self.assertEqual(r['bytes_read'],32)
        self.assertEqual(r['values'],dict(zip(capture.NAMES,[1,2,3,4])));self.assertEqual(r['derived_metadata_bytes'],{'native_allocation_structs':120,'native_range_active_entries':96,'native_range_capacity_entries':128})
        with self.assertRaises(ValueError):self.run_snapshot(p)
        self.assertEqual(len(p.reads),4)
    def test_wrong_pid_no_read(self):
        p=Process(pid=9);r=self.run_snapshot(p);self.assertTrue(r['errors']);self.assertFalse(p.reads)
    def test_not_stopped_no_read(self):
        p=Process(state=6);r=self.run_snapshot(p);self.assertTrue(r['errors']);self.assertFalse(p.reads)
    def test_wrong_buildid_no_read(self):
        p=Process();r=self.run_snapshot(p,uuid='c'*40);self.assertTrue(r['errors']);self.assertFalse(p.reads)
    def test_missing_metadata_no_read(self):
        p=Process();r=self.run_snapshot(p,missing=True);self.assertTrue(r['errors']);self.assertFalse(p.reads)
    def test_short_read_not_zero(self):
        p=Process(short=True);r=self.run_snapshot(p);self.assertTrue(r['errors']);self.assertNotIn('values',r);self.assertEqual(len(p.reads),1)
    def test_read_error_not_zero(self):
        p=Process(bad=True);r=self.run_snapshot(p);self.assertTrue(r['errors']);self.assertNotIn('values',r)
    def test_pid_ownership(self):
        self.assertTrue(hook.owned_pid('12345','12345'))
        for bad in ['','12345 12346','12346','12345\n']:self.assertFalse(hook.owned_pid(bad,'12345'))
    def test_module_mapping(self):
        maps='100000-101000 r--p 00000000 01:01 1 /x/libdmhost_android.so\n102000-103000 r-xp 00002000 01:01 1 /x/libdmhost_android.so'
        self.assertEqual(hook.module_slide(maps),(0x100000,'/x/libdmhost_android.so'))
        for bad in ['',maps+'\n'+maps.replace('/x/','/y/'),maps+' (deleted)']:
            with self.assertRaises(RuntimeError):hook.module_slide(bad)
    def test_no_inferior_execution_or_periodic_logging(self):
        text=(HERE/'capture_counters.py').read_text()
        for forbidden in ['EvaluateExpression','CreateValueFromExpression','WriteMemory','BreakpointCreate','Continue(','.Stop(','.Call(']:self.assertNotIn(forbidden,text)
        controller=(HERE/'run-debugger.py').read_text();self.assertNotIn("'continue'",controller);self.assertNotIn('validate_stops',controller)
        self.assertIn("'process detach'",controller)

if __name__=='__main__':unittest.main()
