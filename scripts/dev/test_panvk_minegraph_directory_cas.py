"""Exercise actual inventory/rename/restore/clean together with real-shaped paths.
The fake only models host filesystem/adb primitives; no transaction function replaced.
"""
from pathlib import Path
import copy,hashlib,importlib.util,itertools,json,tempfile
p=Path(__file__).with_name('run-panvk-minegraph-game.py');s=importlib.util.spec_from_file_location('controller',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class FS:
 def __init__(self):self.dirs=set();self.files={};self.stops=0;self.moves=[]
 def mkdir(self,p):
  self.dirs.add(p)
  for q in Path(p).parents:
   if str(q)!='.':self.dirs.add(str(q))
 def put(self,p,digest,text=''):
  self.mkdir(str(Path(p).parent));self.files[p]=(digest,text)
 def digest(self,p):return self.files[p][0]if p in self.files else None
 def stopped(self):self.stops+=1
 def mv(self,src,dst):
  assert src in self.dirs and dst not in self.dirs
  for p in sorted(list(self.dirs)):
   if p==src or p.startswith(src+'/'):self.dirs.remove(p);self.dirs.add(dst+p[len(src):])
  for p in list(self.files):
   if p.startswith(src+'/'):self.files[dst+p[len(src):]]=self.files.pop(p)
  self.moves.append((src,dst))
 def app(self,*a):
  if a[0]=='sh':
   script=a[2]
   if 'mv "$1" "$2"'in script:
    src,dst,pkg=a[-3:];assert pkg==m.PKG;self.mv(src,dst);return ''
   if '; rm "$1"'in script:
    p,h,pkg=a[-3:];assert pkg==m.PKG and self.digest(p)==h;del self.files[p];return ''
   if 'if test -L'in script or 'if test -d'in script:return 'DIR'if a[-1]in self.dirs else ''
   raise AssertionError('unhandled shell: '+str(a))
  if a[0]=='find':
   root,typ=a[1],a[-1];source=self.files if typ=='f'else self.dirs if typ=='d'else []
   return '\n'.join(sorted(p for p in source if p==root or p.startswith(root+'/')))
  if a[0]=='sha256sum':return '\n'.join(self.digest(p)+'  '+p for p in a[1:])
  if a[0]=='cat':return self.files[a[1]][1]
  if a[0]=='rm':assert a[1]in self.files;del self.files[a[1]];return ''
  if a[0]=='rmdir':
   p=a[1];assert p in self.dirs and not any(q.startswith(p+'/')for q in self.files)and not any(q.startswith(p+'/')for q in self.dirs);self.dirs.remove(p);return ''
  raise AssertionError('unhandled primitive: '+str(a))
with tempfile.TemporaryDirectory()as tmp:
 for presence in itertools.product([False,True],repeat=len(m.DIRS)):
  fs=FS();token='a'*32;ownerhash=hashlib.sha256((token+'\n').encode()).hexdigest();stage='files/runtime/.minegraph-game-'+token
  d={'stage':stage,'token':token,'owner_sha256':ownerhash,'transfer_pushed':False,'candidate_overlay':m.read(m.RESOURCE_READY)['deploy_files']}
  for i,(label,target)in enumerate(m.DIRS.items()):
   if presence[i]:fs.put(target+'/Diablo II/Original cache '+label+'.bin','c'*64)
  original_files=copy.deepcopy(fs.files);original_roots={label:m.inventory(fs,target)for label,target in m.DIRS.items()}
  fs.put(stage+'/'+m.OWNER,ownerhash,token);fs.put(m.LOCK+'/owner',ownerhash,token);fs.mkdir(stage+'/backup');fs.mkdir(stage+'/incoming')
  for label,target in m.DIRS.items():
   d['old_'+label]=original_roots[label];incoming=stage+'/incoming/'+label;fs.put(incoming+'/'+m.OWNER,ownerhash,token)
   if label=='driver':fs.put(incoming+'/libvulkan_panfrost.so',m.DRIVER_SHA)
   if label=='overlay':
    for n,h in d['candidate_overlay'].items():fs.put(incoming+'/'+n,h)
   if d['old_'+label]is not None:m.rename_dir(fs,target,stage+'/backup/'+label,d['old_'+label])
   m.rename_dir(fs,incoming,target,m.inventory(fs,incoming))
  fs.put(m.DIRS['driver']+'/shader-cache/Mesa Cache/compiled shader.bin','d'*64)
  fs.put(m.DIRS['temp']+'/Diablo II/GameLogs/D2261020.txt','e'*64)
  fs.put(m.DIRS['cache']+'/Android cache/generated data.bin','f'*64)
  m.restore(fs,Path(tmp)/'result.json',d);m.clean(fs,Path(tmp)/'result.json',d)
  assert d['original_directories_restored']and d['owned_staging_and_transfer_cleaned']
  assert fs.files==original_files and all(m.inventory(fs,target)==original_roots[label]for label,target in m.DIRS.items())
  assert stage not in fs.dirs and m.LOCK not in fs.dirs
print(json.dumps({'passed':True,'cases':16,'real_functions':['inventory','rename_dir','restore','clean'],'path_shape':'Diablo II/GameLogs/D2261020.txt; old/new nested cache with spaces','backup_publish_restore_cleanup':True,'device_access':False}))
