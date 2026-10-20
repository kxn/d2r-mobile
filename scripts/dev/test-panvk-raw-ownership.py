"""Preserve the actual rejected pidof response; no signal/ADB operation."""
import ast,importlib.util,json,tempfile
from pathlib import Path
P=Path(__file__).with_name('run-panvk-minegraph-game.py');sp=importlib.util.spec_from_file_location('controller',P);m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);tree=ast.parse(P.read_text());f=next(n for n in ast.walk(tree)if isinstance(n,ast.FunctionDef)and n.name=='observe');h=next(n for n in ast.walk(f)if isinstance(n,ast.ExceptHandler));calls=[];checks=0
with tempfile.TemporaryDirectory()as td:
 for raw in ['25470 26341','26341','not-a-pid']:
  d={'first_PID':25470};ns={'d':d,'own':'25470','now':raw,'ambiguous':False,'path':Path(td)/'result.json','journal':lambda p,d:calls.append(dict(d))}
  try:
   try:raise RuntimeError('unowned/multiple/replacement game PID')
   except RuntimeError:exec(compile(ast.Module(body=h.body,type_ignores=[]),str(P),'exec'),ns)
  except RuntimeError:pass
  else:raise AssertionError('original ownership refusal suppressed')
  assert ns['ambiguous']and d['ownership_failed_raw_pidof']==raw and d['ownership_failed_first_PID']=='25470'and d['first_PID']==25470 and calls[-1]['ownership_failed_raw_pidof']==raw;checks+=1
 for raw in ['25470 26341','26341','bad']:
  d={}
  try:m.old.claim_pid(d,raw,'25470')
  except RuntimeError:assert d['ownership_failed'];checks+=1
  else:raise AssertionError('unknown PID adopted')
 assert m.old.claim_pid({},'25470','25470')=='25470';checks+=1
print(json.dumps({'passed':True,'checks':checks,'real_exception_handler_raw_response_journaled_before_reraise':True,'actual_existing_helper_refuses_unknown_multiple_replacement':True,'device_access':False}))
