"""Official adb subprocess orchestration regressions, entirely host-local."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

D = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('startup_wrapper', D / 'run-debugger.py')
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)


class JDWPReadiness(unittest.TestCase):
    def run_reader(self, program, *, succeeds, timeout=.2):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            argv = [sys.executable, '-u', '-c', program]
            if succeeds:
                w.wait_jdwp_pid(argv, '12345', output, timeout=timeout)
            else:
                with self.assertRaises(RuntimeError):
                    w.wait_jdwp_pid(argv, '12345', output, timeout=timeout)
            result = json.loads((output / 'official-adb-jdwp-readiness.json').read_text())
            self.assertTrue(result['owned_reader_reaped'])
            self.assertIn('returncode_after_owned_cleanup', result)
            self.assertEqual(result.get('qualified', False), succeeds)
            self.assertEqual(result['command'], argv + ['jdwp'])
            stdout = (output / 'official-adb-jdwp-stdout.bin').read_bytes()
            stderr = (output / 'official-adb-jdwp-stderr.bin').read_bytes()
            self.assertEqual(result['stdout_bytes'], len(stdout))
            self.assertEqual(result['stderr_bytes'], len(stderr))
            return result, stdout, stderr

    def test_exact_complete_PID_on_open_stream_is_ready_and_reader_reaped(self):
        r, out, _ = self.run_reader("import os,time;os.write(1,b'888\\n123');time.sleep(.02);os.write(1,b'45\\n');time.sleep(60)", succeeds=True)
        self.assertEqual(out, b'888\n12345\n')
        self.assertTrue(r['reader_live_at_match'])
        self.assertFalse(r['stdout_EOF'])
        self.assertTrue(r['owned_reader_terminate_requested'])

    def test_PID_at_finite_output_EOF_is_also_accepted(self):
        _, out, _ = self.run_reader("import os;os.write(1,b'12345\\n')", succeeds=True)
        self.assertEqual(out, b'12345\n')

    def test_wrong_PID_and_partial_PID_EOF_are_not_ready(self):
        for value in ('123456\n', '12345', ''):
            with self.subTest(value=value):
                r, out, _ = self.run_reader('import os;os.write(1,' + repr(value.encode()) + ')', succeeds=False)
                self.assertEqual(out, value.encode())
                self.assertFalse(r['complete_PID_line_observed'])
                self.assertTrue(r['stdout_EOF'])

    def test_absent_PID_open_stream_times_out_and_reaps_only_reader(self):
        r, out, _ = self.run_reader("import os,time;os.write(1,b'987\\n');time.sleep(60)", succeeds=False)
        self.assertEqual(out, b'987\n')
        self.assertTrue(r['timed_out'])
        self.assertTrue(r['owned_reader_terminate_requested'])

    def test_client_failure_preserves_stderr_and_exit(self):
        r, _, err = self.run_reader("import os;os.write(2,b'error: device offline\\n');raise SystemExit(7)", succeeds=False)
        self.assertEqual(err, b'error: device offline\n')
        self.assertEqual(r['returncode_after_owned_cleanup'], 7)
        self.assertFalse(r['complete_PID_line_observed'])

    def test_stderr_PID_cannot_qualify_stdout(self):
        r, out, err = self.run_reader("import os;os.write(2,b'12345\\n')", succeeds=False)
        self.assertEqual(out, b'')
        self.assertEqual(err, b'12345\n')
        self.assertFalse(r['complete_PID_line_observed'])


if __name__ == '__main__':
    unittest.main()
