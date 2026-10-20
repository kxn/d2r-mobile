"""Fault-injection of actual directory restore; no device, runtime or timing test."""
import importlib.util,itertools,tempfile,json,copy
from pathlib import Path
p=Path(__file__).with_name('run-panvk-minegraph-game.py');s=importlib.util.spec_from_file_location('candidate',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Dev:
 def stopped(self):pass
class FilenameDev:
 def __init__(self,bad=None):self.bad=bad
 def app(self,*args):
  root='files/runtime/temp';names=[root+'/ordinary.bin',root+'/Diablo II/GameLogs/D2261016.txt',root+'/Diablo II/GameLogs/D2261020.txt']
  if args[0]=='sh':return 'DIR'
  if args[0]=='find':return '' if args[-1]=='l' else '\n'.join(names)
  assert args[0]=='sha256sum' and list(args[1:])==names
  rows=[('c'*64)+'  '+n for n in names]
  if self.bad=='digest':rows[1]='not-a-hash  '+names[1]
  if self.bad=='duplicate':rows[1]=rows[0]
  return '\n'.join(rows)
class CASDev:
 def __init__(self):self.dirs={'files/runtime/temp':{'Diablo II/GameLogs/D2261020.txt':'c'*64}};self.stops=0
 def stopped(self):self.stops+=1
 def app(self,*args):
  if args[0]=='sh':
   if 'mv "$1" "$2"' in args[2]:
    src,dst,pkg=args[-3:];assert pkg==m.PKG and src in self.dirs and dst not in self.dirs;self.dirs[dst]=self.dirs.pop(src);return ''
   path=args[-1];return 'DIR' if path in self.dirs else ''
  if args[0]=='find':
   path=args[1];return '' if args[-1]=='l' else '\n'.join(path+'/'+n for n in self.dirs[path])
  assert args[0]=='sha256sum'
  return '\n'.join(self.dirs[str(Path(n).parent.parent.parent)]['Diablo II/GameLogs/D2261020.txt']+'  '+n for n in args[1:])
cas=CASDev();before=m.inventory(cas,'files/runtime/temp')
m.rename_dir(cas,'files/runtime/temp','files/runtime/backup-temp',before)
m.rename_dir(cas,'files/runtime/backup-temp','files/runtime/temp',before)
assert m.inventory(cas,'files/runtime/temp')==before and cas.stops==2
# Actual inventory receives same-shaped real source filenames, not a stand-in parser.
parsed=m.inventory(FilenameDev(),'files/runtime/temp')
assert parsed=={'ordinary.bin':'c'*64,'Diablo II/GameLogs/D2261016.txt':'c'*64,'Diablo II/GameLogs/D2261020.txt':'c'*64}
for bad in ['digest','duplicate']:
 try:m.inventory(FilenameDev(bad),'files/runtime/temp')
 except RuntimeError:pass
 else:raise AssertionError('malformed SHA inventory accepted: '+bad)
n=0
with tempfile.TemporaryDirectory()as tmp:
 for presence in itertools.product([False,True],repeat=3):
  for published in range(4):
   d={'stage':'files/runtime/.minegraph-game-'+'a'*32,'token':'a'*32,'owner_sha256':'b'*64};v={};labels=list(m.DIRS)
   for i,label in enumerate(labels):
    old={'original-'+label:'c'*64}if presence[i]else None;d['old_'+label]=old
    new={m.OWNER:d['owner_sha256']}
    if label=='driver':new['libvulkan_panfrost.so']=m.DRIVER_SHA
    if i<published:
     if old is not None:v[d['stage']+'/backup/'+label]=old
     v[m.DIRS[label]]=new
    elif old is not None:v[m.DIRS[label]]=old
   def inv(dev,path):return copy.deepcopy(v.get(path))
   def move(dev,src,dst,expected):
    assert inv(dev,src)==expected and inv(dev,dst)is None
    v[dst]=v.pop(src)
   m.inventory=inv;m.rename_dir=move
   m.restore(Dev(),Path(tmp)/'result.json',d)
   assert d['original_directories_restored']and all(inv(None,target)==d['old_'+label]for label,target in m.DIRS.items())
   # Repeating recovery after interrupted caller bookkeeping is safe.
   m.restore(Dev(),Path(tmp)/'result.json',d);n+=1
 d={'stage':'files/runtime/.minegraph-game-'+'a'*32,'owner_sha256':'b'*64,**{'old_'+k:{'original':'c'*64}for k in m.DIRS}}
 for tamper in ['backup','owner','driver']:
  v={}
  for label,target in m.DIRS.items():
   v[d['stage']+'/backup/'+label]=d['old_'+label];v[target]={m.OWNER:'b'*64}
   if label=='driver':v[target]['libvulkan_panfrost.so']=m.DRIVER_SHA
  if tamper=='backup':v[d['stage']+'/backup/cache']={'changed':'d'*64}
  if tamper=='owner':v['cache'][m.OWNER]='d'*64
  if tamper=='driver':v[m.DIRS['driver']]['libvulkan_panfrost.so']='d'*64
  try:m.restore(Dev(),Path(tmp)/'result.json',d)
  except RuntimeError:pass
  else:raise AssertionError('unowned change accepted: '+tamper)
  n+=1
 # Provenance refusal occurs before any Device construction/ADB.
 good=m.READY_SHA;m.READY_SHA='0'*64
 try:m.proof()
 except RuntimeError:pass
 else:raise AssertionError('corrupt driver receipt accepted')
 m.READY_SHA=good;n+=1
print(json.dumps({'passed':True,'restore_and_idempotence_cases':32,'tamper_rejections':3,'driver_receipt_negative_control':True,'cases':n+4,'actual_inventory_regression_cases':3,'actual_inventory_and_CAS_roundtrip':True,'actual_filename_inventory_regression':True,'malformed_digest_and_duplicate_rejected':True,'device_access':False}))
