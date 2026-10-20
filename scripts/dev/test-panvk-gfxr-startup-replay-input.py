#!/usr/bin/env python3
"""Exercise the real input-admission helper, no SDK/GPU/ADB execution."""
from pathlib import Path
import ast,hashlib,json,tempfile,copy
S=Path(__file__).with_name('run-panvk-gfxr-replay-fixture.py');t=ast.parse(S.read_text());fn=next(n for n in t.body if isinstance(n,ast.FunctionDef)and n.name=='verified_startup_capture');code=compile(ast.fix_missing_locations(ast.Module(body=[fn],type_ignores=[])),str(S),'exec');sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();ns={'json':json,'Path':Path,'sha':sha};exec(code,ns);f=ns['verified_startup_capture'];checks=0
with tempfile.TemporaryDirectory()as tmp:
 D=Path(tmp);cap=D/'owned.gfxr';cap.write_bytes(b'ownedtest'*16);report=D/'result.json'
 base={'controller_sha256':'b8acf68b0befad2c7ddfc86c667c2a332227bd1bd840dd0e30223630756bb2be','candidate_APK':'cc56a9849b28ec338a4a9a1941b9933cac2be689e31c116a0ea48ab175656adc','driver':'c5ecf526c0113c976366818c931173f661adb7df4f519b9a2e9b6ed9456a71b2','apk':'a0bc159c20acc85fe2178a73d3e9f889d23e9422c0bb883982136cafa0f7f4ff','cleanup_errors':[],'official_info_returncode':0,'official_convert_returncode':0,'first_recorded_function':'vkCreateInstance','trim_reconstruction_commands':[],'recorded_submits':[{'file_index':1}],'initial_capture_bytes':128,'capture_properties_effective':{'debug.gfxrecon.'+k:''for k in('capture_queue_submits','capture_frames','capture_draw_calls','capture_trigger','capture_trigger_frames','capture_trigger_boundary','capture_android_trigger')},'capture_raw':{'copy_verified':True,'temp_removed':True,'local_path':str(cap),'bytes':cap.stat().st_size,'sha256':sha(cap)}}
 for k in('capture_stack_mapped','capture_collected','capture_settings_restored','cleanup_complete','all35_restored','all_shaders_restored','official_capture_info_valid','official_capture_convert_valid','untrimmed_prefix_verified'):base[k]=True
 report.write_text(json.dumps(base));assert f(report,cap)==base;checks+=1
 def negative(q):
  global checks
  report.write_text(json.dumps(q))
  try:f(report,cap)
  except AssertionError:checks+=1
  else:raise AssertionError('unsafe input accepted')
 for key,value in [('cleanup_errors',['retainedprocess']),('all_shaders_restored',False),('untrimmed_prefix_verified',False),('official_convert_returncode',1),('trim_reconstruction_commands',['InitBufferCommand']),('controller_sha256','0'*64),('capture_safety_limit','2GiB'),('initial_capture_bytes',0),('recorded_submits',[])]:q=copy.deepcopy(base);q[key]=value;negative(q)
 q=copy.deepcopy(base);q['capture_properties_effective']['debug.gfxrecon.capture_android_trigger']='false';negative(q)
 q=copy.deepcopy(base);q['capture_raw']['sha256']='0'*64;negative(q)
 q=copy.deepcopy(base);q['capture_raw']['local_path']=str(D/'other.gfxr');negative(q)
print('PASS'+str(checks)+': realstartup-source-admission helper positive + parser/trigger/cleanup/size/controller/trim/hash/path negatives; no replay/GPU/ADB')
