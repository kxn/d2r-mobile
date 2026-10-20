#!/usr/bin/env python3
"""Execute the real helper's readiness loop offline, with subprocesses denied.

The fake returns only fixed identity/tool-hash and supplied game-state records.
Successful readiness stops at the next original thread-list read: no official
capture command, fake performance result, clock/sample or device is generated.
"""
from pathlib import Path
import copy,json,runpy,shlex,sys,tempfile,unittest
from unittest.mock import patch
R=Path('/home/dl/d2r-mobile');HELPER=Path(__file__).with_name('capture-official-cpu.py')
SERIAL='10CG570J59001U1';PID='12345';PKG='com.d2rmobile.nativeport'
TOOL='b868bf7d7aaffec117c5c758b594594d71a80de35b1e5b48a10b2f2eccae239c'
class ReachedTools(Exception):pass

def state(tick,pso=10,variant=20):
 return {'valid':True,'world_input_ready':True,'area':1,'guest_heap':{'shared':True},
  'scale_3d':.60,'pool_windows':{'window_bytes':16777216},
  'game_output':{'width':2750,'height':1260},'surface_output':{'width':2750,'height':1260},
  'world_freeze':{'host_frozen':False},'runtime_extrap':{'enabled':False},
  'pipeline_prepare':{'pipelines':pso},'uniform_specialization':{'variants':variant},
  'time_ns':tick*1000000,'tick':tick,'frame_pacing':{'target_fps':60}}

class RealReadiness(unittest.TestCase):
 def exercise(self,records,expect_count,passes):
  records=list(map(copy.deepcopy,records));state_reads=[];commands=[]
  with tempfile.TemporaryDirectory(prefix='native-bulk-readiness-offline-',dir=R/'.local/logs') as name:
   out=Path(name)
   def check_output(command,**kwargs):
    commands.append(command)
    self.assertEqual(command[:3],[str(R/'.local/android-sdk/platform-tools/adb'),'-s',SERIAL])
    args=command[3:]
    if args==['shell','pidof '+PKG+' || true']:return PID+'\n'
    if args==['shell','sha256sum','/system/bin/simpleperf']:return TOOL+'  /system/bin/simpleperf\n'
    self.assertEqual(args[0],'shell');parts=shlex.split(args[1]);self.assertEqual(parts[:2],['run-as',PKG])
    if parts[2:]==['tail','-n','1','files/runtime/game-state.jsonl']:
     self.assertTrue(records,'Unexpected extra state read');row=records.pop(0);state_reads.append(row);return json.dumps(row)+'\n'
    if parts[2:4]==['sh','-c'] and 'task/*' in parts[4]:raise ReachedTools('real readiness passed; stop before official tool windows')
    raise AssertionError('Unexpected fake-device command: '+repr(command))
   def forbidden(*args,**kwargs):raise AssertionError('No process/device may run in offline readiness test')
   argv=[str(HELPER),SERIAL,PID,'1',str(out),'16777216','1']
   with patch.object(sys,'argv',argv),patch('subprocess.check_output',side_effect=check_output),patch('subprocess.run',side_effect=forbidden),patch('subprocess.Popen',side_effect=forbidden),patch('time.sleep',return_value=None):
    if passes:
     with self.assertRaises(ReachedTools):runpy.run_path(str(HELPER),run_name='__main__')
    else:
     with self.assertRaisesRegex(AssertionError,'PSO/variant readiness never stabilized'):runpy.run_path(str(HELPER),run_name='__main__')
   actual=json.loads((out/'official-cpu-capture.json').read_text())
   self.assertEqual(len(actual['pre_record_cache_states']),expect_count)
   self.assertEqual(len(list(out.glob('cache-ready-*.json'))),expect_count)
   self.assertEqual((out/'before-tools.json').exists(),passes)
   self.assertNotIn('stat_official_command',actual);self.assertNotIn('record_official_command',actual)
   self.assertNotIn('official_scheduler_command',actual);self.assertNotIn('complete',actual)
   self.assertEqual(len(state_reads),expect_count+int(passes))
   self.assertTrue(all(c[3]=='shell' for c in commands))
   return actual
 def test_four_fresh_stable_ticks_pass_actual_loop(self):
  result=self.exercise([state(n)for n in range(1,6)],4,True)
  self.assertEqual([v['time_ns']for v in result['pre_record_cache_states']],[1000000,2000000,3000000,4000000])
 def test_PSO_and_variant_changes_each_reset_actual_loop(self):
  for pso,variant in [(11,20),(10,21)]:
   with self.subTest(pso=pso,variant=variant):
    result=self.exercise([state(1),state(2)]+[state(n,pso,variant)for n in range(3,8)],6,True)
    self.assertEqual(result['pre_record_cache_states'][2],{'time_ns':3000000,'PSO':pso,'variants':variant})
 def test_continuously_changing_cache_rejects_after_30_reads(self):
  self.exercise([state(n,pso=n)for n in range(1,31)],30,False)
 def test_no_fresh_tick_cannot_advance_readiness(self):
  self.exercise([state(1)for _ in range(30)],30,False)
 def test_repeated_ticks_wait_until_four_distinct_fresh_ticks(self):
  self.exercise([state(n)for n in [1,1,2,2,3,3,4,5]],7,True)
if __name__=='__main__':unittest.main(verbosity=2)
