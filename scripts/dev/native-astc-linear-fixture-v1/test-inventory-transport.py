#!/usr/bin/env python3
"""Actual quoted exec-out strings through local sh/run-as stub; no ADB/device."""
from pathlib import Path
import argparse,ast,hashlib,json,os,subprocess,tempfile
import readonly_inventory as inventory
P=argparse.ArgumentParser();P.add_argument('--output',type=Path,required=True);a=P.parse_args();a.output.mkdir(exist_ok=False,parents=True)
checks=0;records=[]
def require(c,m):
 global checks
 checks+=1
 if not c:raise AssertionError(m)
with tempfile.TemporaryDirectory(prefix='native-linear-inventory-')as tmp:
 root=Path(tmp);binpath=root/'bin';binpath.mkdir();runas=binpath/'run-as';runas.write_text('#!/bin/sh\ntest "$1" = "com.d2rmobile.nativeport" || exit 91\nshift\nexec "$@"\n');runas.chmod(0o755)
 env=os.environ.copy();env['PATH']=str(binpath)+':'+env['PATH']
 class Device:
  def __init__(self):self.corrupt=False
  def app(self,*args):
   argv=inventory.app_command('com.d2rmobile.nativeport',args)
   p=subprocess.run(['sh','-c',argv[1]],cwd=root,env=env,capture_output=True,text=True,timeout=5)
   records.append({'service_argv':argv,'stdout':p.stdout,'stderr':p.stderr,'exit':p.returncode})
   if p.returncode:raise RuntimeError('remote analogue command failed')
   if self.corrupt and args[0]=='sha256sum':return 'not-a-sha  '+args[1]
   return p.stdout.strip()
 d=Device();folder='files/runtime/temp/Diablo II';target=root/folder;target.mkdir(parents=True);expected={}
 names=['normal.bin','has spaces.bin',"has apostrophe'.bin",'literal $(touch SHOULD_NOT_EXIST).bin']+['entry-%03d.bin'%n for n in range(257)]
 for n,name in enumerate(names):
  data=('payload-%d'%n).encode();(target/name).write_bytes(data);expected[name]=hashlib.sha256(data).hexdigest()
 got=inventory.inventory(d,folder);require(got==expected,'full names+all hashes preserved');require(not(root/'SHOULD_NOT_EXIST').exists(),'literal command substitution not executed')
 commands=[x for x in records if x['service_argv'][1].startswith('run-as com.d2rmobile.nativeport sha256sum')]
 require(len(commands)==3,'hash requests chunked128/128/5')
 import shlex
 require(all(len(shlex.split(x['service_argv'][1]))-3<=128 for x in commands),'each request <=128files')
 require(inventory.inventory(d,'files/missing')is None,'missing distinct from empty')
 (root/'cache').mkdir();require(inventory.inventory(d,'cache')=={},'empty cache exact')
 (root/'not-a-directory').write_text('x')
 try:inventory.inventory(d,'not-a-directory')
 except RuntimeError:checks+=1
 else:raise AssertionError('file accepted as directory')
 (root/'bad-link').symlink_to(target)
 try:inventory.inventory(d,'bad-link')
 except RuntimeError:checks+=1
 else:raise AssertionError('directory symlink accepted')
 (target/'child-link').symlink_to(target/'normal.bin')
 try:inventory.inventory(d,folder)
 except AssertionError:checks+=1
 else:raise AssertionError('child symlink accepted')
 (target/'child-link').unlink();d.corrupt=True
 try:inventory.inventory(d,folder)
 except AssertionError:checks+=1
 else:raise AssertionError('invalid hash accepted')
 # Reproduce the actual frozen failed caller argv with its pre-quoted code.
 old=Path('/home/dl/d2r-mobile/.local/worktrees/native-astc-linear-gpu-fixture-v1/scripts/dev/native-astc-linear-fixture-v1/run-owned.py');tree=ast.parse(old.read_text());func=next(n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name=='snapshot');assign=next(n for n in ast.walk(func)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='code'for t in n.targets));code=ast.literal_eval(assign.value)
 require(code.count(chr(92))==1,'actual frozen code has one slash; delimiter alone is not rootcause')
 # AOSP client/commandline.cpp exec-out appends argv[1] verbatim, then
 # escape_arg() for each later argv. Old caller pre-quoted its code once.
 old_argv=['exec-out','run-as','com.d2rmobile.nativeport','sh','-c',shlex.quote(code),'sh',shlex.quote(folder)]
 def adb_exec_service(argv):
  args=argv[1:];return args[0]+''.join(' '+shlex.quote(v)for v in args[1:])
 old_remote=adb_exec_service(old_argv)
 raw_code_argv=['exec-out','run-as','com.d2rmobile.nativeport','sh','-c',code,'sh',folder]
 corrected_multarg=adb_exec_service(raw_code_argv)
 raw_run=subprocess.run(['sh','-c',corrected_multarg],cwd=root,env=env,capture_output=True,text=True,timeout=5)
 require(raw_run.returncode==0 and len(raw_run.stdout.splitlines())==len(names),'same find code works when not prequoted')
 p=subprocess.run(['sh','-c',old_remote],cwd=root,env=env,capture_output=True,text=True,timeout=5)
 require(p.returncode!=0,'old exact transport reproducibly fails before hashes')
 old_result={'frozen_controller':str(old),'code_repr':repr(code),'old_exec_out_argv':old_argv,'POSIX_equivalent_AOSP_remote_service_command':old_remote,'corrected_raw_multarg_service':corrected_multarg,'AOSP_source_URL':'https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/client/commandline.cpp','AOSP_source_blob':'5cd3ab66dd0a7ba0173b01f954fbe76c21269483','AOSP_lines':[1822,1833],'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr}
(a.output/'actual-command-records.json').write_text(json.dumps(records,indent=2)+'\n');(a.output/'old-command-reproduction.json').write_text(json.dumps(old_result,indent=2)+'\n');result={'passed':True,'checks':checks,'file_names_hashed':len(names),'actual_shell_transport_examples':records[:5],'old_command_failed_as_expected':old_result,'ADB_used':False,'GPU_used':False,'boundary':'Local sh + run-as exec stub exercise actual complete remote command strings and filesystem tools. Not an Android device result.'};(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items()if k not in('actual_shell_transport_examples','old_command_failed_as_expected')}))
