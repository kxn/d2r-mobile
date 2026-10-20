"""Exercise exact property rollback and stop-only native dump archive guards; no ADB."""
from pathlib import Path
import hashlib,importlib.util,io,json,shlex,tarfile,tempfile
p=Path(__file__).with_name('run-panvk-minegraph-game.py');s=importlib.util.spec_from_file_location('dump_controller',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Props:
 command=['fake-adb']
 def __init__(self,fail=False):self.values={'debug.mesa.panvk.debug':' old,value ','debug.mesa.pandecode.dump.file':''};self.before=self.values.copy();self.fail=fail;self.stops=0
 def stopped(self):self.stops+=1
 def adb(self,*a):
  assert a[0]=='shell';v=shlex.split(a[1]);assert len(v)==3 and v[0]=='setprop'and v[1]in m.PROPERTIES
  self.values[v[1]]=v[2]
  if self.fail and v[1]==list(m.PROPERTIES)[1]and v[2]==m.DUMP_PREFIX:self.fail=False;raise RuntimeError('transport lost after mutation')
 def get(self,args,**kw):assert args[-2]=='getprop';return(self.values[args[-1]]+'\n').encode()
class Dump:
 command=['fake-adb']
 def __init__(self,values):self.values=values;self.stops=0
 def stopped(self):self.stops+=1
 def app(self,*a):
  if a[0]=='sh':return 'DIR'
  if a[0]=='find':return ''if a[-1]=='l'else'\n'.join(m.DUMP_DIR+'/'+n for n in self.values)
  if a[0]=='sha256sum':return'\n'.join(self.values[n.removeprefix(m.DUMP_DIR+'/')]+'  '+n for n in a[1:])
  raise AssertionError(a)
def archive(contents,mode=None):
 b=io.BytesIO()
 with tarfile.open(fileobj=b,mode='w')as f:
  for n,v in contents.items():
   t=tarfile.TarInfo('./'+n);t.size=len(v)
   if mode=='symlink':t.type=tarfile.SYMTYPE;t.linkname='/not-owned';t.size=0
   f.addfile(t,io.BytesIO(v))
 return b.getvalue()
n=0;oldcheck=m.subprocess.check_output;oldrun=m.subprocess.run
with tempfile.TemporaryDirectory()as tmp:
 tmp=Path(tmp)
 try:
  for fail in [False,True]:
   dev=Props(fail);m.subprocess.check_output=dev.get;d={};r=tmp/('props-'+str(fail)+'.json')
   try:m.publish_properties(dev,r,d)
   except RuntimeError:assert fail
   else:assert not fail and dev.values==m.PROPERTIES
   m.restore_properties(dev,r,d);assert dev.values==dev.before and d['exact_touched_property_values_restored'];n+=1
  m.subprocess.check_output=oldcheck
  token='a'*32;data={m.OWNER:(token+'\n').encode(),'cs.app_process64.ctx-0.0000':b'Subqueue 1 Ringbuffer Trampoline [job 713]\nCALL d92, r94\n'}
  values={k:hashlib.sha256(v).hexdigest()for k,v in data.items()}
  for case in ['valid','corrupt','unknown','symlink','not-stopped']:
   dev=Dump(values);out=tmp/case;out.mkdir();d={'owner_sha256':values[m.OWNER]};r=out/'result.json'
   blob=archive(data if case!='corrupt'else{**data,'cs.app_process64.ctx-0.0000':b'bad'},'symlink'if case=='symlink'else None)
   if case=='unknown':blob=archive({**data,'../outside':b'bad'})
   if case=='not-stopped':
    def stopped():raise RuntimeError('game still running')
    dev.stopped=stopped
   def run(args,**kw):
    assert dev.stops>0 and args==dev.command+['exec-out','run-as',m.PKG,'tar','cf','-','-C',m.DUMP_DIR,'.'];kw['stdout'].write(blob)
   m.subprocess.run=run
   try:m.preserve_dump(dev,r,d,out)
   except RuntimeError:assert case!='valid'and not d.get('dump_transport_hashed_and_verified')
   else:assert case=='valid'and d['dump_transport_hashed_and_verified']and d['dump_pulled_only_after_game_stopped']
   n+=1
 finally:m.subprocess.check_output=oldcheck;m.subprocess.run=oldrun
print(json.dumps({'passed':True,'cases':n,'actual_functions':['get_property','publish_properties','restore_properties','inventory','preserve_dump'],'exact_whitespace_and_empty_properties_restored':True,'partial_property_mutation_transport_error_rollback':True,'valid_stop_only_tar_sha_verified':True,'corrupt_unknown_symlink_running_rejected':True,'device_access':False}))
