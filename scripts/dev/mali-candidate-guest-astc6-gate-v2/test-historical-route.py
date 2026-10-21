"""Execute actualroute orchestration using bounded original-helper outcomes; noADB."""
from pathlib import Path
import copy,importlib.util,json,subprocess,tempfile
s=importlib.util.spec_from_file_location('m',Path(__file__).with_name('run-game.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
originalrun=m.subprocess.run;originalsnapshot=m.snapshot;cases=0
try:
 for mode in ['normal','early-area2','east-miss','east-wrong-position','gate-miss','unexpected-area','outside']:
  state={'area':1,'x':5234,'y':6282,'near_area':2,'near_x':5234,'near_y':6277};calls=[];states=[]
  if mode=='outside':state['x']=6000
  def snapshot(dev,pid,path):
   assert pid==101;states.append(copy.deepcopy(state));path.write_text(json.dumps(state));return copy.deepcopy(state)
  def run(cmd,**kwargs):
   assert cmd[cmd.index('--expect-pid')+1]=='101'and '--stop-on-area-change'in cmd and cmd[cmd.index('--until-area')+1]=='2'and cmd[cmd.index('--timeout')+1]=='15';calls.append(cmd)
   if len(calls)==1:
    assert cmd[2:4]==['5265','6288']and cmd[cmd.index('--radius')+1]=='4';state.update(x=5265,y=6288)
    if mode=='early-area2':state['area']=2
    if mode=='east-wrong-position':state.update(x=5240,y=6282)
    if mode=='unexpected-area':state['area']=3
    return subprocess.CompletedProcess(cmd,2 if mode=='east-miss'else 0)
   assert len(calls)==2 and cmd[2:4]==['5267','6275']and cmd[cmd.index('--radius')+1]=='.5';state.update(x=5267,y=6278,area=1 if mode=='gate-miss'else 2);return subprocess.CompletedProcess(cmd,2 if mode=='gate-miss'else 0)
  m.snapshot=snapshot;m.subprocess.run=run
  with tempfile.TemporaryDirectory()as td:
   error=None
   try:m.walk_reported_exit(None,101,Path(td),'10.10.10.44:34455')
   except RuntimeError as e:error=e
   if mode in ['normal','early-area2']:
    assert error is None;result=json.loads((Path(td)/'reported-exit-result.json').read_text());assert result['actual_area_transition_accepted']and len(calls)==(1 if mode=='early-area2'else 2)
   else:assert error is not None and len(calls)==(0 if mode=='outside'else 2 if mode=='gate-miss'else 1)
   cases+=1
finally:m.snapshot=originalsnapshot;m.subprocess.run=originalrun
print(json.dumps({'passed':True,'actual_route_cases':cases,'normal_and_early_area2_accepted':True,'miss_unexpected_area_wrong_coordinate_and_outside_rejected':True,'device_access':False}))
