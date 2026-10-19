#!/usr/bin/env python3
"""Exercise official RenderDoc device-specific capture without a remote controller.
Only configures existing vendor layers and reads the independent probe's report.
"""
import argparse,json,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--layer',default='VK_LAYER_RENDERDOC_ARM_Capture')
a=p.parse_args();pkg='com.d2rmobile.devprobe';base=['adb','-s',a.serial]
assert not a.output.exists();a.output.mkdir(parents=True)
def adb(*args):return subprocess.check_output(base+list(args),text=True,timeout=20).strip()
keys=['enable_gpu_debug_layers','gpu_debug_app','gpu_debug_layers','gpu_debug_layer_app','gpu_debug_layers_gles']
before={k:adb('shell','settings','get','global',k)for k in keys}
(a.output/'before-settings.json').write_text(json.dumps(before,indent=2)+'\n')
try:
 for k,v in {'enable_gpu_debug_layers':'1','gpu_debug_app':pkg,'gpu_debug_layers':a.layer,'gpu_debug_layer_app':'org.renderdoc.renderdoccmd.arm64'}.items():adb('shell','settings','put','global',k,v)
 adb('shell','settings','delete','global','gpu_debug_layers_gles')
 adb('shell','am','force-stop',pkg)
 adb('shell','run-as',pkg,'rm','-f','files/report.json')
 print(adb('shell','am','start','-W','-n',pkg+'/.ProbeActivity','--ez','renderdoc_capture','true'),flush=True)
 for _ in range(30):
  r=subprocess.run(base+['exec-out','run-as',pkg,'cat','files/report.json'],text=True,capture_output=True,timeout=10)
  # exec-out may return host status 0 before the remote file exists.
  try:report=json.loads(r.stdout) if r.returncode==0 else None
  except json.JSONDecodeError:report=None
  if report is not None:
   (a.output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
   capture=report.get('renderdoc',{})
   if capture.get('ended')==1 and capture.get('captures') and capture.get('file'):
    dest=a.output/'probe.rdc'
    with dest.open('wb')as stream:subprocess.run(base+['exec-out','run-as',pkg,'cat',capture['file']],stdout=stream,check=True,timeout=20)
    assert dest.stat().st_size>1024
   break
  time.sleep(1)
 else:raise RuntimeError('Report not observed; leave the existing process in place')
finally:
 for k,v in before.items():adb('shell','settings',*(['delete','global',k]if v=='null'else['put','global',k,v]))
 after={k:adb('shell','settings','get','global',k)for k in keys};(a.output/'after-settings.json').write_text(json.dumps(after,indent=2)+'\n');assert after==before
