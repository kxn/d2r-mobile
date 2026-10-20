"""Orchestrate the installed official SDP GUI; never implement counter collection."""
from pathlib import Path
import csv,hashlib,json,math,os,signal,sqlite3,subprocess,time,zipfile
R=Path('/home/dl/d2r-mobile');PKG='com.d2rmobile.nativeport'
REQUIRED=('Clocks / Second','Read Total (Bytes/sec)','Write Total (Bytes/sec)',
 'Texture Memory Read BW (Bytes/Second)','% L1 Hit','% Texture L2 Miss','% Texture Pipes Busy')
def require(ok,msg):
 if not ok:raise RuntimeError(msg)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text())
def pinned(row):
 p=Path(row['path']).resolve();require(sha(p)==row['sha256'],'changed tool input '+str(p));return p
def write(p,v):Path(p).write_text(json.dumps(v,indent=2)+'\n')
def host_identity(pid):
 p=Path('/proc')/str(pid)
 try:
  stat=(p/'stat').read_text();cmd=(p/'cmdline').read_bytes()
  if stat.rsplit(')',1)[1].split()[0]=='Z':return None
  return {'pid':int(pid),'starttime':stat.rsplit(')',1)[1].split()[19],
   'cmdline_sha256':hashlib.sha256(cmd).hexdigest(),'uid':p.stat().st_uid}
 except FileNotFoundError:return None
def config(row):
 c=read(pinned(row));require(c['schema']==1 and c['serial']=='10CG570J59001U1','Q tool config required')
 require(c['prepared_paused'] is True and set(c['required_metrics'])==set(REQUIRED),'unprepared GUI')
 for f in c['files']:pinned(f)
 require(c['gui']['pid']!=c['xvfb']['pid'],'distinct owned tool processes required')
 return c
def ad(c,*args):return subprocess.check_output([c['adb'],'-s',c['serial'],*args],text=True,timeout=30).strip()
def remote_identity(c,pid):
 raw=ad(c,'shell',f'if test -f /proc/{int(pid)}/stat; then cat /proc/{int(pid)}/stat; readlink -f /proc/{int(pid)}/exe; sha256sum /proc/{int(pid)}/exe 2>/dev/null; sed -n \'s/^Uid:[ \\t]*//p\' /proc/{int(pid)}/status; fi')
 if not raw:return None
 lines=raw.splitlines()
 if lines[0].rsplit(')',1)[1].split()[0]=='Z':return None
 require(len(lines)==4,'remote identity unavailable')
 return {'pid':int(pid),'starttime':lines[0].rsplit(')',1)[1].split()[19],
  'exe':lines[1],'sha256':lines[2].split()[0],'uids':lines[3].split()}
def check_owners(c):
 for k in ('gui','xvfb'):require(host_identity(c[k]['pid'])==c[k],'owned host '+k+' changed')
 for row in c['collectors']:require(remote_identity(c,row['pid'])==row,'owned official collector changed')
def gui(c,*args):
 require(host_identity(c['gui']['pid'])==c['gui'],'GUI PID replaced')
 env=dict(os.environ,DISPLAY=c['display']);subprocess.run([c['xdotool'],*map(str,args)],env=env,check=True,timeout=10)
def toggle(c):
 # Actual control observed in this retained GUI, not guessed on another layout.
 # Never use a newly discovered/replacement window. A missing data interval fails.
 wid=c['window_id'];env=dict(os.environ,DISPLAY=c['display'])
 pid=subprocess.check_output([c['xdotool'],'getwindowpid',str(wid)],env=env,text=True,timeout=10).strip()
 require(pid==str(c['gui']['pid']),'GUI window owner changed')
 geometry=subprocess.check_output([c['xdotool'],'getwindowgeometry','--shell',str(wid)],env=env,text=True,timeout=10)
 actual=dict(l.split('=',1)for l in geometry.splitlines()if '='in l)
 require({k:actual[k]for k in ('X','Y','WIDTH','HEIGHT')}==c['window_geometry'],'GUI geometry changed; do not guess UI')
 gui(c,'windowfocus','--sync',wid);gui(c,'mousemove','--window',wid,*c['observed_pause_control'],'click','1')
def saved_database(session,out):
 require(session.is_file() and not session.is_symlink(),'official .sdp absent')
 with zipfile.ZipFile(session)as z:
  require(set(z.namelist())=={'sdp.db','version.txt'} and len(z.infolist())==2,'unexpected official session members')
  for i in z.infolist():require(i.file_size<512*1024*1024 and not i.is_dir(),'oversize session member')
  (out/'sdp.db').write_bytes(z.read('sdp.db'));(out/'version.txt').write_bytes(z.read('version.txt'))
 return out/'sdp.db'
def metrics(db,a,b,out=None,definitions=None):
 require(b>a,'empty official window')
 con=sqlite3.connect(f'file:{db}?mode=ro',uri=True)
 try:
  rows=con.execute('SELECT m.name,m.description,m.global,d.timestamp,v.value FROM tblDataLog d JOIN tblMetricDouble v ON v.rowid=d.valueID JOIN Metric m ON m.id=d.metric WHERE d.timestamp>=? AND d.timestamp<=? ORDER BY m.name,d.timestamp',(a,b)).fetchall()
 finally:con.close()
 if out:
  with (out/'official-counter-samples.csv').open('w')as f:
   w=csv.writer(f);w.writerow(['metric','description','global','epoch_us','value']);w.writerows(rows)
 groups={}
 for n,desc,g,t,v in rows:
  require(g==1 and math.isfinite(v) and v>=0,'invalid official counter value')
  if n.startswith('%'):require(v<=100,'invalid percentage sentinel')
  groups.setdefault(n,{'description':desc,'global':g,'values':[]})['values'].append((t,v))
 result={}
 for n in REQUIRED:
  require(n in groups,'missing required official counter: '+n);x=groups[n];vv=x.pop('values')
  require(bool(x['description']) and (definitions is None or x['description']==definitions[n]),'unknown official units/definition: '+n)
  require(len(vv)>=16 and vv[0][0]-a<500000 and b-vv[-1][0]<500000,'incomplete official coverage: '+n)
  require(all(y[0]-x[0]<=500000 for x,y in zip(vv,vv[1:])),'official counter sample gap: '+n)
  if n in (*REQUIRED[:4],'% Texture Pipes Busy'):require(any(v>0 for _,v in vv),'all-zero active GPU rate: '+n)
  result[n]={**x,'samples':len(vv),'first_epoch_us':vv[0][0],'last_epoch_us':vv[-1][0],
   'min':min(v for _,v in vv),'max':max(v for _,v in vv)}
 return result

def save_gui(c,path):
 require(not path.exists(),'do not overwrite .sdp')
 # Verified on the live official2026.9 GUI: Alt+F, S opens Save Session;
 # Name starts selected. Ctrl+S was not a save shortcut and is NOT used.
 gui(c,'windowfocus','--sync',c['window_id']);gui(c,'key','alt+f');gui(c,'key','s');time.sleep(.6)
 gui(c,'key','ctrl+a');gui(c,'type','--clearmodifiers','--delay','1',str(path));gui(c,'key','Return')
 limit=time.monotonic()+30
 while time.monotonic()<limit:
  try:
   with zipfile.ZipFile(path)as z:
    if set(z.namelist())=={'sdp.db','version.txt'} and z.testzip() is None:break
  except (OSError,zipfile.BadZipFile):pass
  time.sleep(.25)
 else:raise RuntimeError('official File Save did not close; preserve tool evidence')
 time.sleep(.4);gui(c,'key','Return')  # Dismiss the observed Session saved successfully dialog.

def paused(c):
 require(host_identity(c['gui']['pid'])==c['gui'],'owned GUI gone')
 live=Path(c['live_db']);fds=Path('/proc')/str(c['gui']['pid'])/'fd'
 require(any(p.resolve()==live for p in fds.iterdir()),'official live DB is not held by owned GUI')
 def meta():
  con=sqlite3.connect(f'file:{live}?mode=ro',uri=True)
  try:return list(con.execute('SELECT count(*),max(timestamp) FROM tblDataLog').fetchone())
  finally:con.close()
 first=meta();time.sleep(.3);second=meta()
 require(first==second,'official Realtime not paused; no CPU window permitted')
 return {'first':first,'second':second,'scope':'Only official DB count/maxstamp metadata; not a GPU sampler'}

def close(c):
 errors=[]
 for k in ('gui',):
  row=c[k];actual=host_identity(row['pid'])
  if actual is None:continue
  if actual!=row:errors.append('replacement host '+k);continue
  os.kill(row['pid'],signal.SIGTERM)
 # Official clients normally release their collectors; kill only the exact
 # cold-recorded processes still present, never any system service/replacement.
 for row in reversed(c['collectors']):
  try:
   actual=remote_identity(c,row['pid'])
   if actual is None:continue
   require(actual==row,'replacement collector preserve')
   ad(c,'shell',f'kill -TERM {row["pid"]}')
  except BaseException as e:errors.append(repr(e))
 row=c['xvfb'];actual=host_identity(row['pid'])
 if actual==row:os.kill(row['pid'],signal.SIGTERM)
 elif actual is not None:errors.append('replacement Xvfb preserve')
 for _ in range(30):
  alive=[r for r in c['collectors']if remote_identity(c,r['pid']) is not None]
  hosts=[k for k in ('gui','xvfb')if host_identity(c[k]['pid']) is not None]
  if not alive and not hosts:break
  time.sleep(.2)
 else:errors.append('owned official tool still present after TERM')
 return errors

def capture(c,pid,out,window,driver,scheduler):
 out.mkdir(exist_ok=False);report={'complete':False,'owned_PID':int(pid),'cleanup_errors':[],
  'method':'OfficialSDP2026.9 GUI9metrics + existing official8s Perfetto, no custom sampling/timing',
  'scope':'Global system GPU counters include compositor; not per-draw/pass exclusive time.'};path=out/'official-gpu-capture.json'
 def state(label):
  require(ad(c,'shell','pidof '+PKG+' || true')==str(pid),'owned game PID changed')
  s=json.loads(ad(c,'exec-out','run-as',PKG,'tail','-n','1','files/runtime/game-state.jsonl'))
  require(s['valid'] and s['world_input_ready'] and s['scale_3d']==.60 and s['guest_heap']['shared']
   and s['pool_windows']['window_bytes']==window and s['game_output']==s['surface_output']=={'width':2750,'height':1260},'actual game configuration changed')
  require(s['frame_pacing']['target_fps']==60,'public target is not60');write(out/(label+'.json'),s);return s
 proc=None;active=False;log=None
 try:
  check_owners(c);report['paused_before']=paused(c);before=state('before');report['before']=before
  maps=ad(c,'exec-out','run-as',PKG,'cat','/proc/'+str(pid)+'/maps');(out/'maps.txt').write_text(maps)
  require(not any(x in maps.lower()for x in ('renderdoc','libvklayer','liblwi')),'unexpected capture/validation layer')
  actual=ad(c,'exec-out','run-as',PKG,'sha256sum','files/runtime/vulkan-driver/libvulkan_freedreno.so').split()[0]
  require(actual==driver,'active driver file changed');report['driver_sha256']=actual
  toggle(c);active=True
  report['window_start_utc_us']=time.time_ns()//1000;write(path,report)
  log=(out/'perfetto-console.txt').open('w');trace=out/'official-gpu-window.pftrace'
  proc=subprocess.Popen([str(R/'.local/venv/bin/python3'),str(scheduler),'--serial',c['serial'],
   '--seconds','8','--buffer-mb','128','--clean-atrace-print','--app-markers','--frame-timeline','--output',str(trace)],stdout=log,stderr=subprocess.STDOUT)
  # Existing official GUI window orchestration; not a GPU timing implementation.
  time.sleep(12);report['window_end_utc_us']=time.time_ns()//1000
  require(proc.wait(timeout=35)==0,'official Perfetto helper failed');log.close();log=None
  toggle(c);active=False;report['paused_after']=paused(c);after=state('after');report['after']=after
  require(before['pipeline_prepare']['pipelines']==after['pipeline_prepare']['pipelines']
   and before['uniform_specialization']['variants']==after['uniform_specialization']['variants'],'GPU window PSO/variants changed')
  session=out/'official-session.sdp';save_gui(c,session);db=saved_database(session,out)
  report['metrics']=metrics(db,report['window_start_utc_us'],report['window_end_utc_us'],out,c['metric_definitions'])
  sidecar=read(trace.with_suffix('.json'));require(sidecar['pid']==int(pid) and sidecar['trace_sha256']==sha(trace),'official trace identity changed')
  report['official_session_sha256']=sha(session);report['official_db_sha256']=sha(db);report['official_Perfetto']=sidecar
  report['same_window_Surface_denominator']='Use actual game SurfaceView PresentFenceSignaled within this trace, official trace_processor only. Never divide12s traffic by8s frames; project SDP epoch using official clock snapshot and crop raw samples to exact trace bounds first. No full game actualFrameTimeline claim.'
  report['complete']=True
 except BaseException as e:
  report['error']=repr(e);raise
 finally:
  if proc and proc.poll()is None:
   try:proc.wait(timeout=35)
   except subprocess.TimeoutExpired:report['cleanup_errors'].append('official Perfetto helper still active; preserve PID '+str(proc.pid))
  if log:log.close()
  if active:
   try:toggle(c)
   except BaseException as e:report['cleanup_errors'].append('pause: '+repr(e))
  write(path,report)
 require(not report['cleanup_errors'],'official GPU cleanup incomplete');return report
