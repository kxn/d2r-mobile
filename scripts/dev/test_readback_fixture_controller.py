#!/usr/bin/env python3
"""Offline semantic checks of the exact controller oracle predicate; no ADB."""
import ast, copy, re
from pathlib import Path
source=Path(__file__).with_name('run-readback-invalidate-batch-fixture.py').read_text()
node=next(n for n in ast.parse(source).body if isinstance(n,ast.FunctionDef) and n.name=='validate_success_rows')
space={'re':re};exec(compile(ast.Module(body=[node],type_ignores=[]),'<exact-controller-predicate>','exec'),space)
verify=space['validate_success_rows']
rows=[{'test':'readback-batch','result':'passed','checks':525000,'NC_invalidations':35,'GPU_ROI_count':35,
 'pool_GPU_bytes':True,'event_after_fence':True,'reset_blocked':True,'game_modules_executed':False},
 {'native_vulkan_app_configuration':True,'uid':10250,'pid':123,'validation_layer':'disabled_explicitly',
 'synchronization_validation':'disabled_explicitly','legacy_ASTC_requested':False,'readback_batch_requested':True,'game_modules_executed':False},
 {'native_vulkan_app_terminal':True,'uid':10250,'pid':123,'rc':0}]
log='[vulkan loader] system\n[vulkan readback] preferred HOST_CACHED actual_flags=0xb invalidate_after_fence=1\n'
assert verify(rows,log)['actual_NC']; rejected=0
mutants=[]
for key,value in [('NC_invalidations',0),('NC_invalidations',34),('NC_invalidations',True),('GPU_ROI_count',34),
 ('pool_GPU_bytes',False),('event_after_fence',False),('reset_blocked',False),('game_modules_executed',True),('checks',0),('result','unsupported')]:
 m=copy.deepcopy(rows);m[0][key]=value;mutants.append((m,log))
for index,key,value in [(1,'readback_batch_requested',False),(1,'legacy_ASTC_requested',True),(1,'validation_layer','requested'),
 (2,'rc',1),(2,'pid',124),(2,'uid',10251)]:
 m=copy.deepcopy(rows);m[index][key]=value;mutants.append((m,log))
mutants.extend([(rows+rows[:1],log),(rows[:-1],log),(rows,log+'FATAL'),(rows,log.replace('0xb','0xf'))])
for candidate,text in mutants:
 try: verify(candidate,text)
 except (AssertionError,KeyError):rejected+=1
 else:raise AssertionError('controller accepted invalid NC/order/ownership evidence')
print({'exact_controller_mutants_rejected':rejected,'result':'passed','device_operations':False})
