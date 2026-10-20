"""Actual owner/CAS restore+cleanup failure controls, host only, noADB."""
from pathlib import Path
import ast,copy,hashlib,importlib.util,itertools,json,tempfile
P=Path(__file__).with_name('run-game.py');spec=importlib.util.spec_from_file_location('m',P);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
# Reuse the already-tested fake filesystem primitives, not alternate restore logic.
src=Path('/home/dl/d2r-mobile/.local/worktrees/panvk-minegraph-static-hardware-game-v4/scripts/dev/test_panvk_minegraph_directory_cas.py').read_text();node=next(n for n in ast.parse(src).body if isinstance(n,ast.ClassDef)and n.name=='FS');exec(compile(ast.Module(body=[node],type_ignores=[]),'accepted_FS','exec'))
class Dev(FS):
 def adb(self,*a):
  if a[:3]==('shell','pm','path'):return 'package:/original.apk'
  if a[:2]==('shell','sha256sum'):return m.base.APK+'  /original.apk'
  raise AssertionError('unexpected ADB primitive '+str(a))
count=0;original_baseline=m.baseline;m.baseline=lambda dev,p:{'unchanged':True}
with tempfile.TemporaryDirectory()as tmp:
 for presence in itertools.product([False,True],repeat=4):
  for published in range(5):
   fs=Dev();token='a'*32;ownerhash=hashlib.sha256((token+'\n').encode()).hexdigest();stage='files/runtime/.stock-mali-773-'+token;p={'modules':{'main.patched.bin':'d'*64},'APK':{'sha256':'e'*64}};d={'stage':stage,'token':token,'owner_sha256':ownerhash,'transfer_pushed':False,'baseline_before':{'unchanged':True},'old_dirs':{}}
   for i,(label,target)in enumerate(m.DIRS.items()):
    if presence[i]:fs.put(target+'/Diablo II/Original cache '+label+'.bin','c'*64)
   originals=copy.deepcopy(fs.files)
   fs.put(stage+'/'+m.OWNER,ownerhash,token);fs.put(m.LOCK+'/owner',ownerhash,token);fs.mkdir(stage+'/backup');fs.mkdir(stage+'/incoming')
   for i,(label,target)in enumerate(m.DIRS.items()):
    d['old_dirs'][label]=m.inventory(fs,target);incoming=stage+'/incoming/'+label;fs.put(incoming+'/'+m.OWNER,ownerhash,token)
    if label=='modules':fs.put(incoming+'/main.patched.bin','d'*64)
    if i<published:
     if d['old_dirs'][label]is not None:m.rename_dir(fs,target,stage+'/backup/'+label,d['old_dirs'][label])
     m.rename_dir(fs,incoming,target,m.inventory(fs,incoming))
   if published>1:fs.put(m.DIRS['temp']+'/Diablo II/GameLogs/new log.txt','f'*64)
   m.restore(fs,d,p,Path(tmp));assert d['original_restored']and d['cleanup_complete'];assert fs.files==originals;assert stage not in fs.dirs and m.LOCK not in fs.dirs;count+=1
m.baseline=original_baseline
# Strict no opaque/native files and no changed raw modules inside owned directories.
for label,inv in [('native',{m.OWNER:'b'*64,'native-layouts.bin':'f'*64}),('modules',{m.OWNER:'b'*64,'main.patched.bin':'f'*64}),('temp',{m.OWNER:'f'*64})]:
 try:m.owned(inv,{'owner_sha256':'b'*64},label,{'modules':{'main.patched.bin':'d'*64}})
 except RuntimeError:count+=1
 else:raise AssertionError('unowned/native/mutated modules accepted')
cmd=m.command('10.10.10.44:34455',Path('/new-report'));assert '--fixed-3d'not in cmd and '--scale-3d'not in cmd and '--no-shared-guest-heap'in cmd and cmd[cmd.index('--vulkan-provider')+1]=='system'and '--custom-vulkan-driver'not in cmd and '--native-resource-table'not in cmd;count+=1
config={'runtime_commit':m.COMMIT,'scope':'stock-Mali773-fullASTC4-lightweight-camp-memory-control','scale_initial':.75,'dynamic_3d':True,'provider':'system','shared':False,'legacy_ASTC_copy':False,'serial':'10.10.10.44:34455'}
for key,value in [('runtime_commit','bad'),('scope','wrong'),('scale_initial',.60),('dynamic_3d',False),('provider','adreno'),('shared',True),('legacy_ASTC_copy',True),('serial','Q')]:
 c=copy.deepcopy(config);c[key]=value
 try:m.proof(c)
 except RuntimeError:count+=1
 else:raise AssertionError('unsafe config accepted '+key)
# Required lightweight control does not start new CPU/GPU captures.
text=P.read_text();assert 'capture-official-cpu.py'not in text and 'capture-mali-public-api.py'not in text;count+=1
assert 'thermalservice'not in text and 'sysfs'not in text;count+=1
print(json.dumps({'passed':True,'actual_CAS_restore_cleanup_cases':80,'other_owner_mode_controls':count-80,'cases':count,'original_progress_not_mutated_by_test':True,'device_access':False}))
