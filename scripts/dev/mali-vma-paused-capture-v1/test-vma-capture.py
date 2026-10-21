"""Exercise actual one-shot pause capture with original-tool fakes, never ADB."""
from pathlib import Path
import hashlib,importlib.util,json,subprocess,tempfile
s=importlib.util.spec_from_file_location('capture',Path(__file__).with_name('run-game.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
RAW=b'{"General":{"Total":{"AllocationCount":17}},"DetailedMap":{}}\n'
HASH=hashlib.sha256(RAW).hexdigest();START='Android native start pid=101\noriginal boot'
REQ='\n[vulkan VMA dump] explicit request accepted=1; consumes at next normal suspend'
DONE='\n[vulkan VMA dump] complete=1 errno=0 file=/data/user/0/'+m.PKG+'/files/runtime/temp/vulkan-vma.json builtin_detailed_map=1 post_drain_before_this_job_trim=1 earlier_tail_FRAME_trim_not_excluded=1; Usage/Budget may be VMA estimates, not resident/mtrack'
SAVE='\n[dm] Android background save asked=1 commits=2 froze=0 elapsed_ms=281.1'
SUSP='\n[nvn] 图形挂起：在途 GPU 工作已清空'
class Dev:
 def __init__(self,mode):self.mode=mode;self.requested=False;self.saved=False;self.events=[];self.command=['fake-owned-adb']
 def digest(self,path):
  self.events.append(('digest',path))
  if self.mode=='stale-json'and path.endswith('.json'):return HASH
  if self.mode=='stale-part'and path.endswith('.part'):return HASH
  return HASH if self.saved and path.endswith('.json')else None
 def adb(self,*a):
  if a[:2]==('shell','pidof '+m.PKG+' || true'):return '102'if self.saved and self.mode=='replacement'else'101'
  if a[:3]==('shell','am','start'):
   assert a==('shell','am','start','-W','-n',m.PKG+'/.MainActivity','--ez','vma_dump_on_suspend','true')
   self.events.append(('request',));self.requested=True;return 'existing Activity onNewIntent'
  if a[:3]==('shell','dumpsys','meminfo'):
   assert self.saved;self.events.append(('meminfo',));return 'existing standard paused meminfo output'
  raise AssertionError('unexpected tool '+repr(a))
 def app(self,*a):
  assert a==('cat','files/runtime/native.log');self.events.append(('log',))
  if not self.saved:return START
  if self.mode=='reset':return 'reset log'
  req=REQ.replace('accepted=1','accepted=0')if self.mode=='rejected'else REQ
  if self.mode=='duplicate':req+=REQ
  done=DONE.replace('complete=1 errno=0','complete=0 errno=17')if self.mode=='failed-output'else DONE
  if self.mode=='failed-boundary':done='\n[vulkan VMA dump] failed: completion/reset not drained; request consumed'
  if self.mode=='wrong-path':done=done.replace('/files/runtime/temp/vulkan-vma.json','/different.json')
  if self.mode=='missing-request':req=''
  if self.mode=='out-of-order':return START+DONE+REQ+SAVE+SUSP
  if self.mode=='timeout':return START+REQ
  return START+req+done+SAVE+SUSP if self.mode=='wrong-pause'else START+req+SAVE+done+SUSP
original_run=m.subprocess.run;original_showmap=m.showmap;original_clock=m.time.monotonic
count=0
try:
 for mode in ('ok','stale-json','stale-part','replacement','reset','rejected','duplicate','failed-output','failed-boundary','wrong-path','missing-request','out-of-order','timeout','changed-transfer','wrong-pause'):
  with tempfile.TemporaryDirectory()as td:
   out=Path(td);d={'serial':'10.10.10.44:34455'};dev=Dev(mode)
   def invoke(label,argv,timeout):
    assert label=='fresh-save'and timeout==45 and not dev.saved
    assert '--vma-dump-on-suspend'in argv;dev.events.append(('request',));dev.requested=True
    dev.events.append(('save2',));dev.saved=True
    Path(argv[argv.index('--report')+1]).write_text(json.dumps({'pid':101,'fresh_original_lifecycle_save':True,'save_commits':2,'confirmation':SAVE.strip(),'vma_dump_requested':True,'vma_request_confirmation':REQ.strip()}))
   def showmap(dev,pid,out):assert dev.saved;dev.events.append(('showmap',))
   def run(argv,**kw):
    assert argv==dev.command+['exec-out','run-as',m.PKG,'cat','files/runtime/temp/vulkan-vma.json']
    dev.events.append(('pull',));return subprocess.CompletedProcess(argv,0,stdout=RAW if mode!='changed-transfer'else RAW+b' ',stderr=b'')
   m.subprocess.run=run;m.showmap=showmap
   ticks=iter((0,21,42));m.time.monotonic=lambda:next(ticks)
   try:m.vma_pause_capture(dev,101,d,out,invoke)
   except RuntimeError:
    assert mode!='ok'and not d['actual_paused_VMA_capture_complete']
   else:
    assert mode=='ok'and d['actual_paused_VMA_capture_complete']and(out/'vulkan-vma.json').read_bytes()==RAW
    tags=[x[0]for x in dev.events];assert tags.index('request')<tags.index('save2')<tags.index('meminfo')<tags.index('showmap')<tags.index('pull')
    assert d['VMA_original_JSON']['sha256']==HASH and 'not simultaneous'in d['memory_stage']
   if mode.startswith('stale'):assert not dev.requested and not dev.saved
   else:
    assert len([e for e in dev.events if e[0]=='request'])==1 and d['lifecycle_save_attempted']
    if mode!='replacement':assert d['fresh_save2_confirmed']
   count+=1
finally:m.subprocess.run=original_run;m.showmap=original_showmap;m.time.monotonic=original_clock
print(json.dumps({'passed':True,'cases':count,'covers':'same PID, no stale JSON/part, current request ordering/acceptance, original save2, suspend marker, failure/timeout consumes no retry, exact original JSON SHA/bytes, paused official-tool order','device_access':False}))
