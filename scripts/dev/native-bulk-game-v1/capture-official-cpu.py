#!/usr/bin/env python3
"""Reuse original joystick + official Simpleperf99/stat, optional existing Perfetto helper."""
from pathlib import Path
import subprocess as S,json,sys,shlex,hashlib,os,time
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport';serial,pid,mode,folder,window_arg,trace_arg=sys.argv[1:];assert trace_arg in ('0','1');trace=trace_arg=='1';assert serial=='10CG570J59001U1'and pid.isdecimal()and mode in ('0','1');shared=mode=='1';window=int(window_arg);assert shared and window in (0,1048576,16777216);D=Path(folder).resolve();assert D.is_relative_to(R/'.local/logs');B=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',serial];phase={'owned_PID':int(pid),'shared_requested':shared,'scope':'Official8s cpu-clock:user99Hz DWARF16384, originalgamejoystick, no kernel samples/no custom sampler/timer','stages':[],'pre_record_cache_states':[],'window_bytes':window,'wait_wall_not_measured':not trace}
def ad(*a):return S.check_output(B+list(a),text=True,timeout=35).strip()
def exact():assert ad('shell','pidof '+PKG+' || true')==pid,'not exact original owned PID'
def app(*a):return ad('shell',shlex.join(['run-as',PKG,*a]))
def endpoint(n):
 exact();v=json.loads(app('tail','-n','1','files/runtime/game-state.jsonl'));assert v['valid']and v['world_input_ready']and v['area']==1 and v['guest_heap']['shared']is shared and v['scale_3d']==.60 and v['pool_windows']['window_bytes']==window and v['game_output']==v['surface_output']=={'width':2750,'height':1260};assert not v['world_freeze']['host_frozen']and not v['runtime_extrap']['enabled'];(D/(n+'.json')).write_text(json.dumps(v,indent=2)+'\n');return v
def save():(D/'official-cpu-capture.json').write_text(json.dumps(phase,indent=2)+'\n')
exact();assert ad('shell','sha256sum','/system/bin/simpleperf').split()[0]=='b868bf7d7aaffec117c5c758b594594d71a80de35b1e5b48a10b2f2eccae239c';phase['tool_sha256']='b868bf7d7aaffec117c5c758b594594d71a80de35b1e5b48a10b2f2eccae239c';save()
env=os.environ.copy();env['PATH']=str(R/'.local/android-sdk/platform-tools')+':'+env['PATH']
phase['original_route']='Already completed once by parent matched6217 transaction; no extra walk here';save()
# Bounded existing game-state readiness observation, not timing instrumentation.
previous=None;stable=0;last_tick=None
for attempt in range(30):
 v=endpoint('cache-ready-'+str(attempt));key=(v['pipeline_prepare']['pipelines'],v['uniform_specialization']['variants'])
 phase['pre_record_cache_states'].append({'time_ns':v['time_ns'],'PSO':key[0],'variants':key[1]});save()
 if v['tick']!=last_tick:stable=stable+1 if key==previous else 1;previous=key;last_tick=v['tick']
 if stable>=4:break
 time.sleep(.4)
assert stable>=4,'PSO/variant readiness never stabilized'
assert endpoint('before-tools')['frame_pacing']['target_fps']==60,'Target is not60 before tools; preserve run, no hot policy changes'
(D/'thread-names-before.txt').write_text(app('sh','-c','for t in /proc/"$1"/task/*; do printf "%s\\t" "${t##*/}"; cat "$t/comm"; done','sh',pid))
maps=app('cat','/proc/'+pid+'/maps');(D/'maps.txt').write_text(maps);assert not any(x in maps.lower()for x in ['renderdoc','libvklayer_khronos_validation','liblwi','libvklayer_sdp'])
for kind in ('stat','record'):
 first=endpoint('before-'+kind)
 token='native-bulk-bc-cpucost-v1-'+(('window'+str(window))if window else'whole')+'-'+kind;remote='files/runtime/profiles/'+token+('.data'if kind=='record'else'.csv');remotepid='files/runtime/profiles/'+token+'.pid';prop=ad('shell','getprop','security.perf_harden');assert prop in ('0','1');assert app('sh','-c','if test -e "$1" || test -e "$2"; then echo EXISTS; fi','sh',remote,remotepid)=='';profiles_existed=app('sh','-c','if test -d files/runtime/profiles; then echo DIR; fi')=='DIR';app('mkdir','-p','files/runtime/profiles')
 command=(['/system/bin/simpleperf','record','--in-app','-p',pid,'--duration','8','-e','cpu-clock:u','-f','99','--call-graph','dwarf,16384','--post-unwind=yes','--no-callchain-joiner','--keep-failed-unwinding-result','-o',remote] if kind=='record' else ['/system/bin/simpleperf','stat','--csv','--per-thread','--no-inherit','--monitor-new-thread','-p',pid,'--duration','8','-e','task-clock,cpu-cycles:u,cpu-cycles:k,minor-faults,major-faults,context-switches,cpu-migrations','-o',remote]);phase[kind+'_official_command']=command;phase['perf_harden_before']=prop;save()
 try:
  if prop!='0':ad('shell','setprop','security.perf_harden','0')
  shell='echo $$ > '+shlex.quote(remotepid)+'; exec '+shlex.join(command)
  with(D/(kind+'-simpleperf-console.log')).open('w')as f:S.run(B+['shell',shlex.join(['run-as',PKG,'sh','-c',shell])],check=True,stdout=f,stderr=S.STDOUT,timeout=35)
  exact();raw=S.run(B+['exec-out','run-as',PKG,'cat',remote],capture_output=True,check=True,timeout=35).stdout
  if kind=='record':assert raw[:8]==b'PERFILE2'and len(raw)>1024;leaf='official-simpleperf.data'
  else:assert b'Total test time'in raw;leaf='official-per-thread-stat.csv'
  (D/leaf).write_bytes(raw);phase[kind+'_sha256']=hashlib.sha256(raw).hexdigest();phase['stages'].append('official8s-'+kind+'-complete')
 finally:
  spid=app('sh','-c','if test -f "$1"; then cat "$1"; fi','sh',remotepid)
  if spid.isdecimal():
   cmd=app('sh','-c','if test -f /proc/"$1"/cmdline; then cat /proc/"$1"/cmdline; fi','sh',spid)
   if '/system/bin/simpleperf'in cmd and token in cmd:app('kill','-TERM',spid)
  for n in (remotepid,remote):
   present=app('sh','-c','if test -f "$1"; then sha256sum "$1"; fi','sh',n)
   if present:
    h=present.split()[0];app('sh','-c','set -eu; test "$(sha256sum "$1" | cut -d" " -f1)" = "$2"; rm "$1"','sh',n,h)
  if not profiles_existed:app('rmdir','files/runtime/profiles')
  if prop!='0':ad('shell','setprop','security.perf_harden',prop)
  assert ad('shell','getprop','security.perf_harden')==prop
 last=endpoint('after-'+kind)
 phase[kind+'_bracket']={'target':[first['frame_pacing']['target_fps'],last['frame_pacing']['target_fps']],'PSO':[first['pipeline_prepare']['pipelines'],last['pipeline_prepare']['pipelines']],'variants':[first['uniform_specialization']['variants'],last['uniform_specialization']['variants']],'pool_windows_before':first['pool_windows'],'pool_windows_after':last['pool_windows'],'task_work_before':{k:first.get(k)for k in ['tick','draws','vk_draws','shader_present','guest_fences','pool_copy_accounting','present_worker']},'task_work_after':{k:last.get(k)for k in ['tick','draws','vk_draws','shader_present','guest_fences','pool_copy_accounting','present_worker']},'actual_endpoint_wall_ns':last['time_ns']-first['time_ns'],'counter_scope':'ADB bracket encloses official8s; selected is not presented/frame denominator'}
 b=phase[kind+'_bracket'];b['stable_target60_PSO_variants']=b['target']==[60,60] and b['PSO'][0]==b['PSO'][1] and b['variants'][0]==b['variants'][1]
 save()
if trace:
 first=endpoint('before-scheduler')
 assert first['frame_pacing']['target_fps']==60,'Target not60 before optional scheduler; no hot override'
 command=[sys.executable,str(R/'scripts/dev/capture-android-scheduler.py'),'--serial',serial,'--seconds','8','--buffer-mb','64','--app-markers','--clean-atrace-print','--frame-timeline','--output',str(D/'completion-scheduler.pftrace')]
 phase['official_scheduler_command']=command;save()
 with(D/'scheduler-console.log').open('w')as f:S.run(command,env=env,stdout=f,stderr=S.STDOUT,check=True,timeout=50)
 exact();last=endpoint('after-scheduler');capture=json.loads((D/'completion-scheduler.json').read_text())
 assert capture['pid']==int(pid)and capture['shader_chain']=='9507875007c6'and capture['seconds_requested']==8
 assert capture['app_markers_requested']and capture['clean_atrace_print_requested']and capture['frame_timeline_requested']
 phase['scheduler_bracket']={'target':[first['frame_pacing']['target_fps'],last['frame_pacing']['target_fps']],'PSO':[first['pipeline_prepare']['pipelines'],last['pipeline_prepare']['pipelines']],'variants':[first['uniform_specialization']['variants'],last['uniform_specialization']['variants']]}
 b=phase['scheduler_bracket'];b['stable_target60_PSO_variants']=b['target']==[60,60]and b['PSO'][0]==b['PSO'][1]and b['variants'][0]==b['variants'][1]
 phase['wait_scope']='Official scheduler/APP marker wall intervals only, separate window from stat/record; observed fence includes driver/GPU/CPU wake, not pure GPU time or additive critical path.'
(D/'thermal-public-api.log').write_text(ad('logcat','-d','--pid='+pid,'-s','DmThermal:I','AndroidRuntime:E'))
phase['stable_target60_PSO_variants_all_windows']=all(phase[k+'_bracket']['stable_target60_PSO_variants']for k in (('stat','record','scheduler')if trace else('stat','record')))
phase['public_thermal_only']=True;phase['frame_scope']='Official SurfaceFlinger frame/timeline request; analysis must verify exact game SurfaceView. Existing Q traces have buffer PresentFenceSignaled but no app SurfaceView actual_frame_timeline_slice. No fallback to display-wide FPS.';phase['no_GPU_counter_or_FPS_gain_claim']=True;phase['complete']=True;save()
