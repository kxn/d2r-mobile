"""Minimal host-only policy and timeout/detach controls; never device evidence."""
from pathlib import Path
import copy,importlib.util,json,sys,tempfile,types,unittest
from unittest.mock import patch
D=Path(__file__).resolve().parent;R=Path('/home/dl/d2r-mobile')
s=importlib.util.spec_from_file_location('observation',D/'official_api_frame.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Tests(unittest.TestCase):
 def test_exact_existing_artifact_and_mutually_exclusive_tools(self):
  p=json.loads((R/'.local/build/wait-identity-tools-v1/inputs.json').read_text());p['new_elf']=m.WAIT_ELF_SHA
  def pinned(row):q=Path(row['path']);self.assertEqual(m.sha(q),row['sha256']);return q
  self.assertEqual(m.validate(p,pinned)['BuildID'],m.WAIT_BID)
  for key in ['official_api_frame','performance_tools','official_cpu_aa','official_gpu_tools','counter_snapshot','sustained_play','official_memory_map']:
   q=copy.deepcopy(p);q[key]=True
   with self.assertRaisesRegex(RuntimeError,'separate'):m.validate(q,pinned)
  for field in ['runtime_commit','new_elf']:
   q=copy.deepcopy(p);q[field]='bad'
   with self.assertRaisesRegex(RuntimeError,'exact root-reviewed'):m.validate(q,pinned)
  for timeout in [0,True,91]:
   q=copy.deepcopy(p);q['official_wait_identity']['timeout_seconds']=timeout
   with self.assertRaisesRegex(RuntimeError,'bounded'):m.validate(q,pinned)
 def test_timeout_and_unexpected_stop_detach(self):
  # Fake official process-event API only tests orchestration; no fake metadata
  # is passed off as a real wait identity or a live debugger qualification.
  for unexpected in [False,True]:
   p=types.SimpleNamespace(state=5,stopid=1,detached=False)
   def cont():p.state=5 if unexpected else 6;p.stopid=2;return ok
   def stop():p.state=5;return ok
   def detach():p.detached=True;p.state=9;return ok
   ok=types.SimpleNamespace(Success=lambda:True)
   p.IsValid=lambda:True;p.GetState=lambda:p.state;p.GetProcessID=lambda:123
   p.GetStopID=lambda:p.stopid;p.Continue=cont;p.Stop=stop;p.Detach=detach
   p.GetNumThreads=lambda:1;p.GetThreadAtIndex=lambda _:types.SimpleNamespace(GetStopReason=lambda:4)
   mod=types.SimpleNamespace(IsValid=lambda:True,GetUUIDString=lambda:m.WAIT_BID,ResolveFileAddress=lambda a:a)
   bp=types.SimpleNamespace(SetOneShot=lambda v:None,IsValid=lambda:True,GetNumLocations=lambda:1)
   p.GetBroadcaster=lambda:None
   t=types.SimpleNamespace(GetProcess=lambda:p,FindModule=lambda _:mod,BreakpointCreateBySBAddress=lambda a:bp)
   dbg=types.SimpleNamespace(GetSelectedTarget=lambda:t,SetAsync=lambda x:None)
   listener=types.SimpleNamespace(StartListeningForEvents=lambda *a:None,WaitForEvent=lambda *a:False)
   fake=types.SimpleNamespace(eStateStopped=5,eStateExited=10,eStateDetached=9,eStateCrashed=8,eStopReasonBreakpoint=3,SBFileSpec=lambda s:s,SBListener=lambda s:listener,SBProcess=types.SimpleNamespace(eBroadcastBitStateChanged=1),SBEvent=lambda:None)
   with tempfile.TemporaryDirectory()as tmp,patch.dict(sys.modules,{'lldb':fake}),patch.object(m.time,'monotonic',side_effect=[0,100]):
    out=Path(tmp)/'wait.json';m.lldb_wait(dbg,{'pid':123,'timeout_seconds':20,'output':str(out)});r=json.loads(out.read_text())
    self.assertFalse(r['hit']);self.assertTrue(r['detached']);self.assertTrue(p.detached)
    self.assertEqual('error'in r,unexpected)
    if not unexpected:self.assertTrue(r['no_hit_before_timeout'])
 def test_no_live_attach_fails_without_zero_metadata(self):
  # Real official LLDB processless negative is also run separately by producer.
  with tempfile.TemporaryDirectory()as tmp:
   p=types.SimpleNamespace(IsValid=lambda:False,GetState=lambda:0)
   dbg=types.SimpleNamespace(GetSelectedTarget=lambda:types.SimpleNamespace(GetProcess=lambda:p))
   fake=types.SimpleNamespace(eStateExited=10,eStateDetached=9)
   with patch.dict(sys.modules,{'lldb':fake}):
    out=Path(tmp)/'wait.json';m.lldb_wait(dbg,{'pid':123,'timeout_seconds':20,'output':str(out)});r=json.loads(out.read_text());self.assertIn('error',r);self.assertFalse(r['hit']);self.assertNotIn('snapshot',r)
if __name__=='__main__':unittest.main()
