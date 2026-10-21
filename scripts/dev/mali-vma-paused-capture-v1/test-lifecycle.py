"""Execute actual launch-finally control flow using fake official-tool failures."""
from pathlib import Path
import importlib.util,json,tempfile,subprocess
s=importlib.util.spec_from_file_location('m',Path(__file__).with_name('run-game.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
realrun=m.subprocess.run;realpopen=m.subprocess.Popen
class Client:
 def __init__(self,argv,**kw):
  self.n=0;self.returncode=0;p=Path(argv[argv.index('--report')+1]);p.write_text(json.dumps({'actual_vulkan_provider':'system'}))
 def poll(self):self.n+=1;return None if self.n==1 else 0
 def terminate(self):raise AssertionError('should not kill already-completed launcher')
class Dev:
 def __init__(self,mode):self.mode=mode;self.tool_failed=False;self.force_stops=0;self.saves=0;self.requested=False;self.saved=False;self.command=['fake-owned-adb'];self.state={'valid':True,'world_input_ready':True,'scale_3d':.65,'guest_heap':{'shared':False},'game_output':{'width':2772,'height':1240},'surface_output':{'width':2772,'height':1240},'world_freeze':{'host_frozen':False},'runtime_extrap':{'enabled':False},'area':1,'x':5225,'y':6295}
 def stopped(self):assert self.force_stops or self.mode=='exited'
 def adb(self,*a):
  if a[:2]==('shell','pidof '+m.PKG+' || true'):
   if self.force_stops or(self.tool_failed and self.mode in ('exited','pre-exited')):return ''
   return '102'if(self.tool_failed and self.mode in ('replacement','pre-replacement'))else'101'
  if a[:3]==('shell','am','start'):self.requested=True;return 'existing top Activity delivered Intent'
  if a[:3]==('shell','dumpsys','meminfo'):self.tool_failed=True;raise RuntimeError('official meminfo failed; exercise original finally')
  if a[:3]==('shell','am','force-stop'):self.force_stops+=1;return ''
  raise AssertionError('unexpected primitive '+str(a))
 def digest(self,path):return None
 def app(self,*a):
  if a[0]=='id':return '10311'
  if a[0]=='tail':return json.dumps(self.state)
  if a[0]=='cat'and a[1].endswith('/status'):return 'Uid:\t10311\t10311\t10311\t10311\n'
  if a[0]=='cat'and a[1].endswith('/maps'):return 'owned system Vulkan mapping'
  if a[0]=='cat':
   text='Android native start pid=101\nordinary native log'
   if self.requested:text+='\n[vulkan VMA dump] explicit request accepted=1; consumes at next normal suspend'
   if self.saved:text+='\n[dm] Android background save asked=1 commits=2 froze=0 elapsed_ms=281.1\n[vulkan VMA dump] complete=1 errno=0 file=/data/user/0/'+m.PKG+'/files/runtime/temp/vulkan-vma.json builtin_detailed_map=1 post_drain_before_this_job_trim=1 earlier_tail_FRAME_trim_not_excluded=1; Usage/Budget may be VMA estimates, not resident/mtrack\n[nvn] 图形挂起：在途 GPU 工作已清空'
   return text
  if a[0]=='sh':return 'original log retained'
  raise AssertionError('unexpected app '+str(a))
count=0
try:
 for mode in ('normal','exited','replacement','save-failed','pre-exited','pre-replacement'):
  with tempfile.TemporaryDirectory()as td:
   out=Path(td);dev=Dev(mode);d={'serial':'10.10.10.44:34455'}
   def run(argv,**kw):
    if any('walk-to.py'in str(x)for x in argv):return subprocess.CompletedProcess(argv,0)
    if 'screencap'in argv and mode.startswith('pre-'):dev.tool_failed=True;raise RuntimeError('capture failed before any lifecycle save')
    if 'screencap'in argv:return subprocess.CompletedProcess(argv,0,stdout=b'\x89PNG\r\n\x1a\nfixture',stderr=b'')
    if any('background-save.py'in str(x)for x in argv):
     dev.requested='--vma-dump-on-suspend'in argv;dev.saved=True;dev.saves+=1;p=Path(argv[argv.index('--report')+1]);p.write_text(json.dumps({'pid':101,'fresh_original_lifecycle_save':mode!='save-failed','save_commits':2,'confirmation':'[dm] Android background save asked=1 commits=2 froze=0 elapsed_ms=281.1','vma_dump_requested':dev.requested,'vma_request_confirmation':'[dm] [vulkan VMA dump] explicit request accepted=1; consumes at next normal suspend'}));return subprocess.CompletedProcess(argv,0)
    raise AssertionError('unexpected subprocess '+str(argv))
   m.subprocess.run=run;m.subprocess.Popen=Client
   try:m.launch(dev,d,{},out)
   except RuntimeError:pass
   else:raise AssertionError('official failure misreported as success')
   assert d['first_PID']==101
   if mode=='normal':assert dev.saves==1 and dev.force_stops==1 and d['fresh_save2_then_stopped']
   elif mode=='exited':assert dev.saves==1 and dev.force_stops==0 and d['already_exited_no_save_restart']
   elif mode=='replacement':assert dev.saves==1 and dev.force_stops==0
   elif mode=='save-failed':assert dev.saves==1 and dev.force_stops==0
   elif mode=='pre-exited':assert dev.saves==0 and dev.force_stops==0 and d['already_exited_no_save_restart']
   else:assert mode=='pre-replacement'and dev.saves==0 and dev.force_stops==0
   count+=1
finally:m.subprocess.run=realrun;m.subprocess.Popen=realpopen
print(json.dumps({'passed':True,'cases':count,'actual_launch_finally':'original lifecycle save2 before force-stop; no restart/re-save on exitedPID; no re-save/kill replacement after original pause; failed save preserves running owner','device_access':False}))
