"""Offline exact HUD55 root baseline gate; no Device or ADB invocation."""
from pathlib import Path
import copy,importlib.util,json,tempfile
p=Path(__file__).with_name('run-panvk-minegraph-game.py');s=importlib.util.spec_from_file_location('hud55_rc3',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
row={'path':str(m.BASELINE_READY),'sha256':m.BASELINE_READY_SHA};base=m.read(m.BASELINE_READY)
assert m.baseline_proof(row,True)==base['original_modules_resources_private_driver_hashes'];checks=1
with tempfile.TemporaryDirectory()as tmp:
 def gate(data):
  ready=Path(tmp)/'baseline.json';ready.write_text(json.dumps(data))
  # Exercise semantic checks after the independent outer exact-byte pin.
  m.BASELINE_READY_SHA=m.sha(ready)
  return m.baseline_proof({'path':str(ready),'sha256':m.BASELINE_READY_SHA},True)
 for key,value in [('status','OLD022_BASELINE'),('source_revision','0'*40),('original_APK','0'*64),('original_ELF','0'*64),('shaderchain','oldchain'),('original_game_process_absent',False),('original_modules_resources_private_driver_hashes',{}),('root_actual_review',{'path':str(m.BASELINE_READY),'sha256':'0'*64})]:
  bad=copy.deepcopy(base);bad[key]=value
  try:gate(bad)
  except RuntimeError:checks+=1
  else:raise AssertionError('invalid baseline admitted: '+key)
 try:m.baseline_proof(None)
 except RuntimeError:checks+=1
 else:raise AssertionError('missing snapshot admitted')
print(json.dumps({'passed':True,'checks':checks,'exact_actual_root_receipt_bound':True,'device_access':False}))
