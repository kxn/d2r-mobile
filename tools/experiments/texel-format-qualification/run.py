#!/usr/bin/env python3
"""Orchestrate the frozen owned APK. Default: print plan, no ADB."""
from pathlib import Path
import argparse,hashlib,json,shlex,subprocess,time,traceback
D=Path(__file__).resolve().parent;R=D.parents[2];P='com.d2rmobile.texelformatprobe';ACT=P+'/'+P+'.ProbeActivity';ADB=R/'.local/android-sdk/platform-tools/adb'

def require(condition, reason):
 if not condition:raise ValueError(reason)


def parse_events(text):
 """Only organize existing JSON output; partial writes are not completion."""
 out=[]
 for line in text.splitlines():
  try:item=json.loads(line)
  except ValueError:continue
  if isinstance(item,dict) and item.get('event'):out.append(item)
 return out


def validate_ready(ready):
 require(ready['package']==P and ready['activity']==P+'.ProbeActivity' and ready.get('no_game_modules') is True,
         'only the isolated fixture package is permitted')
 require(set(ready['native_libraries'])=={'libtexelformatprobe.so','libadrenotools.so','libfile_redirect_hook.so',
         'libgsl_alloc_hook.so','libhook_impl.so','libmain_hook.so'},'unexpected native payload; main runtime is forbidden')
 require(len(ready.get('source_fingerprint',''))==64,'new build/source fingerprint required; no historical identity reuse')


def qualify_output(text,pid,ready,inject_error=False,full=False):
 ev=parse_events(text)
 def one(kind):
  values=[item for item in ev if item['event']==kind]
  require(len(values)==1,'missing/ambiguous '+kind)
  return values[0]
 entry,context,terminal,final=[one(k) for k in ('app_entry','context','app_terminal','result')]
 require(type(entry.get('pid')) is int and entry['pid']==pid and type(entry.get('uid')) is int and entry['uid']>=10000,
         'ordinary app UID and exact current PID required')
 require(context.get('source_fingerprint')==ready['source_fingerprint'],'rebuilt probe/source identity mismatch')
 require(context.get('format')=='R32G32B32A32_SFLOAT' and context.get('comparison_stage')=='fragment',
         'wrong format/stage contract')
 maps=[e for e in ev if e['event']=='actual_driver_map']
 require(any('/'+P+'/' in e.get('line','') and '/files/runtime/driver/libvulkan_freedreno.so' in e['line'] for e in maps),
         'actual map is not the unique private Turnip driver')
 fields=('tested_texels','tested_components','mismatch_texels','mismatched_components','source_mismatch_texels','coverage_errors')
 require(all(type(final.get(k)) is int and final[k]>=0 for k in fields),'missing/invalid exact GPU counters')
 require(final['tested_components']==4*final['tested_texels'],'component coverage disagrees')
 require(final['source_mismatch_texels']==0 and final['coverage_errors']==0,'source/coverage failure is not candidate equivalence')
 require(final['mismatch_texels']<=final['tested_texels'] and final['mismatched_components']<=4*final['mismatch_texels'],
         'impossible mismatch counters')
 require(type(terminal.get('rc')) is int,'invalid terminal code')
 if inject_error:
  require(terminal['rc']==3 and final['status']=='FAIL' and final['mismatch_texels']>0,'negative control must actually fail')
 elif final['status']=='PASS':
  require(terminal['rc']==0 and final['mismatch_texels']==0 and final['mismatched_components']==0,'PASS contains a mismatch')
 else:require(terminal['rc']==3 and final['status']=='FAIL' and final['mismatch_texels']>0,'actual comparator outcome required')
 if full and final['status']=='PASS':
  require(final.get('full_domain_completed') is True and final['tested_texels']==1<<32,'incomplete full-domain proof')
 return dict(context=context,entry=entry,actual_driver_maps=maps,terminal=terminal,result=final,
             comparison_passed=final['status']=='PASS',negative_control_passed=bool(inject_error))


def main():
 a=argparse.ArgumentParser();a.add_argument('label');a.add_argument('--ready',type=Path,required=True);a.add_argument('--log-root',type=Path,default=R/'.local/logs/texel-format-qualification');a.add_argument('--serial');a.add_argument('--conversion',choices=['raw','quiet'],default='raw');a.add_argument('--chunk-pow',type=int,choices=range(7,21),default=12);a.add_argument('--chunks',type=int,default=1);a.add_argument('--start-chunk',type=int,default=0);a.add_argument('--full',action='store_true');a.add_argument('--inject-error',action='store_true');a.add_argument('--install',action='store_true');a.add_argument('--execute',action='store_true');a.add_argument('--timeout',type=int,default=120);x=a.parse_args()
 assert x.label and all(c.isalnum()or c in '-_'for c in x.label),'safe unique label required'
 assert x.chunks>0 and x.start_chunk>=0 and (not x.full or x.start_chunk==0)
 assert not(x.full and x.inject_error),'negative full sweep is unnecessary'
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 M=json.loads(x.ready.read_text());validate_ready(M)
 for key in ['APK','driver']:assert sha(Path(M[key]['path']))==M[key]['sha256']
 launch=['am','start','-W','-n',ACT,'--ez','run_probe','true','--es','conversion',x.conversion,'--ei','chunk_pow',str(x.chunk_pow),'--el','chunks',str(x.chunks),'--el','start_chunk',str(x.start_chunk),'--ez','inject_error',str(x.inject_error).lower(),'--ez','full',str(x.full).lower()]
 plan={'status':'PLAN_ONLY','serial':x.serial,'package':P,'APK':M['APK'],'driver':M['driver'],'launch':launch,'requires_queue_ownership_and_no_game_PID':True,'install':x.install,'no_main_package_or_profile_or_resources_changed':True,'expected_negative':x.inject_error,'not_performance_test':True}
 if not x.execute:print(json.dumps(plan,indent=2));raise SystemExit(0)
 assert x.serial,'--execute requires exact Q735 serial and explicit parent queue ownership'
 O=x.log_root/'actual'/x.label;O.mkdir(parents=True,exist_ok=False);commands=[];result={'plan':plan,'status':'STARTED','controller_sha256':sha(Path(__file__)),'ready_sha256':sha(x.ready)}
 def call(args,*,input=None,check=True,timeout=60):
  cmd=[str(ADB),'-s',x.serial,*map(str,args)];commands.append(cmd);p=subprocess.run(cmd,input=input,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
  if check and p.returncode:raise RuntimeError(f'command failed ({p.returncode}): {cmd}: '+p.stdout.decode(errors='replace')[-2000:])
  return p
 def shell(args,**kw):return call(['shell',shlex.join(list(map(str,args)))],**kw)
 def readlog():return shell(['run-as',P,'cat','files/runtime/probe-output.log'],check=False).stdout.decode(errors='replace')
 own_started=False
 try:
  assert not shell(['pidof','com.d2rmobile.nativeport'],check=False).stdout.strip(),'main game active: preserve it; no test started'
  existing=shell(['pidof',P],check=False).stdout.decode().strip()
  if existing:
   old=parse_events(readlog());assert any(e.get('event')=='app_terminal'and str(e.get('pid',existing))==existing for e in old),'owned probe process has no terminal; do not interrupt'
   shell(['am','force-stop',P]);assert not shell(['pidof',P],check=False).stdout.strip()
  if x.install:
   p=call(['install','--no-incremental',M['APK']['path']],timeout=120);(O/'install.log').write_bytes(p.stdout)
  paths=shell(['pm','path',P]).stdout.decode().splitlines();base=next(v[len('package:'):]for v in paths if v.startswith('package:')and v.endswith('/base.apk'))
  installed=shell(['sha256sum',base]).stdout.decode().split()[0];assert installed==M['APK']['sha256'];result['installed_APK_sha256']=installed
  shell(['run-as',P,'mkdir','-p','files/runtime/driver','files/runtime/shader-cache'])
  driver_remote='files/runtime/driver/libvulkan_freedreno.so';before=shell(['run-as',P,'sha256sum',driver_remote],check=False).stdout.decode().split()
  if not before or before[0]!=M['driver']['sha256']:
   shell(['run-as',P,'sh','-c','cat > '+driver_remote],input=Path(M['driver']['path']).read_bytes())
  assert shell(['run-as',P,'sha256sum',driver_remote]).stdout.decode().split()[0]==M['driver']['sha256']
  (O/'launch.txt').write_bytes(shell(launch).stdout);own_started=True
  pid=shell(['pidof',P]).stdout.decode().strip();assert pid.isdigit(),'expected exactly one owned probe PID';result['PID']=int(pid)
  deadline=time.monotonic()+x.timeout;raw='';ev=[]
  while True:
   raw=readlog();ev=parse_events(raw);entries=[e for e in ev if e['event']=='app_entry'and e.get('pid')==int(pid)]
   if entries and any(e['event']=='app_terminal'for e in ev):break
   if not shell(['pidof',P],check=False).stdout.strip():raise RuntimeError('owned probe exited before terminal')
   if time.monotonic()>deadline:raise RuntimeError('owned probe liveness timeout; stop once, no retries')
   time.sleep(.5)
  (O/'probe-output.log').write_text(raw)
  qualified=qualify_output(raw,int(pid),M,x.inject_error,x.full)
  assert shell(['run-as',P,'sha256sum',driver_remote]).stdout.decode().split()[0]==M['driver']['sha256']
  result.update(status='COMPLETED',**qualified)
 except Exception as e:
  result.update(status='FAILED',error=repr(e),traceback=traceback.format_exc());(O/'probe-output.log').write_text(readlog()if own_started else '')
  result['crash_buffer']=shell(['logcat','-b','crash','-d','-t','200'],check=False).stdout.decode(errors='replace');raise
 finally:
  if own_started:
   shell(['am','force-stop',P],check=False);result['owned_process_after_stop']=shell(['pidof',P],check=False).stdout.decode().strip()
  result['commands']=commands;(O/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)


if __name__ == '__main__':
 main()
