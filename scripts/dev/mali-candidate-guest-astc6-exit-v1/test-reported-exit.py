"""Run the actual thin original joystick helper with fake ADB output, host only."""
from pathlib import Path
import copy,importlib.util,json,runpy,subprocess,sys,tempfile,time
P=Path(__file__).with_name('walk-reported-exit.py');originalcheck=subprocess.check_output;originalsleep=time.sleep;originalargv=sys.argv
cases=0
try:
 for mode in ('expected','unexpected','not-ready-transition','pid-changed'):
  state0={'valid':True,'world_input_ready':True,'time_ns':1,'area':1,'x':5236,'y':6287};states=[copy.deepcopy(state0),copy.deepcopy(state0),{**state0,'time_ns':2,'area':3 if mode=='unexpected'else 2,'world_input_ready':mode!='not-ready-transition'},{**state0,'time_ns':3,'area':2}];n=0;events=[];pidchecks=0
  def fake(args,**kw):
   global n,pidchecks
   a=args[3:]
   if a[:3]==['shell','wm','size']:return 'Physical size:1080x2412\n'
   if a[:2]==['shell','pidof']:
    pidchecks+=1;return '102\n'if mode=='pid-changed'and pidchecks>2 else '101\n'
   if a[:3]==['exec-out','run-as','com.d2rmobile.nativeport']:
    v=states[min(n,len(states)-1)];n+=1;return json.dumps(v)
   if a[:4]==['shell','input','touchscreen','motionevent']:events.append(a[4]);return ''
   raise AssertionError('unexpected subprocess '+str(args))
  subprocess.check_output=fake;time.sleep=lambda _:None;sys.argv=[str(P),'5236','6277','--radius','.5','--timeout','15','--ready-timeout','3','--until-area','2','--expect-pid','101','--stop-on-area-change','--serial','10.10.10.44:34455']
  error=None
  try:runpy.run_path(str(P),run_name='__main__')
  except (SystemExit,RuntimeError)as e:error=e
  assert events.count('DOWN')==1 and events[-1]=='UP'and events.count('UP')==1
  if mode=='unexpected':assert isinstance(error,SystemExit)and error.code==2
  elif mode=='pid-changed':assert isinstance(error,RuntimeError)
  else:assert error is None
  assert n<=4;cases+=1
finally:subprocess.check_output=originalcheck;time.sleep=originalsleep;sys.argv=originalargv
print(json.dumps({'passed':True,'actual_helper_cases':cases,'original_joystick_UP_on_expected_unexpected_transition_and_owner_change':True,'no_device_access':True}))
