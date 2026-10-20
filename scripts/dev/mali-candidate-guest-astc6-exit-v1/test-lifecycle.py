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
 def __init__(self,mode):self.mode=mode;self.tool_failed=False;self.force_stops=0;self.saves=0;self.command=['fake-owned-adb'];self.state={'valid':True,'world_input_ready':True,'scale_3d':.75,'guest_heap':{'shared':False},'game_output':{'width':2772,'height':1240},'surface_output':{'width':2772,'height':1240},'world_freeze':{'host_frozen':False},'runtime_extrap':{'enabled':False},'area':1,'x':5225,'y':6295,'near_area':2,'near_x':5225,'near_y':6277}
 def stopped(self):assert self.force_stops or self.mode=='exited'
 def adb(self,*a):
  if a[:2]==('shell','pidof '+m.PKG+' || true'):
   if self.force_stops or(self.tool_failed and self.mode=='exited'):return ''
   return '102'if(self.tool_failed and self.mode=='replacement')else'101'
  if a[:3]==('shell','dumpsys','meminfo'):self.tool_failed=True;raise RuntimeError('official meminfo failed; exercise original finally')
  if a[:3]==('shell','am','force-stop'):self.force_stops+=1;return ''
  raise AssertionError('unexpected primitive '+str(a))
 def app(self,*a):
  if a[0]=='id':return '10311'
  if a[0]=='tail':return json.dumps(self.state)
  if a[0]=='cat'and a[1].endswith('/status'):return 'Uid:\t10311\t10311\t10311\t10311\n'
  if a[0]=='cat'and a[1].endswith('/maps'):return 'owned system Vulkan mapping'
  if a[0]=='cat':return 'ordinary native log'
  if a[0]=='sh':return 'original log retained'
  raise AssertionError('unexpected app '+str(a))
count=0
try:
 for mode in ('normal','exited','replacement','save-failed'):
  with tempfile.TemporaryDirectory()as td:
   out=Path(td);dev=Dev(mode);d={'serial':'10.10.10.44:34455'}
   def run(argv,**kw):
    if any('walk-reported-exit.py'in str(x)for x in argv):dev.state['area']=2;return subprocess.CompletedProcess(argv,0)
    if any('walk-to.py'in str(x)for x in argv):return subprocess.CompletedProcess(argv,0)
    if any('capture-official-cpu.py'in str(x)for x in argv):raise AssertionError('should not repeat CPU profile')
    if any('background-save.py'in str(x)for x in argv):
     dev.saves+=1;p=Path(argv[argv.index('--report')+1]);p.write_text(json.dumps({'pid':101,'fresh_original_lifecycle_save':mode!='save-failed','save_commits':2}));return subprocess.CompletedProcess(argv,0)
    raise AssertionError('unexpected subprocess '+str(argv))
   m.subprocess.run=run;m.subprocess.Popen=Client
   try:m.launch(dev,d,{},out)
   except RuntimeError:pass
   else:raise AssertionError('official failure misreported as success')
   assert d['first_PID']==101
   if mode=='normal':assert dev.saves==1 and dev.force_stops==1 and d['fresh_save2_then_stopped']
   elif mode=='exited':assert dev.saves==0 and dev.force_stops==0 and d['already_exited_no_save_restart']
   elif mode=='replacement':assert dev.saves==0 and dev.force_stops==0
   else:assert dev.saves==1 and dev.force_stops==0
   count+=1
finally:m.subprocess.run=realrun;m.subprocess.Popen=realpopen
print(json.dumps({'passed':True,'cases':count,'actual_launch_finally':'original lifecycle save2 before force-stop; no restart on exitedPID; no save/kill replacement; failed save preserves running owner','device_access':False}))
