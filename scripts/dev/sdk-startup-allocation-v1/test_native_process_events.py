import importlib.util,json,lldb,sys,time,os
from pathlib import Path
R=Path('/home/dl/d2r-mobile');D=Path(os.environ.get('DM_SDK_EVENT_TEST_OUTPUT',str(R/'.local/logs/sdk-startup-large-allocation-tools-v6')));H=Path(__file__).resolve().parent;sys.path.insert(0,str(H));import sdk_startup_allocations as h

def run(debugger):
 results=[];debugger.HandleCommand('settings set target.disable-aslr false')
 for mode in ['accepted','unrelated','deadline']:
  row={'case':mode};target=debugger.CreateTarget(str(D/'native-conditional'));bp=target.BreakpointCreateByName('stop_here');bp.SetCondition('$rdi == 200')
  if mode=='unrelated':other=target.BreakpointCreateByName('unrelated')
  launch=lldb.SBLaunchInfo([] if mode=='accepted' else [mode]);launch.SetLaunchFlags(lldb.eLaunchFlagStopAtEntry);error=lldb.SBError();debugger.SetAsync(False);p=target.Launch(launch,error);assert error.Success(),str(error)
  listener=lldb.SBListener('actual-helper-'+mode);mask=p.GetBroadcaster().AddListener(listener,lldb.SBProcess.eBroadcastBitStateChanged);assert mask&lldb.SBProcess.eBroadcastBitStateChanged
  last=p.GetStopID();debugger.SetAsync(True);assert p.Continue().Success();diagnostics={'explicit_restarted_stops_skipped':0}
  try:
   hit=h.wait_stop_events(p,listener,last,time.monotonic()+(.2 if mode=='deadline' else 10),diagnostics)
   row.update(hit=hit,events=diagnostics)
   if hit:
    raw=h.observed_stop(p,('rdi','rip'));row['raw_before_qualification']=raw
    if mode=='accepted':
     thread=h.breakpoint_thread(p,bp);value=thread.GetFrameAtIndex(0).FindRegister('rdi').GetValueAsUnsigned();assert value==200,value;assert diagnostics['explicit_restarted_stops_skipped']>=200;row['accepted_register']=value
    else:
     try:h.breakpoint_thread(p,bp)
     except ValueError as error:row['correctly_rejected']=str(error)
     else:raise AssertionError('genuine unrelated stop was ignored')
   else:assert mode=='deadline'
   row['pass']=True
  finally:
   if p.GetState()!=lldb.eStateStopped:
    p.SendAsyncInterrupt();assert h.wait_stop(p,-1,time.monotonic()+2)
   fixture_PID=p.GetProcessID();detached=p.Detach();assert detached.Success(),str(detached);row['owned_native_detached']=True
   row['owned_listener_removed']=p.GetBroadcaster().RemoveListener(listener,lldb.SBProcess.eBroadcastBitStateChanged)
   # Only the exact host fixture that this test launched, never another process.
   executable=Path('/proc/'+str(fixture_PID)+'/exe')
   if executable.exists():
    assert executable.resolve()==D/'native-conditional';os.kill(fixture_PID,15)
   row['owned_native_cleanup_requested']=True;debugger.DeleteTarget(target)
  results.append(row)
 (D/'actual-native-event-tests.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
