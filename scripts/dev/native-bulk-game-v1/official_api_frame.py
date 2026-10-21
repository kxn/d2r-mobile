"""One existing official GFXR single-frame flow; no replay or performance test.
The original runner owns APK/resources, PID, normal input, save2, stop and CAS.
Only explicit mode selection changes Android layer settings/properties.
"""
from pathlib import Path
import hashlib,importlib.util,json,os,re,shlex,signal,subprocess,sys,time
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport';LAYER='com.arm.frameadvisor.replay'
OLD=R/'.local/build/full6-36a-gfxr-capture-v1-freeze/capture-astc6-full6-36a9781-api-v1.py'
OLD_SHA='fc8d4656f606adc7e288e8acaa2d9dba835a62a3f6348a74c5eabc26608bc194'
LAYER_APK=R/'.local/tools/Arm_Performance_Studio_2026.5/frame_advisor/bin/android/replay-release.apk'
LAYER_SHA='e44a9dea3eb2933051e9d1bafb1c1c6a476d980b910447b75988cca80ed8c127'
INFO=R/'.local/build/gfxreconstruct-host/tools/info/gfxrecon-info'
CONVERT=R/'.local/build/gfxreconstruct-host/tools/convert/gfxrecon-convert'
SETTINGS={'enable_gpu_debug_layers':'1','gpu_debug_app':PKG,
 'gpu_debug_layers':'VK_LAYER_LUNARG_gfxreconstruct','gpu_debug_layer_app':LAYER,'gpu_debug_layers_gles':None}
CONFLICT=['debug.gfxrecon.capture_queue_submits','debug.gfxrecon.capture_draw_calls',
 'debug.gfxrecon.capture_trigger','debug.gfxrecon.capture_trigger_boundary']
def require(ok,message):
 if not ok:raise RuntimeError(message)
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def put(p,x):Path(p).write_text(json.dumps(x,indent=2)+'\n')
def validate(p,pinned):
 if p.get('official_wait_identity'):
  return validate_wait(p,pinned)
 c=p['official_api_frame'];require(set(c)=={'tool','frames','timeout_seconds','memory_tracking_mode','layer_apk','info','convert'},'official frame fields differ')
 require(c['tool']=='gfxreconstruct'and type(c['frames'])is int and c['frames']==1 and
  type(c['timeout_seconds'])is int and 20<=c['timeout_seconds']<=180,'one bounded official frame required')
 require(c['memory_tracking_mode']=='assisted','shared HOST_ALLOCATION capture requires explicit assisted mode')
 require(p.get('native_host_experiment')and p.get('native_candidate_validation')and
  not any(p.get(k)for k in ['performance_tools','official_cpu_aa','official_l1i_record','official_gpu_tools',
  'counter_snapshot','sustained_play','official_memory_map','official_wait_identity']),'API frame capture must be separate from performance/debugger modes')
 require(sha(OLD)==OLD_SHA,'existing successful capture controller changed')
 for key,path in [('layer_apk',LAYER_APK),('info',INFO),('convert',CONVERT)]:
  require(pinned(c[key])==path,'existing official tool required: '+key)
 require(c['layer_apk']['sha256']==LAYER_SHA,'official layer APK differs')
 return {'tool':'official GFXReconstruct single Vulkan frame','frames':1,'timeout_seconds':c['timeout_seconds'],
  'memory_tracking_mode':'assisted','mapped_pointer_policy':'preserve original driver mapping; no page-guard shadow',
  'coherent_noFlush_payload_complete':False,
  'snapshot_boundary':'Official triggered trim still idles the device and snapshots live resources; large scans/copies and capture perturbation remain possible.',
  'existing_controller':{'path':str(OLD),'sha256':OLD_SHA},'layer_APK':c['layer_apk'],
  'scope':'API structure/state/offset/group sizes only; imported HOST_ALLOCATION payload and replay correctness unproven',
  'no_replay':True,'not_performance_or_thermal':True,'capture_before_save_stop_then_bulk_pull':True}
class Capture:
 def __init__(self,dev,out,p,token,pid,save):
  self.dev,self.p,self.pid,self.save=dev,p,pid,save;self.out=out/'official-api-frame';self.out.mkdir()
  self.prefix='q735-f830-api-'+token;self.report=self.out/'capture.json';self.own=None
  self.settings={};self.props={};self.touched=False
  self.props_new={'debug.gfxrecon.memory_tracking_mode':'assisted',
   'debug.gfxrecon.capture_process_name':PKG,
   'debug.gfxrecon.capture_file':'/data/user/0/'+PKG+'/files/runtime/'+self.prefix+'.gfxr',
   'debug.gfxrecon.capture_frames':'','debug.gfxrecon.capture_file_timestamp':'false',
   'debug.gfxrecon.capture_file_flush':'true','debug.gfxrecon.capture_android_trigger':'false',
   'debug.gfxrecon.capture_trigger_frames':'1','debug.gfxrecon.quit_after_capture_frames':'false'}
  spec=importlib.util.spec_from_file_location('existing_single_frame_primitives',OLD)
  self.old=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.old)
  self.r={'stage':'prepared','capture_completed':False,'cleanup_errors':[],
   'replayed':False,'scope':p['_official_observation'],'runtime':p['runtime_commit'],
   'host_ELF_sha256':p['new_elf'],'driver_sha256':p['driver']['sha256'],
   'shared_lazy_windows_requested':p.get('shared_lazy_windows',0),'token':token,
   'capture_file_prefix':self.prefix,'requested_settings':dict(SETTINGS),'requested_properties':self.props_new}
  self.write()
 def write(self):put(self.report,self.r);self.save()
 def same(self):require(self.own and self.pid()==self.own,'official frame requires original owned PID')
 def prop(self,key,value):self.dev.adb('shell','setprop '+shlex.quote(key)+' '+shlex.quote(value))
 def configure(self):
  require(not self.pid(),'configure layers only before original cold launch')
  path=self.dev.adb('shell','pm','path',LAYER)
  require(path.startswith('package:')and '\n'not in path,'official layer APK absent; install original tool separately')
  require(self.dev.adb('shell','sha256sum',path.removeprefix('package:')).split()[0]==LAYER_SHA,'installed official layer APK differs')
  self.settings={k:self.dev.adb('shell','settings','get','global',k)for k in SETTINGS}
  self.props={k:self.dev.adb('shell','getprop',k)for k in self.props_new}
  conflicts={k:self.dev.adb('shell','getprop',k)for k in CONFLICT}
  self.r.update(settings_before=self.settings,properties_before=self.props,untouched_range_properties=conflicts);self.write()
  require(self.settings['enable_gpu_debug_layers']in ['0','null'],'another layer owner active')
  require(all(not v for k,v in conflicts.items()if not k.endswith('boundary'))and
   conflicts['debug.gfxrecon.capture_trigger_boundary']in ['','frames'],'existing GFXR range/trigger conflict; preserve')
  require(not any(n.startswith(self.prefix)for n in self.dev.app('ls','files/runtime').splitlines()),'capture prefix already exists')
  self.touched=True
  for k,v in SETTINGS.items():
   self.dev.adb('shell','settings','delete','global',k)if v is None else self.dev.adb('shell','settings','put','global',k,v)
  for k,v in self.props_new.items():self.prop(k,v)
  require(all(self.dev.adb('shell','getprop',k)==v for k,v in self.props_new.items()),'official capture property did not apply')
  self.r['stage']='configured-before-launch';self.write()
 def wait_ready(self,own,endpoint):
  # Same original CPU-helper readiness:30 bounded observations,4 distinct ticks.
  self.own=own;previous=None;stable=0;last_tick=None;self.r['pre_trigger_cache_states']=[]
  for attempt in range(30):
   self.same();v=endpoint('api-cache-ready-'+str(attempt))
   require(v['valid']and v['world_input_ready']and v['area']==1 and v['guest_heap']['shared']is True and
    v['scale_3d']==.60 and v['pool_windows']['window_bytes']==self.p['pool_window_bytes']and
    v['game_output']==v['surface_output']=={'width':2750,'height':1260}and
    not v['world_freeze']['host_frozen']and not v['runtime_extrap']['enabled'],'API frame scene/provider readiness changed')
   key=(v['pipeline_prepare']['pipelines'],v['uniform_specialization']['variants'])
   self.r['pre_trigger_cache_states'].append({'time_ns':v['time_ns'],'tick':v['tick'],'PSO':key[0],'variants':key[1],'target_fps':v['frame_pacing']['target_fps']});self.write()
   if v['tick']!=last_tick:stable=stable+1 if key==previous else 1;previous=key;last_tick=v['tick']
   if stable>=4:break
   time.sleep(.4)
  require(stable>=4,'PSO/variant readiness never stabilized')
  require(v['frame_pacing']['target_fps']==60,'Target is not60 before API frame; do not override')
  self.r['pre_trigger_cache_ready']=True;self.write()
 def trigger(self,own):
  self.own=own;self.same();maps=self.dev.app('cat','/proc/'+own+'/maps');(self.out/'maps.txt').write_text(maps)
  require('libVkLayer_gfxreconstruct.so'in maps and 'libvulkan_freedreno.so'in maps and
   all(x not in maps.lower()for x in ['renderdoc','libvklayerlwi','libvulkan_panfrost']), 'wrong/missing API layer or f830 driver')
  log=self.dev.adb('logcat','-d','--pid='+own,'-v','brief');(self.out/'before-trigger-logcat.txt').write_text(log)
  require('Settings Loader: Found option "debug.gfxrecon.memory_tracking_mode" with value "assisted"'in log,'official layer did not log assisted selection; no trigger')
  self.r['official_layer_logged_memory_tracking_mode']='assisted';self.write()
  require('Recording graphics API capture to'not in log and
   not any(n.startswith(self.prefix)and n.endswith('.gfxr')for n in self.dev.app('ls','files/runtime').splitlines()),'capture started before explicit trigger')
  closed=log.count('Finished recording graphics API capture');self.same()
  self.r.update(stage='trigger',owned_PID=int(own));self.write();self.prop('debug.gfxrecon.capture_android_trigger','true')
  # Existing bounded tool observation only; not a target timer or sampler.
  deadline=time.monotonic()+self.p['official_api_frame']['timeout_seconds']
  while time.monotonic()<deadline:
   self.same();log=self.dev.adb('logcat','-d','--pid='+own,'-v','brief');(self.out/'capture-logcat.txt').write_text(log)
   if log.count('Finished recording graphics API capture')>closed:
    self.r['capture_completed']=True;break
   time.sleep(1)
  require(self.r['capture_completed'],'official closed-capture marker absent; no retry')
  self.prop('debug.gfxrecon.capture_android_trigger','false');self.same()
  names=[n for n in self.dev.app('ls','files/runtime').splitlines()if n.startswith(self.prefix)and n.endswith('.gfxr')]
  require(len(names)==1,'one closed raw trace required');remote='files/runtime/'+names[0]
  size=int(self.dev.app('stat','-c','%s',remote));require(size>64,'raw capture too small')
  self.r.update(stage='closed-before-original-save',remote_trace=remote,remote_bytes=size);self.write()
 def restore(self):
  # Always run, even if configuration/launch/capture/original save failed.
  if self.touched:
   for k,v in self.props.items():
    try:self.prop(k,v)
    except BaseException as e:self.r['cleanup_errors'].append(k+': '+repr(e))
   for k,v in self.settings.items():
    try:self.dev.adb('shell','settings','delete','global',k)if v=='null'else self.dev.adb('shell','settings','put','global',k,v)
    except BaseException as e:self.r['cleanup_errors'].append(k+': '+repr(e))
   for method,values in [('properties',self.props),('settings',self.settings)]:
    try:
     actual={k:self.dev.adb('shell','getprop',k)if method=='properties'else
      self.dev.adb('shell','settings','get','global',k)for k in values}
     require(actual==values,'exact '+method+' restoration mismatch');self.r[method+'_restored']=True
    except BaseException as e:self.r['cleanup_errors'].append(method+': '+repr(e))
  self.write()
  if self.r['cleanup_errors']and sys.exc_info()[0]is None:raise RuntimeError('official API frame settings/property cleanup incomplete')
 def export_after_stop(self):
  require(not self.pid(),'bulk trace transfer requires original owned game already stopped')
  require(self.r['capture_completed']and not self.r['cleanup_errors']and
   self.r.get('settings_restored')and self.r.get('properties_restored'),'closed capture/exact property cleanup required')
  remote=self.r['remote_trace'];digest=self.dev.app('sha256sum',remote).split()[0]
  available=self.old.available_tmp_bytes(self.dev.adb('shell','df','-k','/data/local/tmp'))
  require(available>=self.r['remote_bytes']+(64<<20),'owned raw copy requires actual free space plus64MiB')
  tmp='/data/local/tmp/'+self.prefix+'.gfxr';raw=self.out/'capture.gfxr';require(not raw.exists(),'preserve prior raw output')
  copy={'path':tmp,'verified':False};self.r['owned_raw_copy']=copy;self.write()
  try:
   self.dev.adb('shell',self.old.raw_copy_command(remote,tmp));require(self.dev.adb('shell','sha256sum',tmp).split()[0]==digest,'owned raw copy differs')
   copy.update(verified=True,sha256=digest);self.write();self.dev.adb('pull',tmp,str(raw))
   require(raw.stat().st_size==self.r['remote_bytes']and sha(raw)==digest,'raw transfer differs')
   self.r.update(stage='raw-transferred',local_trace=str(raw),raw_sha256=digest,private_trace_retained_for_recovery=True)
  finally:
   if copy['verified']:
    require(self.dev.adb('shell','sha256sum',tmp).split()[0]==digest,'owned copy changed; preserve')
    self.dev.adb('shell','rm',tmp);copy['removed']=True
   else:copy['retained_unverified']=True
   self.write()
  info=self.out/'official-info.json';api=self.out/'official-api.jsonl';require(not info.exists()and not api.exists(),'preserve prior official export')
  commands=[[str(INFO),'--verbose','--output',str(info),str(raw)],
   [str(CONVERT),'--format','jsonl','--expand-flags','--output',str(api),str(raw)]]
  self.r['official_commands']=commands;self.write()
  for n,command in enumerate(commands):
   key='info'if not n else'convert';require(sha(command[0])==self.p['official_api_frame'][key]['sha256'],'official export tool changed')
   with (self.out/(key+'.log')).open('x')as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180 if not n else 900)
   if not n:
    result=json.loads(info.read_text());f=result['general-info']['frames']
    operations=result.get('gfxr-operations',[])
    require(any(x.get('tool')=='capture'and x.get('capture-parameters',{}).get('memory-tracking-mode')=='assisted'for x in operations),'official trace does not confirm assisted memory tracking')
    self.r['official_recorded_memory_tracking_mode']='assisted'
    require(result['detected-apis']==['Vulkan']and result['exe']['name']==PKG and
     f['actual-count']==f['total-count']==1 and f['blank-count']==0,'official info is not one nonblank original-app Vulkan frame')
  require(api.stat().st_size>0,'empty official API conversion')
  self.r.update(stage='complete-API-structure-only-not-replay',info_sha256=sha(info),api_sha256=sha(api),
   replay_correctness_proven=False,imported_host_payload_proven=False);self.write()

# Alternative, mutually exclusive official observation. It neither loads a GPU
# layer nor changes app/runtime code. Addresses apply ONLY to this reviewed ELF.
WAIT_RUNTIME='4422f7689cea0c2f1ad88f343485ee6a6acb4c93'
WAIT_BID='b68b362a5bd53a568911497e73106741e86841d9'
WAIT_ELF_SHA='6675c890d2fe978980c16c6994e970cc6fde2e24d9ec69cf34aafc42b7685702'
WAIT_REVIEW='0d5dde97cc175309f8403a95d219fbbe94dfa6cf84f5d46fe12dba1b4d7870ce'
WAIT_HOST=R/'.local/tools/lldb19/usr/lib/llvm-19/bin/lldb'
WAIT_SERVER=R/'.local/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/19/lib/linux/aarch64/lldb-server'
WAIT_OLD=R/'.local/build/capture-pool-residency-onestop-v1.py'
WAIT_OLD_SHA='03b8ec8402f2ed479de402083620e336e3f8f6258d5325b133823d78472297b5'

def validate_wait(p,pinned):
 c=p['official_wait_identity']
 require(set(c)=={'tool','timeout_seconds','host_lldb','server','elf'} and c['tool']=='lldb19-one-stop'
  and type(c['timeout_seconds'])is int and 20<=c['timeout_seconds']<=90,'one bounded official LLDB wait observation required')
 require(p.get('native_host_experiment')and p.get('native_candidate_validation')and
  not any(p.get(k)for k in ['official_api_frame','performance_tools','official_cpu_aa','official_l1i_record',
   'official_gpu_tools','counter_snapshot','sustained_play','official_memory_map']), 'wait identity must be separate from layers/performance/debugger modes')
 require(p['runtime_commit']==WAIT_RUNTIME and p['new_elf']==WAIT_ELF_SHA
  and p['native_candidate_validation']['root_review']['sha256']==WAIT_REVIEW,
  'wait file addresses require exact root-reviewed4422 ELF')
 review=json.loads(pinned(p['native_candidate_validation']['root_review']).read_text())
 require(review['host_ELF_BuildID']==WAIT_BID and review['host_ELF_SHA256']==WAIT_ELF_SHA
  and review['runtime']==WAIT_RUNTIME,'root artifact identity differs')
 for key,path,digest in [('host_lldb',WAIT_HOST,'7d1b36cbaccbc1ccf8b47e581948ec876f006a3e5316b51373deadbfb4765e9a'),
  ('server',WAIT_SERVER,'fb970fd0800b1f886ce7a366cf361e57f302ef08bb769f9eb0b85e5fc6823d3e')]:
  require(pinned(c[key])==path and c[key]['sha256']==digest,'exact official '+key+' required')
 require(c['elf']['sha256']==WAIT_ELF_SHA,'wait ELF hash differs');pinned(c['elf'])
 require(sha(WAIT_OLD)==WAIT_OLD_SHA,'existing owned debugger reference changed')
 return {'tool':'official LLDB19 one all-stop wait identity','timeout_seconds':c['timeout_seconds'],
  'existing_controller':{'path':str(WAIT_OLD),'sha256':WAIT_OLD_SHA},'BuildID':WAIT_BID,
  'breakpoint_fileVA':'0x33e9b8','preflight_caller_fileVA':'0x354db8','one_shot':True,
  'scope':'One actual CPU-source wait dependency metadata; not GPU duration or proof it causes the9ms phase',
  'payload_or_smaps':False,'no_hit_is_not_identity_success':True,'default':'OFF without explicit official_wait_identity'}

class WaitIdentity:
 # Reuse original API observation's same-PID/PSO camp admission; it does not
 # configure layers or change rendering. The parent still owns route/save/CAS.
 wait_ready=Capture.wait_ready
 def __init__(self,dev,out,p,token,pid,save):
  self.dev,self.p,self.pid,self.save=dev,p,pid,save;self.own=None
  self.out=out/'official-wait-identity';self.out.mkdir();self.report=self.out/'capture.json'
  self.sock='d2r-wait-'+token;self.pidfile='files/runtime/temp/'+self.sock+'.pid'
  self.server=self.debugger=None;self.port=None;self.closed=False
  self.r={'stage':'prepared','identity_captured':False,'cleanup_errors':[],
   'scope':p['_official_observation'],'token':token,'runtime':p['runtime_commit'],
   'pre_trigger_cache_states':[]};self.write()
 def write(self):put(self.report,self.r);self.save()
 def same(self):require(self.own and self.pid()==self.own,'official wait requires original owned PID')
 def configure(self):
  require(not self.pid(),'wait tool preflight before original launch')
  require(self.dev.adb('shell','settings','get','global','enable_gpu_debug_layers')in ['0','null'],'GPU layer active')
  props={k:self.dev.adb('shell','getprop',k)for k in ['debug.egl.profiler','debug.vulkan.profiler']}
  require(all(v in ['','0']for v in props.values()),'SDP driver profiling hook active')
  require(self.dev.app('sha256sum','files/runtime/temp/d2r-lldb-server').split()[0]==self.p['official_wait_identity']['server']['sha256'],'transaction official server differs')
  self.r.update(stage='clean-before-launch',unchanged_profiling_properties=props);self.write()
 def trigger(self,own):
  self.own=own;self.same();maps=self.dev.app('cat','/proc/'+own+'/maps');(self.out/'maps.txt').write_text(maps)
  require(all(x not in maps.lower()for x in ['renderdoc','libvklayer','libq3dtools','libvulkan_panfrost']), 'injected/profiling layer or SDP hook in process')
  rows=[x for x in maps.splitlines()if 'libdmhost_android.so'in x];require(rows,'host mapping absent')
  slide=min(int(x.split()[0].split('-')[0],16)-int(x.split()[2],16)for x in rows)
  mapped=rows[0].split()[-1]
  require('!/'not in mapped and self.dev.adb('shell','sha256sum',mapped).split()[0]==WAIT_ELF_SHA,'mapped exact host ELF differs')
  drivers=[x.split()[-1]for x in maps.splitlines()if'libvulkan_freedreno.so'in x]
  require(drivers and self.dev.adb('shell','sha256sum',drivers[0]).split()[0]==self.p['driver']['sha256'],'mapped driver differs')
  require(self.dev.app('sh','-c','test ! -e "$1" || echo EXISTS','sh',self.pidfile)=='','owned server pidfile collision')
  self.port=self.dev.adb('forward','tcp:0','localabstract:'+self.sock);require(self.port.isdecimal(),'allocated forward absent')
  self.r.update(owned_PID=int(own),mapped_host=mapped,slide=hex(slide),forward_port=int(self.port));self.write()
  c=self.p['official_wait_identity'];elf=c['elf']['path'];commands=self.out/'commands.lldb'
  config={'pid':int(own),'timeout_seconds':c['timeout_seconds'],'output':str(self.out/'wait.json')}
  load="import importlib.util; s=importlib.util.spec_from_file_location('owned_wait_module',"+repr(str(Path(__file__).resolve()))+"); w=importlib.util.module_from_spec(s); s.loader.exec_module(w); w.lldb_wait(lldb.debugger,"+repr(config)+")"
  commands.write_text('settings set auto-confirm true\ntarget create --arch aarch64 '+json.dumps(elf)+'\ngdb-remote 127.0.0.1:'+self.port+'\ntarget modules load --file '+json.dumps(elf)+' --slide '+hex(slide)+'\nprocess handle SIGSEGV -s false -n false -p true\nprocess handle SIGBUS -s false -n false -p true\nprocess handle SIGCONT -s false -n false -p true\nprocess handle SIGSTOP -s false -n false -p false\nscript '+load+'\nquit\n')
  shell='echo $$ > '+shlex.quote(self.pidfile)+'; exec files/runtime/temp/d2r-lldb-server gdbserver '+shlex.quote('unix-abstract://'+self.sock)+' --attach '+own
  env=os.environ.copy();env['LD_LIBRARY_PATH']=str(R/'.local/tools/lldb19/usr/lib/x86_64-linux-gnu');env['PYTHONPATH']=str(R/'.local/tools/lldb19/usr/lib/llvm-19/lib/python3.13/site-packages')
  argv=[str(WAIT_HOST),'--batch','-s',str(commands)];serverargv=[str(R/'.local/android-sdk/platform-tools/adb'),'-s',self.p['serial'],'shell','run-as',PKG,'sh','-c',shlex.quote(shell)]
  self.r.update(stage='official-debugger-attach',host_argv=argv,server_argv=serverargv);self.write()
  try:
   with(self.out/'server.log').open('x')as serverlog,(self.out/'debugger.log').open('x')as debuglog:
    self.server=subprocess.Popen(serverargv,stdout=serverlog,stderr=subprocess.STDOUT,start_new_session=True);time.sleep(.3)
    self.debugger=subprocess.Popen(argv,env=env,stdout=debuglog,stderr=subprocess.STDOUT,start_new_session=True)
    require(self.debugger.wait(timeout=c['timeout_seconds']+30)==0,'official debugger failed')
   observed=json.loads((self.out/'wait.json').read_text());self.r['official_stop']=observed
   self.r['identity_status']='captured'if observed.get('hit')else'UNKNOWN'
   if 'error'in observed:self.r['stage']='UNKNOWN-read-failed'
   self.write()
   require(observed.get('detached')or observed.get('already_exited'),'explicit detach/exit not confirmed')
   require('error'not in observed,'official debugger identity read failed: '+str(observed.get('error')))
   self.r.update(identity_captured=observed['hit'],stage='identity-read-and-detached'if observed['hit']else'no-wait-hit-before-timeout')
  finally:self.restore()
  self.write()
 def restore(self):
  if self.closed:return
  def problem(label,e):self.r['cleanup_errors'].append(label+': '+repr(e))
  # Normal path has already detached in LLDB's finally. This is bounded owned
  # failure cleanup; no generic debugger or ADB service is killed.
  for label,proc in [('debugger',self.debugger)]:
   try:
    if proc is not None and proc.poll()is None:
     os.killpg(proc.pid,signal.SIGINT)
     try:proc.wait(timeout=7)
     except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGTERM);proc.wait(timeout=5)
   except BaseException as e:problem(label,e)
  try:
   spid=self.dev.app('sh','-c','cat "$1" 2>/dev/null || true','sh',self.pidfile)
   if spid.isdecimal():
    cmd=self.dev.app('sh','-c','cat /proc/"$1"/cmdline 2>/dev/null || true','sh',spid)
    if cmd:
     require(self.sock in cmd and 'lldb-server'in cmd and self.own in cmd,'owned server identity changed; preserve')
     self.dev.app('kill',spid)
   self.dev.app('rm','-f',self.pidfile)
  except BaseException as e:problem('owned server',e)
  try:
   if self.server is not None and self.server.poll()is None:self.server.wait(timeout=5)
  except BaseException as e:problem('owned server exit',e)
  try:
   if self.port:self.dev.adb('forward','--remove','tcp:'+self.port)
  except BaseException as e:problem('owned forward',e)
  try:
   now=self.pid()
   if now:
    require(self.own and now==self.own,'replacement PID; preserve')
    status=self.dev.app('cat','/proc/'+now+'/status');tracer=re.search(r'^TracerPid:\s+(\d+)',status,re.M)
    require(tracer and tracer.group(1)=='0','owned game still traced')
    self.dev.app('kill','-CONT',now)
   self.r['owned_debugger_cleanup_complete']=not self.r['cleanup_errors']
  except BaseException as e:problem('owned trace cleanup',e)
  self.closed=True;self.write()
  if self.r['cleanup_errors']and sys.exc_info()[0]is None:raise RuntimeError('official wait cleanup incomplete')
 def export_after_stop(self):
  require(not self.pid(),'original game must already be stopped')
  require(self.closed and not self.r['cleanup_errors'],'wait debugger cleanup incomplete')
  self.r['original_save_stop_observed']=True;self.write()

def lldb_wait(debugger,config):
 """Official SB process event wait with a tool timeout, never a GPU timer."""
 import lldb
 target=debugger.GetSelectedTarget();process=target.GetProcess();r={'hit':False,'detached':False}
 try:
  require(process.IsValid()and process.GetProcessID()==config['pid']and process.GetState()==lldb.eStateStopped,'no owned live attach stop')
  module=target.FindModule(lldb.SBFileSpec('libdmhost_android.so'))
  require(module.IsValid()and module.GetUUIDString().replace('-','').lower()==WAIT_BID,'live module BuildID differs')
  bp=target.BreakpointCreateBySBAddress(module.ResolveFileAddress(0x33e9b8));bp.SetOneShot(True)
  require(bp.IsValid()and bp.GetNumLocations()==1,'one exact wait breakpoint absent')
  listener=lldb.SBListener('owned-one-wait');listener.StartListeningForEvents(process.GetBroadcaster(),lldb.SBProcess.eBroadcastBitStateChanged)
  original_stop=process.GetStopID();debugger.SetAsync(True);err=process.Continue();require(err.Success(),str(err))
  deadline=time.monotonic()+config['timeout_seconds']
  while True:
   state=process.GetState()
   if state==lldb.eStateStopped and process.GetStopID()>original_stop:
    stopped=[process.GetThreadAtIndex(i)for i in range(process.GetNumThreads())if process.GetThreadAtIndex(i).GetStopReason()==lldb.eStopReasonBreakpoint]
    require(len(stopped)==1,'not one actual breakpoint stop')
    process.SetSelectedThread(stopped[0]);r['breakpoint_encountered']=True;debugger.HandleCommand('thread backtrace all')
    r['snapshot']=read_wait_metadata(debugger,config['pid']);r['hit']=True;break
   if state in(lldb.eStateExited,lldb.eStateDetached,lldb.eStateCrashed):r['terminal_state']=state;break
   left=deadline-time.monotonic()
   if left<=0:r['no_hit_before_timeout']=True;break
   event=lldb.SBEvent();listener.WaitForEvent(max(1,int(left)),event)
 except BaseException as e:r['error']=repr(e)
 finally:
  state=process.GetState()
  if state in(lldb.eStateExited,lldb.eStateDetached):r['already_exited']=True
  elif process.IsValid():
   if state!=lldb.eStateStopped:
    err=process.Stop();r['interrupt_error']=str(err)if not err.Success()else None
   err=process.Detach();r['detached']=err.Success()
   if not err.Success():r['detach_error']=str(err)
  put(config['output'],r)

def read_wait_metadata(debugger,expected_pid):
 import json
 import lldb
 T=debugger.GetSelectedTarget(); P=T.GetProcess(); TH=P.GetSelectedThread(); F=TH.GetFrameAtIndex(0)
 BID='b68b362a5bd53a568911497e73106741e86841d9'
 assert P.IsValid() and P.GetState()==lldb.eStateStopped and P.GetProcessID()==expected_pid
 assert TH.GetStopReason()==lldb.eStopReasonBreakpoint
 assert F.GetPCAddress().GetFileAddress()==0x33e9b8
 assert F.GetModule().GetUUIDString().replace('-','').lower()==BID
 
 def good(v):
     assert v.IsValid(), 'invalid SBValue'
     assert v.GetError().Success(), str(v.GetError())
     return v
 
 def u(v):
     good(v); error=lldb.SBError(); n=v.GetValueAsUnsigned(error)
     assert error.Success(), str(error)
     return n
 
 def c(v,name): return good(good(v).GetChildMemberWithName(name))
 def fields(v,names): return {n:u(c(v,n)) for n in names.split()}
 def glob(name,file):
     vs=[v for v in T.FindGlobalVariables(name,32) if v.GetDeclaration().GetFileSpec().GetFilename()==file]
     assert len(vs)==1, (name,len(vs))
     return good(vs[0])
 def obj(name,addr,typename,bytes):
     ty=T.FindFirstType(typename); assert ty.IsValid() and ty.GetByteSize()==bytes
     assert addr
     return good(T.CreateValueFromAddress(name,lldb.SBAddress(addr,T),ty))
 def overlap(a,n,b,m): return n and m and a<b+m and b<a+n
 
 R={n:u(F.FindRegister(n)) for n in ['x19','x20','x21','x22','w23','x24','x25']}
 address,size=R['x20'],R['x19']; serial=R['x24']; assert serial and size
 V=glob('V','nvn_vk.c'); assert u(c(V,'device')) and u(c(V,'queue'))
 completed=u(glob('queue_completed','nvn_vk.c')); assert completed<serial
 page=u(glob('g_pg','nvn_dirty.c')); assert page and not page&(page-1)
 first=address&~(page-1); end=(address+size+page-1)&~(page-1)
 owner=obj('observed_pool',R['x21'],'pool',448)
 O={'scope':'single stopped metadata; no elapsed/GPU timing or payload','pid':P.GetProcessID(),'tid':TH.GetThreadID(),'BuildID':BID,'registers':R,'address':address,'bytes':size,'host_page_bytes':page,'page_envelope':[first,end],'requested_serial':serial,'completed_serial':completed,'submitted_serial':u(glob('queue_submitted','nvn_vk.c')),'Pool':fields(owner,'identity guest size references shared shared_closing shared_properties shared_import_nodes shared_import_bytes'),'pending':[]}
 assert address==u(c(owner,'guest'))+R['x22']
 s=glob('pending','nvn_vk.c'); seen=set()
 while u(s):
     assert len(seen)<64 and u(s) not in seen, 'pending bound/cycle: preserve incomplete, do not silently truncate'
     seen.add(u(s)); q=good(s.Dereference()); rs=good(c(q,'resources').Dereference())
     n=u(c(rs,'cpu_write_count')); ne=u(c(rs,'cpu_effect_write_count')); assert ne<=n<=4096
     writes=c(rs,'cpu_writes'); assert not n or u(writes)
     row={'submission_address':u(s),**fields(q,'serial queue_serial frame fence'),'cpu_write_count':n,'cpu_effect_write_count':ne,'matching_page_ranges':[]}
     for k in range(n):
         w=obj('cpu_write_range_'+str(k),u(writes)+16*k,'cpu_write_range',16); a=u(c(w,'address')); b=u(c(w,'size'))
         if overlap(first,end-first,a,b): row['matching_page_ranges'].append({'index':k,'address':a,'bytes':b,'kind':'CPU effect destination' if k<ne else 'shared GPU publication','overlaps_raw_texture_bytes':bool(overlap(address,size,a,b))})
     O['pending'].append(row); s=c(q,'next')
 # Do NOT follow s->effects: completion frees those nodes before removing pending.
 # The immutable cpu_writes array remains retained by pending/resources ownership.
 frames=[TH.GetFrameAtIndex(k) for k in range(TH.GetNumFrames())]
 pf=[f for f in frames if 'vk_texture_cpu_preflight' in (f.GetFunctionName() or '')]
 assert len(pf)==1, 'no unique preflight ancestor; preserve unknown, no image-list guess'
 pf=pf[0]; assert pf.GetPCAddress().GetFileAddress()==0x354db8
 # Exact-BID objdump proves caller x19=logical tex_t*, x20=image* at BL354db4.
 # These are caller-frame unwound callee-saved registers, NOT current x19/x20.
 taddr=u(pf.FindRegister('x19')); iaddr=u(pf.FindRegister('x20'))
 tex=obj('logical_texture',taddr,'tex_t',144)
 O['logical_texture']={'address':taddr,**fields(tex,'serial mem off has_layout'),'description':fields(c(tex,'d'),'target format flags samples width height depth levels stride pkg_block_h'),'layout':fields(c(tex,'L'),'size layer_size align layers bpb tileH tileD')}
 assert u(c(tex,'mem'))==address and u(c(c(tex,'L'),'size'))==size
 # This register was selected before a possible commit/render_end. It is a
 # non-owning entry candidate, NEVER a proof of current image lifetime/owner.
 O['entry_image_candidate_not_lifetime_proof']=True
 O['entry_image_candidate']={'address':iaddr,'status':'absent'if not iaddr else'UNKNOWN','recorded_commit':bool(R['w23'])}
 if iaddr:
     try:
         im=obj('entry_image_candidate',iaddr,'image',904)
         assert u(c(c(im,'tex'),'serial'))==u(c(tex,'serial'))
         O['entry_image_candidate'].update(status='readable_metadata_not_live_owner_proof',**fields(im,'handle format initialized decoded external_access aspect stamp checked_dirty_epoch checked_dirty_stamp cpu_rebuildable shared_writer shared_writer_source shared_dirty_scope native_entry last_use astc_copy_encoding bc_copy_decoded levels layers'),layout=u(c(im,'layout')),shared_writer_region=fields(c(im,'shared_writer_region'),'xoffset yoffset zoffset width height depth'))
     except BaseException as e:O['entry_image_candidate']['read_error_UNKNOWN']=repr(e)
 O['interpretation_limits']=['Immutable write snapshots distinguish CPU-effect prefix from GPU-publication suffix; a suffix alone does not prove retile vs other GPU write.','Page-only overlap is a conservative host-page dependency, not automatically unnecessary.','Entry image pointer was selected before optional commit; readable candidate handle/stamp does not prove current owner/lifetime or reconstruct prior publication-ack failure.','Original requested serial covers GPU fence AND completion CPU effects; all-stop is not a GPU time measurement.','One encountered wait may not be the dominant9ms Surface wait; reproduce the slow phase separately before this diagnostic.']
 return O
