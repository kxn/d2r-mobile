"""Offline metadata/control guard tests; no ADB, process, or inferior."""
from pathlib import Path
import ast
import importlib.util
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('refusal_hook', HERE / 'run-debugger.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class Guards(unittest.TestCase):
    def test_exact_first_pid(self):
        self.assertTrue(m.owned_pid('1234', '1234'))
        for value in ['', '1234 5678', '5678']:
            self.assertFalse(m.owned_pid(value, '1234'))

    def test_absent_or_incomplete_capture_not_proven(self):
        for data in [{}, {'events': [{}]}, {'events': [{}], 'errors': ['unavailable']}]:
            self.assertNotEqual(m.validate_stops(data)['status'], 'actual_ASTC_copy_refusal_metadata_captured')

    def test_one_bounded_complete_capture(self):
        row = {name: {'field': {'unsigned': 0}} for name in ['parameters', 'source', 'destination', 'observation']}
        row.update(cell_history={'complete': True}, alias_scan={'complete': True}, pools=[])
        self.assertEqual(m.validate_stops({'events': [row]})['status'], 'actual_ASTC_copy_refusal_metadata_captured')
        row['alias_scan']['complete'] = False
        self.assertNotEqual(m.validate_stops({'events': [row]})['status'], 'actual_ASTC_copy_refusal_metadata_captured')

    def test_no_inferior_expressions_or_calls(self):
        for name in ['capture_astc.py', 'run-debugger.py']:
            tree = ast.parse((HERE / name).read_text())
            for node in ast.walk(tree):
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
                    self.assertNotIn(node.func.attr, ['EvaluateExpression', 'CallFunction', 'WriteMemory'])


if __name__ == '__main__':
    unittest.main()
