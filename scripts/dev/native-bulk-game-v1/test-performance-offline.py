#!/usr/bin/env python3
"""Source-only hook/original tool command and no-device guards."""
from pathlib import Path
import importlib.util,sys,unittest
from unittest.mock import patch
D=Path(__file__).resolve().parent
class Performance(unittest.TestCase):
 def test_hook_precedes_LLDB_and_uses_original_route_only(self):
  s=(D/'run-game.py').read_text();a=s.index("        m.original_route(");b=s.index("        if p.get('performance_tools'):",a);c=s.index("        if p.get('counter_snapshot'):",a)
  self.assertLess(a,b);self.assertLess(b,c)
  self.assertIn("str(p['pool_window_bytes']), '1'",s[b:c]);self.assertIn("'stable_target60_PSO_variants_all_windows'",s[b:c])
 def test_existing_official_commands_and_scope(self):
  s=(D/'capture-official-cpu.py').read_text()
  for text in ["'/system/bin/simpleperf'","'--duration','8'","'-f','99'","'dwarf,16384'","'--per-thread'","'--frame-timeline'","'--clean-atrace-print'","v['scale_3d']==.60"]:
   self.assertIn(text,s)
  self.assertNotIn('walk-to.py',s);self.assertNotIn('world_freeze\'][\'host_frozen\']=True',s)
  self.assertIn('selected is not presented/frame denominator',s)
 def test_no_device_on_module_import(self):
  spec=importlib.util.spec_from_file_location('bulk_offline_guard',D/'run-game.py');m=importlib.util.module_from_spec(spec)
  def deny(*a,**k):raise AssertionError('Unexpected subprocess on import')
  with patch('subprocess.run',deny),patch('subprocess.Popen',deny),patch('subprocess.check_output',deny):spec.loader.exec_module(m)
 def test_bad_manifest_rejected_before_any_device(self):
  spec=importlib.util.spec_from_file_location('bulk_offline_guard_hash',D/'run-game.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  def deny(*a,**k):raise AssertionError('Unexpected subprocess on bad input')
  with patch('subprocess.run',deny),patch('subprocess.Popen',deny),patch('subprocess.check_output',deny),self.assertRaisesRegex(RuntimeError,'explicit input SHA256'):
   m.proof(Path(__file__), 'bad')
if __name__=='__main__':unittest.main()
