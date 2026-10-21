"""Two exact runtime / fixed mode gates plus byte-exact existing lifecycle; no device."""
from pathlib import Path
import ast,copy,hashlib,importlib.util,json,tempfile
P=Path(__file__).with_name('run-game.py');s=importlib.util.spec_from_file_location('m',P);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);R=m.R
config={'scope':m.SCOPE,'runtime_commit':m.COMMIT,'preferred_block_MiB':32,'scale_initial':.65,'dynamic_3d':False,'provider':'system','shared':False,'legacy_ASTC_copy':False,'serial':'10.10.10.44:34455','artifact_records_ready':False};count=0
for key,value in [('runtime_commit','bad'),('scope','wrong'),('preferred_block_MiB',16),('scale_initial',.75),('dynamic_3d',True),('provider','panvk'),('shared',True),('legacy_ASTC_copy',True),('serial','Q'),('artifact_records_ready',False)]:
 c=copy.deepcopy(config);c[key]=value
 try:m.proof(c)
 except RuntimeError:count+=1
 else:raise AssertionError('unsafe or incomplete input admitted '+key)
c=copy.deepcopy(config);c.update(runtime_commit='463bbfccd32bfa658a14c7492ebecde7731bca78',preferred_block_MiB=16,artifact_records_ready=False)
try:m.proof(c)
except RuntimeError as e:assert 'DRAFT'in str(e);count+=1
else:raise AssertionError('incomplete16MiB input can reach Device')
for commit,row in m.RUNTIMES.items():
 cmd=m.command(config['serial'],Path('/not-executed'),row['source']);assert cmd[1]==str(row['source']/'scripts/dev/android-game-launch.py')and cmd[cmd.index('--scale-3d')+1]=='0.65'and'--fixed-3d'in cmd and'--no-shared-guest-heap'in cmd and'--no-legacy-astc-copy'in cmd and'--native-resource-table'not in cmd;count+=1
original=(R/'.local/worktrees/mali-vma-single-foreground-v3/scripts/dev/mali-vma-paused-capture-v1/run-game.py').read_text();current=P.read_text()
def fn(text,name):return ast.dump(next(n for n in ast.parse(text).body if isinstance(n,ast.FunctionDef)and n.name==name),include_attributes=False)
for name in ['baseline','owned','showmap','original_save','vma_pause_capture','restore']:assert fn(original,name)==fn(current,name);count+=1
# Launch differs solely in selecting launcher/walk helper from the pinned runtime source.
normalized=current.replace("command(d['serial'],out,Path(p.get('runtime_source',SOURCE)))","command(d['serial'],out)").replace("str(Path(p.get('runtime_source',SOURCE))/'scripts/dev/android-game-walk-to.py')","str(SOURCE/'scripts/dev/android-game-walk-to.py')")
assert fn(original,'launch')==fn(normalized,'launch');count+=1
helper=P.parent.parent/'android-game-background-save.py';oldhelper=R/'.local/worktrees/mali-vma-single-foreground-v3/scripts/dev/android-game-background-save.py';assert helper.read_bytes()==oldhelper.read_bytes();count+=1
print(json.dumps({'passed':True,'checks':count,'unchanged_lifecycle_CAS_helpers':6,'launch_only_pinned_SOURCE_selection':True,'helper_byte_identical':True,'incomplete_candidate16_input_refused_before_Device':True,'device_access':False}))
