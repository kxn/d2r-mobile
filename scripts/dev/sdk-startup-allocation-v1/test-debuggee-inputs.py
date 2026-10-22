"""Local exact acquired Q image qualification; no target execution or device."""
import copy
import importlib.util
from pathlib import Path
import unittest
D=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('wrapper',D/'run-debugger.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
class DebuggeeInputs(unittest.TestCase):
    def setUp(self):
        self.config={'serial':'10CG570J59001U1','debuggee_inputs':{'path':'/home/dl/d2r-mobile/.local/logs/sdk-startup-large-allocation-tools-v5-device-exe/actual-device-inputs.json','sha256':'36842fa98552992494970e0d57fc2572ffbdd01f478c111a561ae025049e5c76'}}
    def test_actual_acquired_main_and_linker_bytes(self):
        d=w.validate_debuggee(self.config)
        self.assertEqual(d['files']['/system/bin/app_process64']['build_id'],'9350e31b559c63b148098d9a0a5815a1')
        self.assertEqual(len(d['files']),2)
    def test_missing_wrong_manifest_pin_or_device_is_rejected(self):
        for mutate in (lambda c:c.pop('debuggee_inputs'),lambda c:c['debuggee_inputs'].update(sha256='0'*64),lambda c:c.update(serial='other')):
            c=copy.deepcopy(self.config);mutate(c)
            with self.assertRaises(RuntimeError):w.validate_debuggee(c)
if __name__=='__main__':unittest.main()
