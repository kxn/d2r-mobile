"""Replay the actual failed controller's frozen lifecycle log. No ADB."""
from pathlib import Path
import hashlib,importlib.util,json,subprocess,tempfile
R=Path('/home/dl/d2r-mobile');P=Path(__file__).with_name('run-game.py')
TRACE=R/'.local/logs/mali-vma-paused-7225d7f-Mali-v1/vma-pause-native-tail.log'
TRACE_SHA='e0a352564f4a212604a41ce50589eda1647fbecdbd1dccdee1227c6d97105a9d'
assert hashlib.sha256(TRACE.read_bytes()).hexdigest()==TRACE_SHA
RAW=TRACE.read_text();PID=6660;START='Android native start pid=6660\nowned original boot'
CURRENT='[dm] Android background save asked=1 commits=2 froze=0 elapsed_ms=321.2'
LATER='[dm] Android background save asked=1 commits=2 froze=0 elapsed_ms=302.7'
EARLIER='[dm] Android background save asked=1 commits=2 froze=0 elapsed_ms=281.1'
JSON=b'{"Total":{"AllocationCount":1}}\n';H=hashlib.sha256(JSON).hexdigest()
def module(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
new=module('successor',P);old=module('frozen_predecessor',R/'.local/worktrees/mali-vma-paused-capture-v1/scripts/dev/mali-vma-paused-capture-v1/run-game.py')
class Dev:
 def __init__(self,tail):self.tail=tail;self.saved=False;self.requested=False;self.command=['fake-owned-adb'];self.requests=0
 def digest(self,path):return H if self.saved and path.endswith('.json')else None
 def adb(self,*a):
  if a[:2]==('shell','pidof '+new.PKG+' || true'):return str(PID)
  if a[:3]==('shell','am','start'):assert not self.requested;self.requested=True;self.requests+=1;return 'existing Activity received explicit Intent'
  if a[:3]==('shell','dumpsys','meminfo'):assert self.saved;return 'official paused meminfo'
  raise AssertionError(a)
 def app(self,*a):assert a==('cat','files/runtime/native.log');return START+self.tail if self.saved else START
saved_run=new.subprocess.run;saved_map_new=new.showmap;saved_map_old=old.showmap;saved_clock=new.time.monotonic
cases=[]
try:
 for label,m,tail,confirmation,expect in [
  ('actual_trace_old_marker_reproduces_old_timeout',old,RAW,CURRENT,False),
  ('actual_trace_current_save_old_and_later_pauses_new_passes',new,RAW,CURRENT,True),
  ('later_repeat_pause_cannot_relabel_prior_dump',new,RAW,LATER,False),
  ('earlier_pause_before_explicit_request_rejected',new,RAW,EARLIER,False),
  ('only_prior_suspend_no_post_dump_marker_rejected',new,RAW[:RAW.index('[dm] [vulkan VMA dump] complete=1')]+RAW[RAW.index('[dm] [vulkan VMA dump] complete=1'):].split('[dm] [nvn] 图形挂起')[0],CURRENT,False)]:
  with tempfile.TemporaryDirectory()as td:
   out=Path(td);d={'serial':'10.10.10.44:34455'};dev=Dev(tail)
   def invoke(label,argv,timeout):
    assert label=='fresh-save'and not dev.saved;dev.saved=True
    Path(argv[argv.index('--report')+1]).write_text(json.dumps({'pid':PID,'fresh_original_lifecycle_save':True,'save_commits':2,'confirmation':confirmation}))
   def run(argv,**kw):
    assert argv==dev.command+['exec-out','run-as',new.PKG,'cat','files/runtime/temp/vulkan-vma.json']
    return subprocess.CompletedProcess(argv,0,stdout=JSON,stderr=b'')
   m.subprocess.run=run;m.showmap=lambda *args:None
   ticks=iter((0,21,42));m.time.monotonic=lambda:next(ticks)
   try:m.vma_pause_capture(dev,PID,d,out,invoke)
   except RuntimeError as e:passed=False;reason=str(e)
   else:passed=True;reason='exact original request/save2/completion/post-completion marker and JSON accepted'
   assert passed==expect,(label,reason)
   assert dev.requests==1 and d['fresh_save2_confirmed']
   if expect:assert(out/'vulkan-vma.json').read_bytes()==JSON
   else:assert not(out/'vulkan-vma.json').exists()
   cases.append({'case':label,'accepted':passed,'expected':expect,'reason':reason})
finally:
 new.subprocess.run=saved_run;new.showmap=saved_map_new;old.showmap=saved_map_old;new.time.monotonic=saved_clock
print(json.dumps({'passed':True,'cases':len(cases),'original_actual_trace_SHA256':TRACE_SHA,'results':cases,'device_access':False},ensure_ascii=False))
