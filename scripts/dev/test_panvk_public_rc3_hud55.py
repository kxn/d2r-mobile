"""Offline HUD55 identity gate; never creates a Device or invokes ADB."""
from pathlib import Path
import copy, hashlib, importlib.util, json, tempfile
p=Path(__file__).with_name('run-panvk-minegraph-game.py')
s=importlib.util.spec_from_file_location('hud55_rc3',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
checks=0
with tempfile.TemporaryDirectory()as tmp:
 root=Path(tmp);actual=root/'root-actual.json';actual.write_text('{}\n')
 base={'source_revision':m.SOURCE_COMMIT,'APK':m.APK,'ELF':m.ELF,'selected_module_resource_hashes':{'files/runtime/modules/test-'+str(i):'a'*64 for i in range(35)},'shader_chain':m.CHAIN,'root_actual_receipt':{'path':str(actual),'sha256':m.sha(actual)},'execution_ready':False,'root_actual_complete':False}
 def gate(data,execute=False):
  ready=root/'baseline.json';ready.write_text(json.dumps(data));return m.baseline_proof({'path':str(ready),'sha256':m.sha(ready)},execute)
 assert gate(base)==base['selected_module_resource_hashes'];checks+=1
 for field,value in [('source_revision','0'*40),('APK','0'*64),('ELF','0'*64),('shader_chain','oldchain'),('selected_module_resource_hashes',{}),('selected_module_resource_hashes',{'files/runtime/../bad':'a'*64,**{f'files/runtime/modules/test-{i}':'a'*64 for i in range(34)}}),('root_actual_receipt',{'path':str(actual),'sha256':'0'*64})]:
  bad=copy.deepcopy(base);bad[field]=value
  try:gate(bad)
  except RuntimeError:checks+=1
  else:raise AssertionError('invalid baseline admitted: '+field)
 approved=copy.deepcopy(base);approved.update(execution_ready=True,root_actual_complete=True)
 try:gate(approved,True)
 except RuntimeError:checks+=1
 else:raise AssertionError('missing final root pin admitted execute')
 try:m.baseline_proof(None)
 except RuntimeError:checks+=1
 else:raise AssertionError('missing snapshot admitted')
print(json.dumps({'passed':True,'checks':checks,'device_access':False,'final_root_pin_pending_execute_refused':True}))
