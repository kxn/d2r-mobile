"""Public immutable driver admission and existing exact cold-pause proof; no device."""
from pathlib import Path
import ast,copy,importlib.util,json
p=Path(__file__).with_name('run-panvk-minegraph-game.py');s=importlib.util.spec_from_file_location('candidate',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
m.proof();checks=1
original_read=m.read;original_sha=m.sha;cache={}
def cached_sha(p):
 p=str(p)
 if p not in cache:cache[p]=original_sha(p)
 return cache[p]
m.sha=cached_sha
base=original_read(m.READY)
for name,value in [('associated_source_commit','0'*40),('ELF_BuildID','0'*40),('needed',['libhardware.so']),('ICD_exports_verified',[]),('status','LOCAL_MODIFIED_DRIVER')]:
 bad=copy.deepcopy(base);bad[name]=value
 m.read=lambda p:bad if Path(p)==m.READY else original_read(p)
 try:m.proof()
 except RuntimeError:checks+=1
 else:raise AssertionError('admitted invalid published identity '+name)
m.read=original_read
A=Path('/home/dl/d2r-mobile/.local/logs/native-bulk-bcbd751-malloc-control-v1/recovery/activities.txt').read_text();assert m.activity_stopped(A,'21077');checks+=1
before='[dm] Android native start pid=21077\n[dm] menu\n';fresh='[dm] [nvn] 图形挂起：在途 GPU 工作已清空\n';assert m.cold_pause_proof(before,before+fresh,A,'21077');checks+=1
for b,a in [(before,before),(before,before+'Android background save asked=1 commits=0\n'+fresh),(before,before+'Android background save asked=1 commits=2\n'+fresh),(before,'RESET'+fresh),(before.replace('21077','21078'),before+fresh)]:assert not m.cold_pause_proof(b,a,A,'21077');checks+=1
for a,pid in [(A,'21078'),(A.replace('state=STOPPED','state=PAUSED'),'21077'),(A.replace(m.PKG+'/.MainActivity ',m.PKG+'/.OtherActivity '),'21077'),(A.replace(m.PKG+'/.MainActivity ',m.PKG+'/.MainActivityExtra '),'21077')]:assert not m.activity_stopped(a,pid);checks+=1
# New tool must reuse old pure CAS and exact already-reviewed no-world proof.
def fn(path,name):
 tree=ast.parse(Path(path).read_text());return ast.dump(next(n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name==name),include_attributes=False)
V5='/home/dl/d2r-mobile/.local/worktrees/panvk-minegraph-original-bc-game-v5/scripts/dev/run-panvk-minegraph-game.py';V6='/home/dl/d2r-mobile/.local/worktrees/panvk-shader-map-game-v6/scripts/dev/run-panvk-minegraph-game.py'
for name in ['inventory','rename_dir','resource_proof','restore','clean','command','owned']:assert fn(p,name)==fn(V5,name);checks+=1
for name in ['native_log_text','activity_stopped','cold_pause_proof']:assert fn(p,name)==fn(V6,name);checks+=1
text=p.read_text();assert 'setprop'not in text and 'TRACE_FAILURE_ONLY'not in text and 'dump_bytes'not in text;assert '--execute'not in m.command('test',Path('/tmp/not-run'));checks+=1
print(json.dumps({'passed':True,'checks':checks,'public_identity_negative_cases':5,'exact_prior_CAS_and_BC_function_bodies':7,'exact_prior_cold_lifecycle_helpers':3,'device_access':False}))
