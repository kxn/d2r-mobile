"""Host-local pipe orchestration, not Android or JDWP protocol emulation."""
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest
D=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('wrapper',D/'run-debugger.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
class JDBFlush(unittest.TestCase):
    def test_both_official_commands_reach_child_before_EOF(self):
        program='import sys; line=sys.stdin.readline(); print(line,flush=True); line=sys.stdin.readline(); print("SECOND:"+line,flush=True); raise SystemExit(0 if line=="exit\\n" else 2)'
        with tempfile.TemporaryFile() as log:
            child=subprocess.Popen([sys.executable,'-u','-c',program],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            try:
                r=w.release_official_jdb(child,log,timeout=2)
                self.assertTrue(r['magic_observed']);self.assertEqual(child.returncode,0)
                log.seek(0);self.assertIn(b'SECOND:exit\n',log.read())
            finally:
                if child.poll() is None:child.kill();child.wait()
    def test_missing_magic_or_client_error_does_not_qualify(self):
        for program in ('import sys;sys.stdin.readline();print("wrong",flush=True)', 'import sys;sys.stdin.readline();raise SystemExit(7)'):
            with self.subTest(program=program),tempfile.TemporaryFile() as log:
                child=subprocess.Popen([sys.executable,'-u','-c',program],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                try:
                    with self.assertRaises(RuntimeError):w.release_official_jdb(child,log,timeout=1)
                finally:
                    if child.poll() is None:child.kill();child.wait()
if __name__=='__main__':unittest.main()
