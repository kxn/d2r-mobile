"""One existing official GFXR single-frame flow; no replay or performance test.
The original runner owns APK/resources, PID, normal input, save2, stop and CAS.
Only explicit mode selection changes Android layer settings/properties.
"""
from pathlib import Path
import hashlib,importlib.util,json,re,shlex,subprocess,sys,time
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
 c=p['official_api_frame'];require(set(c)=={'tool','frames','timeout_seconds','memory_tracking_mode','layer_apk','info','convert'},'official frame fields differ')
 require(c['tool']=='gfxreconstruct'and type(c['frames'])is int and c['frames']==1 and
  type(c['timeout_seconds'])is int and 20<=c['timeout_seconds']<=180,'one bounded official frame required')
 require(c['memory_tracking_mode']=='assisted','shared HOST_ALLOCATION capture requires explicit assisted mode')
 require(p.get('native_host_experiment')and p.get('native_candidate_validation')and
  not any(p.get(k)for k in ['performance_tools','official_cpu_aa','official_l1i_record','official_gpu_tools',
  'counter_snapshot','sustained_play','official_memory_map']),'API frame capture must be separate from performance/debugger modes')
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
   'replayed':False,'scope':p['_official_api_frame'],'runtime':p['runtime_commit'],
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
