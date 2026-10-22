"""Actual pinned B2T build inputs, without a device or synthetic game results."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('b2t_candidate_gate', HERE / 'candidate_gate.py')
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
INPUT = Path('/home/dl/d2r-mobile/.local/logs/shared-b2t-functional-tools-v1/inputs-ROOT-v2.json')

def read(path):
    return json.loads(Path(path).read_text())

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def pinned(row):
    path = Path(row['path'])
    require(hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256'], 'input pin changed')
    return path

class Functional(unittest.TestCase):
    def config(self):
        p = read(INPUT)
        for phase in p['phases']:
            phase['composition_data'] = read(pinned(phase['composition']))
        return p

    def test_actual_artifacts_without_performance_claim(self):
        p = self.config()
        self.assertEqual(gate.validate(p, pinned, read, require), p['runtime_commit'])

    def test_functional_exception_does_not_cover_another_runtime_or_parent(self):
        p = self.config()
        for key, value in [('runtime_commit', '0' * 40),
                           ('native_host_experiment', {'kind': 'resource-preparation', 'base_runtime': '0' * 40}),
                           ('native_functional_validation', {'purpose': 'performance'})]:
            q = copy.deepcopy(p)
            q[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                gate.validate(q, pinned, read, require)

    def test_profiling_or_debugger_may_not_hide_under_functional_exception(self):
        for key in ['performance_tools', 'official_l1i_record', 'official_cpu_aa',
                    'official_gpu_tools', 'counter_snapshot', 'sustained_play',
                    'startup_allocation_capture', 'prior_actual_game_qualification',
                    'readonly_memory_snapshot']:
            p = self.config()
            p[key] = {}
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                gate.validate(p, pinned, read, require)

    def test_original_provider_and_artifact_pins_remain_required(self):
        for key in ['driver', 'canonical', 'raw']:
            p = self.config()
            p[key]['sha256'] = '0' * 64
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                gate.validate(p, pinned, read, require)

if __name__ == '__main__':
    unittest.main()
