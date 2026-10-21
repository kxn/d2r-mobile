"""Replay both failed actual traces through the real original/extended helper.
Only standard adb subprocess calls are faked; no device access or lifecycle replacement.
"""
from pathlib import Path
import contextlib,hashlib,io,json,re,runpy,subprocess,sys,tempfile,time
R=Path('/home/dl/d2r-mobile');NEW=Path(__file__).resolve().parent.parent/'android-game-background-save.py';OLD=R/'scripts/dev/android-game-background-save.py'
TRACES=[
 ('v1',6660,'e0a352564f4a212604a41ce50589eda1647fbecdbd1dccdee1227c6d97105a9d'),
 ('v2',12267,'2c9d5554f5edc02dfc8df0a5e98b386861e8a94cf09f6f00871afd3a1a656d1a')]
PKG='com.d2rmobile.nativeport';SERIAL='10.10.10.44:34455';ACCEPT='[vulkan VMA dump] explicit request accepted=1; consumes at next normal suspend'
class Fake:
 def __init__(self,raw,pid,mode='ok'):
  self.pid=str(pid);self.mode=mode;self.started=False;self.home=False;self.log_reads=0;self.clock=0;self.calls=[];self.accept_observed=False
  accepted=re.search(r'^.*\[vulkan VMA dump\] explicit request accepted=1;[^\n]*',raw,re.M)
  fresh=next(x for x in re.finditer(r'^.*Android background save asked=1 commits=2[^\n]*',raw,re.M)if x.start()>accepted.end())
  self.fresh=fresh.group(0);self.old_phase=raw[:accepted.start()];self.accept_phase=raw[:accepted.end()];self.offset_phase=raw[:fresh.start()];self.final=raw
  self.boot='Android native start pid='+self.pid+'\noriginal boot\n'
  if mode=='stale-accepted':self.boot+='[dm] '+ACCEPT+'\n'
 def now(self):self.clock+=1;return self.clock
 def output(self,argv,**kw):
  assert argv[:3]==['adb','-s',SERIAL];a=argv[3:];self.calls.append(a)
  if a==['shell','pidof',PKG]:return ('99999'if self.started and self.mode=='pid-change'else self.pid)+'\n'
  if a==['exec-out','run-as',PKG,'cat','files/runtime/native.log']:
   if not self.started:return self.boot
   if self.home:return self.boot+self.final
   self.log_reads+=1
   if not self.optional:return self.boot+self.offset_phase
   if self.mode=='reset':return 'reset native log'
   if self.mode=='stale-accepted':return self.boot+self.old_phase
   if self.mode=='rejected':return self.boot+self.accept_phase.replace('accepted=1','accepted=0')
   if self.mode=='duplicate':return self.boot+self.accept_phase+'\n[dm] '+ACCEPT
   if self.log_reads==1:return self.boot+self.old_phase
   if self.log_reads==2:self.accept_observed=True;return self.boot+self.accept_phase
   return self.boot+self.offset_phase
  if a[:4]==['shell','am','start','-W']:
   if self.home:assert a==['shell','am','start','-W','-n',PKG+'/.MainActivity'];return 'existing explicit resume\n'
   assert not self.started;self.started=True;self.optional='--ez'in a
   assert a==['shell','am','start','-W','-n',PKG+'/.MainActivity']+(['--ez','vma_dump_on_suspend','true']if self.optional else[])
   return 'am start returned before async onNewIntent acceptance\n'
  if a==['shell','input','keyevent','HOME']:
   assert self.started and not self.home
   if self.optional:assert self.accept_observed,'HOME before actual accepted callback'
   self.home=True;return ''
  raise AssertionError('unexpected subprocess '+repr(a))
def execute(path,raw,pid,flag=False,mode='ok',resume=False):
 fake=Fake(raw,pid,mode);oldargv=sys.argv;oldout=subprocess.check_output;oldclock=time.monotonic;oldsleep=time.sleep
 try:
  with tempfile.TemporaryDirectory()as td:
   report=Path(td)/'fresh-save.json';sys.argv=[str(path),'--serial',SERIAL,'--report',str(report)]+(['--vma-dump-on-suspend']if flag else[])+(['--resume']if resume else[])
   subprocess.check_output=fake.output;time.monotonic=fake.now;time.sleep=lambda _:None
   error=None
   try:
    with contextlib.redirect_stdout(io.StringIO()):runpy.run_path(str(path),run_name='__main__')
   except SystemExit as e:error=str(e)
   result=json.loads(report.read_text())if report.exists()else None
 finally:sys.argv=oldargv;subprocess.check_output=oldout;time.monotonic=oldclock;time.sleep=oldsleep
 return fake,result,error
results=[]
for label,pid,h in TRACES:
 p=R/'.local/logs'/('mali-vma-paused-7225d7f-Mali-'+label)/'vma-pause-native-tail.log';assert hashlib.sha256(p.read_bytes()).hexdigest()==h;raw=p.read_text()
 for resume in (False,True):
  old,old_result,old_error=execute(OLD,raw,pid,resume=resume);new,result,error=execute(NEW,raw,pid,resume=resume)
  assert old_error is None and error is None and old.calls==new.calls and old_result==result
  results.append({'case':label+' default resume='+str(resume),'default_original_ADB_sequence_and_report_identical':True})
 fake,result,error=execute(NEW,raw,pid,flag=True)
 assert error is None and result['confirmation']==fake.fresh and result['vma_dump_requested']and ACCEPT in result['vma_request_confirmation']
 assert len([x for x in fake.calls if x[:3]==['shell','am','start']])==1 and fake.calls.count(['shell','input','keyevent','HOME'])==1
 results.append({'case':label+' async old-save before accepted ignored','confirmation':result['confirmation'],'single_foreground_and_HOME':True})
 for mode in ('stale-accepted','rejected','duplicate','reset','pid-change'):
  fake,result,error=execute(NEW,raw,pid,flag=True,mode=mode)
  assert error and result is None and not fake.home
  assert len([x for x in fake.calls if x[:3]==['shell','am','start']])==1
  results.append({'case':label+' '+mode,'rejected_before_HOME':True,'reason':error})
print(json.dumps({'passed':True,'cases':len(results),'results':results,'two_original_trace_hashes':{x[0]:x[2]for x in TRACES},'device_access':False},ensure_ascii=False))
