#!/usr/bin/env python3
"""Evaluate the actual official command/window policy without device calls."""
from pathlib import Path
import ast,hashlib,json,unittest
D=Path(__file__).resolve().parent
TREE=ast.parse((D/'capture-official-cpu.py').read_text())
def expression(name):
 for n in ast.walk(TREE):
  if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name)and t.id==name for t in n.targets):
   return compile(ast.Expression(n.value),'<actual-helper-policy>','eval')
 raise AssertionError(name)
class Policy(unittest.TestCase):
 def command(self,kind):return eval(expression('command'),{'kind':kind,'pid':'123','remote':'owned.data','stat_events':{'stat':'task-clock'}})
 def test_original_cpu_clock_command_unchanged(self):
  self.assertEqual(self.command('record'),['/system/bin/simpleperf','record','--in-app','-p','123','--duration','8','-e','cpu-clock:u','-f','99','--call-graph','dwarf,16384','--post-unwind=yes','--no-callchain-joiner','--keep-failed-unwinding-result','-o','owned.data'])
 def test_refill_uses_its_own_event_period_and_stack(self):
  c=self.command('record-l1i-refill')
  self.assertEqual(c[c.index('-e')+1],'raw-l1i-cache-refill:u');self.assertEqual(c[c.index('-c')+1],'1000000');self.assertNotIn('-f',c)
  self.assertEqual(c[c.index('--call-graph')+1],'dwarf,32768');self.assertIn('--keep-failed-unwinding-result',c)
 def test_disabled_original_windows_exact_and_enabled_appends(self):
  old=('stat','record','pmu-core','pmu-memory','pmu-front-cache','pmu-front-stalls')
  self.assertEqual(eval(expression('capture_kinds'),{'refill':False}),old)
  self.assertEqual(eval(expression('capture_kinds'),{'refill':True}),old+('record-l1i-refill',))
  self.assertIn('--per-thread',self.command('stat'));self.assertNotIn('record',self.command('stat'))
 def test_proven_event_open_not_a_game_or_sample_claim(self):
  p=Path('/home/dl/d2r-mobile/.local/logs/q735-official-L1I-record-open-probe-v2/result.json');b=p.read_bytes()
  self.assertEqual(hashlib.sha256(b).hexdigest(),'b74438a5f5b66ebc914e2aec01991d4fa9fe20a80299b5a7f3d05fc3340e3808')
  r=json.loads(b);self.assertEqual(r['sampling_event_open_returncode'],0);self.assertTrue(r['PERFILE2']);self.assertEqual(r['perf_harden_before'],r['perf_harden_after'])
  self.assertIn('Samples recorded: 0.',next(c['output']for c in r['commands']if 'Samples recorded:'in c['output']))
if __name__=='__main__':unittest.main()
