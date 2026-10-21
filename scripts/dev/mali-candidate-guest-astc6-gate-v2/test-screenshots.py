"""Exercise actual auxiliary screencap/route control flow, no devices."""
from pathlib import Path
import copy,importlib.util,json,subprocess,tempfile,unittest
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('screenshot_controller',Path(__file__).with_name('run-game.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
PNG=b'\x89PNG\r\n\x1a\nowned fake PNG bytes, not a real screenshot'
class Dev:
 command=['fake-owned-adb']
 def __init__(self,pid=101):self.pid=pid
 def adb(self,*a):assert a==('shell','pidof '+m.PKG+' || true');return str(self.pid)
class Capture(unittest.TestCase):
 def test_actual_helper_save_and_identity(self):
  with tempfile.TemporaryDirectory()as tmp:
   out=Path(tmp);r={};calls=[]
   def run(a,**kw):
    calls.append(a);self.assertEqual(a,['fake-owned-adb','exec-out','screencap','-p']);self.assertEqual(kw,dict(capture_output=True,check=True,timeout=15));return subprocess.CompletedProcess(a,0,stdout=PNG)
   with patch.object(m.subprocess,'run',run):row=m.capture_screen(Dev(),101,out,'before-exit',r)
   self.assertTrue(row['capture_complete']);self.assertTrue(row['visual_review_pending']);self.assertEqual((out/'before-exit.png').read_bytes(),PNG);self.assertEqual(row['sha256'],m.sha(out/'before-exit.png'));self.assertNotIn('screenshot_errors',r)
   with patch.object(m.subprocess,'run',side_effect=AssertionError('must not touch replaced PID')):row=m.capture_screen(Dev(102),101,out,'changed',r)
   self.assertFalse(row['capture_complete']);self.assertFalse((out/'changed.png').exists());self.assertEqual(len(r['screenshot_errors']),1)
 def test_badPNG_or_tool_failure_auxiliary(self):
  for fault in ['badPNG','timeout','nonzero','write']:
   with self.subTest(fault=fault),tempfile.TemporaryDirectory()as tmp:
    out=Path(tmp);r={}
    if fault=='write':(out/'before-exit.png').write_bytes(b'preexisting evidence')
    def run(a,**kw):
     if fault=='timeout':raise subprocess.TimeoutExpired(a,15)
     if fault=='nonzero':raise subprocess.CalledProcessError(1,a)
     return subprocess.CompletedProcess(a,0,stdout=b'notPNG'if fault=='badPNG'else PNG)
    with patch.object(m.subprocess,'run',run):row=m.capture_screen(Dev(),101,out,'before-exit',r)
    self.assertFalse(row['capture_complete']);self.assertEqual(len(r['screenshot_errors']),1);self.assertNotIn('visual_review_pending',row)
    if fault=='write':self.assertEqual((out/'before-exit.png').read_bytes(),b'preexisting evidence')
 def test_cooperative_cancel_not_swallowed(self):
  with tempfile.TemporaryDirectory()as tmp:
   err=RuntimeError('cooperative owner cancellation: save2/stop/restore only')
   with patch.object(m.subprocess,'run',side_effect=err):
    with self.assertRaises(RuntimeError)as caught:m.capture_screen(Dev(),101,Path(tmp),'before-exit',{})
   self.assertIs(caught.exception,err)
 def test_actual_route_success_fail_and_exception_images(self):
  for mode in ['normal','east-failed','gate-failed','east-timeout','shot-failed-route-failed','shot-failed-route-success']:
   with self.subTest(mode=mode),tempfile.TemporaryDirectory()as tmp:
    out=Path(tmp);dev=Dev();state={'area':1,'x':5234,'y':6282,'near_area':2,'near_x':5234,'near_y':6277};legs=[];shots=[]
    timeout=subprocess.TimeoutExpired(['original walk'],40)
    def snapshot(dev,pid,path):path.write_text(json.dumps(state));return copy.deepcopy(state)
    def run(a,**kw):
     if a[:1]==['fake-owned-adb']:
      shots.append(a)
      if mode.startswith('shot-failed'):raise subprocess.CalledProcessError(1,a)
      return subprocess.CompletedProcess(a,0,stdout=PNG)
     self.assertIn('--expect-pid',a);legs.append(a)
     if mode=='east-timeout':raise timeout
     east=len(legs)==1;state.update(x=5265 if east else 5267,y=6288 if east else 6278,area=1 if east else 2)
     failed=(east and mode in ['east-failed','shot-failed-route-failed'])or(not east and mode=='gate-failed')
     return subprocess.CompletedProcess(a,2 if failed else 0)
    error=None
    with patch.object(m,'snapshot',snapshot),patch.object(m.subprocess,'run',run):
     try:m.walk_reported_exit(dev,101,out,'10.10.10.44:34455')
     except Exception as e:error=e
    r=json.loads((out/'reported-exit-result.json').read_text());expected=2 if mode in ['east-failed','east-timeout','shot-failed-route-failed']else 3
    self.assertEqual(len(shots),expected);self.assertEqual([x['label']for x in r['screenshots']],['before-exit','after-east']+(['after-gate']if expected==3 else[]))
    if mode in ['normal','shot-failed-route-success']:self.assertIsNone(error);self.assertTrue(r['actual_area_transition_accepted'])
    elif mode=='east-timeout':self.assertIs(error,timeout);self.assertFalse(r['actual_area_transition_accepted'])
    else:self.assertEqual(str(error),'normal historical route failed or unexpected area');self.assertFalse(r['actual_area_transition_accepted'])
    if mode.startswith('shot-failed'):self.assertEqual(len(r['screenshot_errors']),expected);self.assertFalse(list(out.glob('*.png')))
    else:self.assertEqual(len(list(out.glob('*.png'))),expected)
if __name__=='__main__':unittest.main(verbosity=2)
