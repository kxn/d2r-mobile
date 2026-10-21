#!/usr/bin/env python3
"""Exercise the actual final CLI Raise in a Python child; no controller/device IO."""
from pathlib import Path
import ast,hashlib,json,subprocess,sys
ROOT=Path('/home/dl/d2r-mobile');HERE=Path(__file__).resolve().parent
current=HERE/'run-native-astc-linear-NVN-companion-v1.py';old=ROOT/'.local/worktrees/native-qualified-astc6-NVN-tools-v1/scripts/dev'/current.name

def terminal(path):
 tree=ast.parse(path.read_text());assert isinstance(tree.body[-1],ast.Raise)
 return ast.get_source_segment(path.read_text(),tree.body[-1])

def exitcode(statement,result):
 # Actual subprocess/CLI status, not catching SystemExit inside a fake call.
 code='import json\nresult=json.loads('+repr(json.dumps(result))+')\n'+statement+'\n'
 p=subprocess.run([sys.executable,'-c',code],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 assert not p.stdout and not p.stderr,(p.stdout,p.stderr)
 return p.returncode

new=terminal(current);previous=terminal(old)
oldfunc={n.name:ast.dump(n,include_attributes=False)for n in ast.parse(old.read_text()).body if isinstance(n,ast.FunctionDef)}
newfunc={n.name:ast.dump(n,include_attributes=False)for n in ast.parse(current.read_text()).body if isinstance(n,ast.FunctionDef)}
assert oldfunc==newfunc and len(newfunc)==13
cases=[]
for stage,success in [('actual-qualified-ASTC6-original-NVN-native-window-family-owned-passed',True),('actual-original-NVN-native-window-family-owned-passed',True),('actual-old-AA-native-table-refused-by-original-runtime',True),('failed-preserved',False),('preflight',False),('unknown-success',False)]:
 for cleanup in [[],['preserved cleanup error']]:
  result={'stage':stage,'cleanup_errors':cleanup};actual=exitcode(new,result);expected=0 if success and not cleanup else 1
  assert actual==expected,(result,actual,expected);cases.append({'stage':stage,'cleanup_errors':cleanup,'actual_child_exit':actual})
path=ROOT/'.local/logs/native-qualified-astc6-NVN-165-Q735-v1/result.json';before=hashlib.sha256(path.read_bytes()).hexdigest();result=json.loads(path.read_text());assert result['stage']=='actual-qualified-ASTC6-original-NVN-native-window-family-owned-passed'and not result['cleanup_errors']and result['original_unchanged']
assert exitcode(previous,result)==1 and exitcode(new,result)==0
assert hashlib.sha256(path.read_bytes()).hexdigest()==before
print(json.dumps({'passed':True,'cases':cases,'existing_function_AST_exact':len(newfunc),'actual_165_result':{'path':str(path),'sha256':before,'read_only':True,'previous_final_Raise_child_exit':1,'corrected_final_Raise_child_exit':0},'GPU_rerun':False,'device_access':False,'scope':'Only final CLI stage-to-exit mapping; existing actual GPU/cleanup records are not edited or replaced.'}))
