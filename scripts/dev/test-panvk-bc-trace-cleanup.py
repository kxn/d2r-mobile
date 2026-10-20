"""Actual token property rollback and refusal of unwantedTRACE output; noADB."""
from pathlib import Path
import importlib.util,json,tempfile,shlex
P=Path(__file__).with_name('run-panvk-minegraph-game.py');sp=importlib.util.spec_from_file_location('controller',P);m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
class Props:
 command=['offline-only']
 def __init__(self,fail=False):self.values={'debug.mesa.panvk.debug':' old,value ','debug.mesa.pandecode.dump.file':' /old/dump '};self.before=self.values.copy();self.fail=fail
 def stopped(self):pass
 def adb(self,*a):
  assert a[0]=='shell';v=shlex.split(a[1]);assert len(v)==3 and v[0]=='setprop'and v[1]in m.PROPERTIES;self.values[v[1]]=v[2]
  if self.fail and v[1]=='debug.mesa.pandecode.dump.file'and v[2]=='':self.fail=False;raise RuntimeError('transport lost after mutation')
 def get(self,args,**kw):assert args[-2]=='getprop';return(self.values[args[-1]]+'\n').encode()
checks=0;oldcheck=m.subprocess.check_output;oldinventory=m.inventory
with tempfile.TemporaryDirectory()as tmp:
 tmp=Path(tmp)
 try:
  for fail in [False,True]:
   dev=Props(fail);m.subprocess.check_output=dev.get;d={};p=tmp/('properties-'+str(fail)+'.json')
   try:m.publish_properties(dev,p,d)
   except RuntimeError:assert fail
   else:assert not fail and dev.values==m.PROPERTIES
   m.restore_properties(dev,p,d);assert dev.values==dev.before and d['exact_touched_property_values_restored'];checks+=1
  class Dev:
   def stopped(self):pass
  for values,want in [({m.OWNER:'a'*64},True),({m.OWNER:'b'*64},False),({m.OWNER:'a'*64,'cs.fake.ctx-0.0':'b'*64},False),(None,False),({},False)]:
   m.inventory=lambda *_:values;d={'owner_sha256':'a'*64};p=tmp/'no-dump.json'
   try:m.preserve_dump(Dev(),p,d,tmp)
   except RuntimeError:assert not want
   else:assert want and d['TRACE_DUMP_files_absent']and d['dump_absent']and'dump_archive'not in d
   checks+=1
 finally:m.subprocess.check_output=oldcheck;m.inventory=oldinventory
print(json.dumps({'checks':checks,'actual_property_write_then_transport_failure_restores_both_exact_old_values':True,'only_owned_empty_dump_directory_accepted_no_tar':True,'unexpected_capture_files_preserved_refused':True,'ADB_access':False}))
