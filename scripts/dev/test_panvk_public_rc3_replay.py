"""Finite source-only identity/trace-origin/directory checks. Never calls ADB."""
import ast,copy,hashlib,json,re,shlex,tempfile,zipfile
from pathlib import Path
SOURCE=Path(__file__).with_name('run-panvk-public-rc3-gfxr-replay.py')
R=Path('/home/dl/d2r-mobile');tree=ast.parse(SOURCE.read_text())
space={'Path':Path,'json':json,'re':re,'hashlib':hashlib,'zipfile':zipfile,'shlex':shlex}
for node in tree.body:
 if isinstance(node,ast.FunctionDef)and node.name in ('verified_startup_capture','verify_baseline','app','inventory'):
  exec(compile(ast.Module(body=[node],type_ignores=[]),str(SOURCE),'exec'),space)
checks=[]
def expect_reject(name,call):
 try:call()
 except (AssertionError,RuntimeError):checks.append(name);return
 raise AssertionError('Accepted negative case: '+name)
H=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
capture=R/'.local/logs/panvk-original-untrimmed-capture-v2-mali-v1/startup-prefix.gfxr'
origin=R/'.local/logs/panvk-original-untrimmed-capture-v2-mali-v1/result.json'
q=json.loads(origin.read_text());origin_sha=H(origin);capture_sha=H(capture)
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp)/'origin.json'
 space['sha']=lambda n:origin_sha if Path(n)==p else capture_sha if Path(n)==capture else H(n)
 p.write_text(json.dumps(q));assert space['verified_startup_capture'](p,capture)['driver'].startswith('c5ec');checks.append('original full prefix accepted')
 for name,change in [('capture origin rewritten as public RC3',{'driver':'b46b9f0416f207a05482349dffb37b6f3d84c20762151c623774831ce3939445'}),('midstream trim',{'untrimmed_prefix_verified':False}),('missing createInstance prefix',{'first_recorded_function':'vkCreateBuffer'})]:
  v=copy.deepcopy(q);v.update(change);p.write_text(json.dumps(v));expect_reject(name,lambda:space['verified_startup_capture'](p,capture))
 baseline=R/'.local/logs/mali-HUD55-public-RC3-selected35-baseline-root.json';b=json.loads(baseline.read_text());p=Path(tmp)/'baseline.json';p.write_text(json.dumps(b));pinned=H(baseline)
 space['sha']=lambda n:pinned if Path(n)==p else H(n)
 apk=R/'.local/build/debug-overlay-scale-55a1112-canonical-source-final/canonical.apk'
 _,values=space['verify_baseline'](p,'10.10.10.44:34455',apk);assert len(values)==35;checks.append('HUD55 selected35 + actual saved/stopped root receipt accepted')
 for name,change in [('wrong production host',{'source_revision':'old022'}),('production not stopped',{'original_game_process_absent':False}),('selected35 truncated',{'original_modules_resources_private_driver_hashes':{}})]:
  v=copy.deepcopy(b);v.update(change);p.write_text(json.dumps(v));expect_reject(name,lambda:space['verify_baseline'](p,'10.10.10.44:34455',apk))
commands=[];space['original']='com.d2rmobile.nativeport';space['ad']=lambda *args,**kw:commands.append((args,kw))or''
space['app']('sha256sum','files/runtime/romfs_overlay/Diablo II.data')
assert shlex.split(commands[0][0][1])==['run-as','com.d2rmobile.nativeport','sha256sum','files/runtime/romfs_overlay/Diablo II.data'];checks.append('run-as uses original quoted CAS convention for spaces')
root='files/runtime/romfs_overlay';names=[root+'/Diablo II.data']+[root+'/file'+str(n)for n in range(129)];batches=[]
def fake_app(*args,**kw):
 if args[0]=='sh':return'DIR'
 if args[:2]==('find',root):return '' if args[-1]=='l' else '\n'.join(names)
 if args[0]=='sha256sum':
  batches.append(args[1:]);return'\n'.join('a'*64+'  '+n for n in args[1:])
 raise AssertionError(args)
space['app']=fake_app;assert len(space['inventory'](root))==130 and list(map(len,batches))==[128,2];checks.append('entire130-file inventory retains spaces and bounded128 batches')
space['app']=lambda *a,**k:'DIR' if a[0]=='sh' else root+'/symlink';expect_reject('symlink in protected directory',lambda:space['inventory'](root))
space['app']=lambda *a,**k:'';assert space['inventory'](root)is None;checks.append('absent directory preserved as None')
space['app']=lambda *a,**k:'DIR' if a[0]=='sh' else'';assert space['inventory'](root)=={};checks.append('empty directory distinct from absent')
# Original ownership cleanup code is reused without edits, not weakened here.
legacy=R/'.local/worktrees/panvk-gfxr-startup-sync-replay-v1/scripts/dev/run-panvk-gfxr-replay-fixture.py';old=ast.parse(legacy.read_text())
for name in ['observe','stop','exact','install']:
 left=next(n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name==name)
 right=next(n for n in old.body if isinstance(n,ast.FunctionDef)and n.name==name)
 assert ast.dump(left,include_attributes=False)==ast.dump(right,include_attributes=False)
checks.append('original first-PID/sticky ambiguity/owned stop/install function bodies exactly unchanged')
print(json.dumps({'checks':len(checks),'passed':checks,'ADB_calls':0,'device_access':False},indent=2))
