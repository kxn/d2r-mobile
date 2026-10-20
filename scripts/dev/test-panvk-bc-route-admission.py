#!/usr/bin/env python3
"""Real admission/restore helpers, offline synthetic records, no GPU/ADB."""
import ast,json,re,shlex,tempfile,copy
from pathlib import Path
p=Path(__file__).with_name('run-panvk-gfxr-replay-fixture.py');t=ast.parse(p.read_text());names={'verified_rebind_gfx_reference','panvk_debug_environment_absent','write_debug_property'};defs=[n for n in t.body if isinstance(n,ast.FunctionDef)and n.name in names];assert len(defs)==3
ns={'json':json,'re':re,'shlex':shlex};exec(compile(ast.fix_missing_locations(ast.Module(body=defs,type_ignores=[])),str(p),'exec'),ns);checks=0
for v in ['', 'PATH=/system/bin\0HOME=/data\0']:assert ns['panvk_debug_environment_absent'](v);checks+=1
for v in ['PANVK_DEBUG=\0','A=1\0PANVK_DEBUG=bc_compute\0']:
 try:ns['panvk_debug_environment_absent'](v)
 except AssertionError:checks+=1
 else:raise AssertionError('env override accepted')
current='';calls=[]
def ad(*args):
 global current
 calls.append(args)
 if args[:2]==('shell','setprop'):
  q=shlex.split(' '.join(args[1:]));assert len(q)==3 and q[0]=='setprop';current=q[2]
 else:assert args==('shell','getprop','debug.mesa.panvk.debug');return current
ns.update(ad=ad,plan={'debug_property_name':'debug.mesa.panvk.debug'})
for value in ['0','bc_compute','','startup,sync',"a b;'c"]:ns['write_debug_property'](value);assert current==value;checks+=1
with tempfile.TemporaryDirectory()as td:
 d=Path(td);path=d/'result.json';before={'owned':'original'};base={'panvk_bc_route':'gfx','memory_translation':'rebind','debug_property_requested':'0','production_unchanged':True,'original_before':before,'original_after':before,'cleanup_errors':[],'first_PID':44,'APK_SHA256':'a','driver_SHA256':'b','capture_SHA256':'c','baseline_promotion_SHA256':'d','controller_SHA256':'e'}
 for k in ('fixture_PID_absent','fixture_uninstalled','owned_tmp_absent','owned_clients_stopped','debug_property_restored','initial_PANVK_DEBUG_env_absent'):base[k]=True
 log='[replay bridge] configured pid=44 x\nidentity verified driverID=20\nCore Checks\n  - Synchronization\nVK_ERROR_DEVICE_LOST\nTOOL-ADDED BDA official-rebind captureReplay_added=0 multiDevice_added=0 tool-rebind-bda=on'
 cat='API call at index: 149328 thread: 2 vkQueueSubmit returned error value VK_ERROR_DEVICE_LOST'
 (d/'replay.log').write_text(log);(d/'PID-logcat.txt').write_text(cat);(d/'mesa.log').write_text('timeout on subqueue 0')
 ex={k:base[k]for k in ('APK_SHA256','driver_SHA256','capture_SHA256','baseline_promotion_SHA256','controller_SHA256')}
 path.write_text(json.dumps(base));assert ns['verified_rebind_gfx_reference'](path,ex)==base;checks+=1
 for key,value in [('panvk_bc_route','compute'),('memory_translation','none'),('debug_property_requested',''),('production_unchanged',False),('cleanup_errors',['leftprocess']),('debug_property_restored',False),('initial_PANVK_DEBUG_env_absent',False),('controller_SHA256','other'),('first_PID',99)]:
  q=copy.deepcopy(base);q[key]=value;path.write_text(json.dumps(q))
  try:ns['verified_rebind_gfx_reference'](path,ex)
  except AssertionError:checks+=1
  else:raise AssertionError('unsafe reference admitted '+key)
 path.write_text(json.dumps(base));(d/'replay.log').write_text(log+'\nVUID-test-00001')
 try:ns['verified_rebind_gfx_reference'](path,ex)
 except AssertionError:checks+=1
 else:raise AssertionError('VUID admitted')
print('PASS'+str(checks)+': actual property quoting empty/nonempty, env priority rejection, exact gfx-rebind matching failure/cleanup/controller admission; offline only')
