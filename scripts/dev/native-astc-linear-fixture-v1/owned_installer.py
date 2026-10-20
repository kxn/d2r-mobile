"""Existing Vivo pending-install acknowledgement, scoped to one owned adb client.
Adapted from the accepted run-turnip-native-window-family-fixture-v1.py install loop.
No second install, installer-service reset, account link or verification setting change.
"""
import hashlib,json,re,shlex,subprocess,time,uuid,xml.etree.ElementTree as ET
UNRESOLVED=False

def actions(xml):
 """Read only exact owned D2R ASTC Filter GPU Fixture installer UI; return at most one fresh action."""
 try:tree=ET.fromstring(xml)
 except ET.ParseError:return []
 nodes=list(tree.iter('node'));pkg='com.android.packageinstaller'
 if not any(n.get('package')==pkg and n.get('text')=='D2R ASTC Filter GPU Fixture'for n in nodes):return []
 def enabled(n):return n.get('enabled','true')=='true'
 acks=[n for n in nodes if n.get('package')==pkg and n.get('resource-id')==pkg+':id/deleted_file_state_cb'and n.get('text')=='已了解应用的风险检测结果'and enabled(n)]
 parents=[n for n in nodes if n.get('package')==pkg and n.get('resource-id')=='android:id/button1'and enabled(n)and(n.get('text')in('Install','安装','继续安装')or any(c.get('resource-id')==pkg+':id/vbutton_title'and c.get('text')=='继续安装'for c in n.iter('node')))]
 titles=[n for n in nodes if n.get('package')==pkg and n.get('resource-id')==pkg+':id/vbutton_title'and n.get('text')=='继续安装'and enabled(n)]
 # A risk checkbox is the only action until a fresh dump proves it checked.
 # The continue button may correctly be disabled in this first dump.
 if len(acks)>1:return []
 if acks and acks[0].get('checked')!='true':selected=[('risk-ack',acks[0])]
 else:
  buttons=parents if parents else titles
  if len(buttons)!=1:return []
  selected=[('continue-install',buttons[0])]
 result=[]
 for kind,n in selected:
  b=re.fullmatch(r'\[(\d+),(\d+)\]\[(\d+),(\d+)\]',n.get('bounds',''))
  if not b:return []
  x,y,z,w=map(int,b.groups())
  if not(0<=x<z<=20000 and 0<=y<w<=20000):return []
  result.append({'kind':kind,'id':n.get('resource-id'),'bounds':n.get('bounds'),'tap':[(x+z)//2,(y+w)//2]})
 return result

def install(dev,apk,expected,out,require,pkg):
 global UNRESOLVED
 dev.stopped();require(not UNRESOLVED,'prior install unresolved: no second install')
 require(not out.exists(),'installer output must be unique');host=out.parent/(out.stem+'-installer');host.mkdir(exist_ok=False)
 token=uuid.uuid4().hex;tmp='/data/local/tmp/d2r-native-one-install-'+token;remote=tmp+'/installer.xml';client=None;created=False;resolved=False;seen=set();state={'token':token,'temporary':tmp,'APK_expected':expected,'actions':[],'owned_client_complete':False,'xml_cleanup_complete':False}
 def ad(*args):return subprocess.check_output(dev.command+list(args),text=True,timeout=20).strip()
 def save():(host/'result.json').write_text(json.dumps(state,indent=2)+'\n')
 def digest(path):
  s=ad('shell',shlex.join(['sh','-c','if test -f "$1"; then sha256sum "$1"; elif test -e "$1"; then exit 3; else echo ABSENT; fi','sh',path]))
  if s=='ABSENT':return None
  h=s.split()[0];require(re.fullmatch('[a-f0-9]{64}',h),'installer temporary digest');return h
 try:
  ad('shell','test','!','-e',tmp);ad('shell','mkdir',tmp);created=True
  ad('shell',shlex.join(['sh','-c','printf "%s\\n" "$2" > "$1"','sh',tmp+'/owner',token]));save()
  # Exactly one install request; all UI actions below belong to this live client.
  with out.open('x')as log:
   UNRESOLVED=True;client=subprocess.Popen(dev.command+['install','-r','-d','--no-incremental',str(apk)],stdout=log,stderr=subprocess.STDOUT);state['client_PID']=client.pid;save();deadline=time.monotonic()+100
   while client.poll()is None:
    if time.monotonic()>deadline:raise TimeoutError('single install pending; preserve transaction, no retry')
    try:
     ad('shell','uiautomator','dump',remote);xml=ad('exec-out','cat',remote)
    except (subprocess.SubprocessError,OSError)as error:
     state.setdefault('UI_observation_errors',[]).append(repr(error));save()
     if client.poll()is None:time.sleep(.5)
     continue
    selected=actions(xml)
    if client.poll()is not None:break
    for action in selected:
     key=(action['kind'],action['id'],action['bounds'])
     if key in seen:continue
     # Preserve this current snapshot in this invocation's unique host directory.
     leaf=host/('action-'+str(len(state['actions']))+'.xml');leaf.write_text(xml)
     action.update(xml_sha256=hashlib.sha256(xml.encode()).hexdigest(),host_xml=str(leaf));state['actions'].append(action);save()
     if client.poll()is not None:break
     ad('shell','input','tap',*[str(v)for v in action['tap']]);seen.add(key)
    time.sleep(.5)
   state['returncode']=client.returncode;require(client.returncode==0,'owned install failed; inspect its log, no retry')
  location=ad('shell','pm','path',pkg).removeprefix('package:');require(location.startswith('/')and '\n'not in location,'unknown installed package location')
  require(ad('shell','sha256sum',location).split()[0]==expected,'installed APK digest mismatch')
  resolved=True;UNRESOLVED=False;state['installed_exact_expected_APK']=True
 except BaseException as error:state['error']=repr(error);raise
 finally:
  if client is not None and client.poll()is None:
   client.terminate()
   try:client.wait(timeout=5)
   except subprocess.TimeoutExpired:client.kill();client.wait(timeout=5)
  state['owned_client_complete']=client is None or client.poll()is not None
  state['install_resolved']=resolved
  if created:
   try:
    require(digest(tmp+'/owner')==hashlib.sha256((token+'\n').encode()).hexdigest(),'installer temp owner changed')
    names=ad('shell','find',tmp,'-type','f').splitlines();require(set(names)<={tmp+'/owner',remote},'unknown installer temporary leaf')
    require(not ad('shell','find',tmp,'-type','l'),'installer temp symlink')
    for path in names:
     h=digest(path);require(h is not None,'installer temporary disappeared')
     ad('shell',shlex.join(['sh','-c','test "$(sha256sum "$1" | cut -d" " -f1)" = "$2" && rm "$1"','sh',path,h]))
    ad('shell','rmdir',tmp);state['xml_cleanup_complete']=True
   except BaseException as error:state['cleanup_error']=repr(error);save();raise
  else:state['xml_cleanup_complete']=True
  save()
