#!/usr/bin/env python3
"""Offline exact-source orchestration tests; no ADB, GPU or guest execution."""
import argparse,ast,contextlib,copy,io,itertools,json,tempfile
from pathlib import Path
from types import SimpleNamespace
P=argparse.ArgumentParser();P.add_argument('--recovered-log',type=Path);P.add_argument('--report',type=Path);opts=P.parse_args()
source=Path(__file__).with_name('run-panvk-shared-image-gate-fixture.py');tree=ast.parse(source.read_text())
names={'read_oracle_log','parse_oracle_log','observe_pid','wait_natural_exit','stop_owned'}
functions=[n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name in names];assert len(functions)==5
loop=next(n for n in ast.walk(tree)if isinstance(n,ast.While)and any(isinstance(x,ast.Call)and isinstance(x.func,ast.Name)and x.func.id=='wait_natural_exit'for x in ast.walk(n))and 'functional-bound-reached'in ast.unparse(n))
fatal=opts.recovered_log.read_text()if opts.recovered_log else '{"test_details":"standard-owned-dirty-cycle","cpu_first":7,"GPU_copy_readback_first":0}\n[dm] FATAL Vulkan check failed\nnative fault signal=6\n'
assert 'FATAL'in fatal
class Clock:
 def __init__(self):self.n=0;self.sleeps=[]
 def monotonic(self):self.n+=1;return self.n
 def sleep(self,n):self.sleeps.append(n)
cases=[]
with tempfile.TemporaryDirectory(prefix='shared-anon-controller-test-')as td:
 def setup(logs,pids):
  state={'logs':list(logs),'pids':list(pids),'calls':[],'events':[],'snapshots':[]};clock=Clock()
  phase={'name':'owned-cell','stage':'launch','expected_test':'shared-image-NC-prewrite-gate'}
  def process(unused):
   state['events'].append('pid');v=state['pids'][0]
   if len(state['pids'])>1:state['pids'].pop(0)
   return v
  def ad(*args,**unused):
   state['calls'].append(args)
   if args[:3]==('shell','am','force-stop'):state['pids']=[''];return ''
   assert args[:3]==('exec-out','run-as','owned.test'),args
   state['events'].append('log');v=state['logs'][0]
   if len(state['logs'])>1:state['logs'].pop(0)
   return v
  g={'json':json,'time':clock,'a':SimpleNamespace(output=Path(td)),'pkg':'owned.test','process':process,'ad':ad,'first_pid':None,'phase':phase,'active_phase':phase,'result':{},'deadline':1000,'exact_fixture':lambda:True}
  g['save']=lambda:state['snapshots'].append(copy.deepcopy(phase))
  exec(compile(ast.Module(body=functions,type_ignores=[]),str(source),'exec'),g)
  return g,state,phase,clock
 def poll(g):exec(compile(ast.Module(body=[loop],type_ignores=[]),str(source),'exec'),g)
 def no_force(s):assert not any(c[:3]==('shell','am','force-stop')for c in s['calls'])
 # Real recovered FATAL must be retained before initial crash/fork PID ambiguity.
 g,s,phase,clock=setup([fatal],['111 222','222','']);poll(g)
 assert phase['stage']=='native-assertion-failed'and g['first_pid']is None
 assert phase['pid_raw']==['111 222','222','']and phase['natural_exit']['all_PIDs_absent']
 assert s['events'][:2]==['log','pid'];no_force(s)
 observed=[r for r in phase['rows']if r.get('test_details')=='standard-owned-dirty-cycle']
 assert observed[0]['cpu_first']==7 and observed[0]['GPU_copy_readback_first']==0
 cases.append('recovered-private-FATAL-before-multiPID-retained/no-adoption/no-force')
 # Already claimed first PID is never replaced by a crash child PID.
 g,s,phase,clock=setup(['',fatal],['101','101 202','202','']);poll(g)
 assert g['first_pid']=='101'and phase['first_pid']==101 and phase['ownership_ambiguous']
 assert phase['stage']=='native-assertion-failed';no_force(s)
 cases.append('firstPID-preserved-through-crash-child/natural-exit-only')
 # A final row without PID cannot rescue ambiguous/replacement ownership.
 passed='{"test":"shared-image-NC-prewrite-gate","result":"passed"}\n{"shared_image_app_terminal":true,"rc":0}\n'
 for logs,pids in [([passed],['101 202','202','']),(['',passed],['101','202',''])]:
  g,s,phase,clock=setup(logs,pids);poll(g)
  assert phase['stage']=='ownership-failed'and phase['terminal_after_unowned_wait']=='oracle-passed'
  assert phase['untrusted_final']['result']=='passed'and 'final'not in phase
  assert phase['natural_exit']['all_PIDs_absent'];no_force(s)
 cases.append('ambiguous-or-replacement-plus-pass-rejected/untrusted-row-retained/no-force')
 # Unknown/replacement processes that do not exit fail boundedly, untouched.
 for logs,pids in [(['',fatal],['101','202']),([fatal],['303'])]:
  g,s,phase,clock=setup(logs,pids)
  try:poll(g);raise RuntimeError('live ambiguous process accepted')
  except TimeoutError:pass
  assert phase['natural_exit']['timed_out'];no_force(s)
  before=len(clock.sleeps)
  try:g['stop_owned'](phase);raise RuntimeError('cleanup accepted live PID')
  except TimeoutError:pass
  assert len(clock.sleeps)==before;no_force(s)
 cases.append('live-crash/replacement-bounded-timeout/no-force/no-repeated-wait')
 # Normal exact owned process remains eligible for the original explicit stop.
 normal=passed
 g,s,phase,clock=setup([normal],['101','101']);poll(g);assert phase['stage']=='oracle-passed'
 g['stop_owned'](phase);assert any(c[:3]==('shell','am','force-stop')for c in s['calls'])
 cases.append('normal-single-owned-PID-original-stop')
 # Malformed raw PID text is saved and never adopted.
 g,s,phase,clock=setup([''],['101 invalid','']);poll(g)
 assert phase['pid_raw'][0]=='101 invalid'and g['first_pid']is None
 assert phase['stage']=='ownership-failed';no_force(s)
 cases.append('invalid-raw-PID-durable/no-adoption')
 # A final JSON without any observed owner PID is also untrusted.
 g,s,phase,clock=setup([passed],['']);poll(g)
 assert phase['stage']=='ownership-failed' and phase['untrusted_final']['result']=='passed'
 assert g['first_pid'] is None;no_force(s)
 cases.append('final-without-ever-observed-owner-rejected')
# No wrapper terminal yet: a producer final cannot trigger premature stop.
g,s,phase,clock=setup(['{"test":"shared-image-NC-prewrite-gate","result":"passed"}\n'],['101'])
rows,stage,final=g['parse_oracle_log'](s['logs'][0]);assert stage is None and final is None
# Unsupported hardware is a real non-pass, not a software/coherent fallback.
rows,stage,final=g['parse_oracle_log']('{"test":"shared-image-NC-prewrite-gate","result":"unsupported"}\n')
assert stage=='oracle-unsupported' and final['result']=='unsupported'
cases.append('wrapper-return-required/unsupported-not-passed')
mode_loop=next(n for n in ast.walk(tree) if isinstance(n,ast.For) and isinstance(n.target,ast.Tuple) and [x.id for x in n.target.elts]==['kind','test_name'])
assert len(mode_loop.iter.elts)==1 and 'a.mode' in ast.unparse(mode_loop.iter)
args=next(n for n in ast.walk(mode_loop) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='args' for t in n.targets))
text=ast.unparse(args.value)
for exact in ["'shared_image_test'", "'vulkan_provider'", "'panvk'", "'run_game'", "'false'", "'run_vulkan_tests'", "'validate'", "'vulkan_test_validation'"]:assert exact in text
cases.append('one-cold-mode/no-retry/no-VVL/game/no-surface-preset')
# Verify inherited handlers route interruption into the single try/finally.
handler=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='interrupted')
g={};exec(compile(ast.Module(body=[handler],type_ignores=[]),str(source),'exec'),g)
for sig in (2,15):
 try:g['interrupted'](sig,None);raise RuntimeError('signal did not raise')
 except KeyboardInterrupt:pass
cases.append('SIGINT-SIGTERM-raise-through-outer-finally')
result={'status':'passed','source':str(source),'cases':cases,'reused_controller_source':'shared-anon-controller-ownership-v3 / 5c185ca','scope':'Exact parsed production-controller helpers/poll loop under local mocks; no ADB/device or performance measurements. Failures preserved, no PID adoption or unknown force-stop.'}
if opts.report:opts.report.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
