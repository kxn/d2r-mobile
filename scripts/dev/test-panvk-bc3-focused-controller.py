#!/usr/bin/env python3
"""Offline contract/fault cleanup checks execute real controller functions; no ADB."""
from pathlib import Path
import ast,json,copy
p=Path(__file__).with_name('run-panvk-bc3-focused-fixture.py');tree=ast.parse(p.read_text());functions={n.name:n for n in tree.body if isinstance(n,ast.FunctionDef)}
checks=0
def function(name,env):
 exec(compile(ast.Module(body=[functions[name]],type_ignores=[]),str(p),'exec'),env);return env[name]
body={'test':'original022-BC4-layered-T2T','result':'passed','BC_format':139,'mips_copied':7,'destination_layers':252,'only_layer_read':250,'source_levels':8,'destination_levels':7,'NVN_submissions':5,'independent_command_objects':5,'explicit_middle_drains':0,'final_fence_waits':1,'sampled_pixels':43688,'raw_BC_bytes_compared':21856,'backend_CPU_BC_decode':False,'game_modules_executed':False,'checks':87454}
config={'BC3_app_configuration':True,'pid':123,'uid':10174,'provider':'panvk','shared':False,'TRACE':False,'game_modules_executed':False}
terminal={'BC3_app_terminal':True,'pid':123,'uid':10174,'rc':0,'vendor_id':0x13b5,'shared':False,'game_modules_executed':False}
rows=[body,config,terminal,{'BC3_app_case':'bc4-layered'}];f=function('validate_rows',{});assert f(rows,'123')==terminal;checks+=1
for index,key,value in [(0,'BC_format',141),(0,'mips_copied',6),(0,'destination_layers',251),(0,'only_layer_read',249),(0,'source_levels',7),(0,'destination_levels',8),(0,'NVN_submissions',4),(0,'independent_command_objects',1),(0,'explicit_middle_drains',1),(0,'final_fence_waits',0),(0,'checks',1488866),(3,'BC3_app_case','single-batch'),(0,'sampled_pixels',1),(0,'raw_BC_bytes_compared',1),(0,'backend_CPU_BC_decode',True),(0,'game_modules_executed',True),(0,'checks',3),(1,'pid',999),(1,'shared',True),(1,'TRACE',True),(2,'rc',1),(2,'uid',0),(2,'vendor_id',0x5143)]:
 bad=copy.deepcopy(rows);bad[index][key]=value
 try:f(bad,'123');raise RuntimeError('Accepted invalid row '+key)
 except AssertionError:checks+=1
for bad in [rows+rows[:1],rows+rows[-1:],rows[1:],rows[:3],rows+[{'test':'original022-BC3-capture-focused'}]]:
 try:f(bad,'123');raise RuntimeError('Accepted missing/duplicate terminal/body')
 except AssertionError:checks+=1
parse=function('parse_oracle_log',{'json':json})
assert parse('\n'.join(map(json.dumps,rows)))[1]=='oracle-passed';checks+=1
assert parse('[dm] FATAL\n'+json.dumps(terminal))[1]=='native-assertion-failed';checks+=1
for raw,first,kind in [('','123','absent'),('123','123','owned'),('123 456','123','ambiguous'),('456','123','replacement'),('bad','123','ambiguous')]:
 env={'first_pid':first,'process':lambda _:raw,'pkg':'owned','save':lambda:None};observe=function('observe_pid',env);phase={};assert observe(phase)[1]==kind;checks+=1
 if kind in('ambiguous','replacement'):assert phase['last_pid_raw']==raw and phase['ownership_ambiguous'];checks+=1
# Actual cleanup helper must not force-stop missing, replacement, ambiguous or
# native-crashing processes. The inherited bounded natural-exit path owns that.
for raw,kind,native in [('456','replacement',False),('123 456','ambiguous',False),('123','owned',True),('','absent',False)]:
 actions=[];phase={'native_terminal_seen':native};env={'active_phase':phase,'first_pid':'123','pkg':'owned','observe_pid':lambda *a,**k:(raw,kind),'wait_natural_exit':lambda _:actions.append('natural-wait'),'ad':lambda *a:actions.append(a),'process':lambda _:''}
 stop=function('stop_owned',env);stop(phase);assert not any(isinstance(x,tuple)and'force-stop'in x for x in actions);checks+=1
 if raw:assert actions==['natural-wait'];checks+=1
# A genuine exact-APK owned functional process can be stopped once.
actions=[];phase={};env={'active_phase':phase,'first_pid':'123','pkg':'owned','observe_pid':lambda *a,**k:('123','owned'),'wait_natural_exit':lambda _:actions.append('unexpected'),'ad':lambda *a:actions.append(a),'process':lambda _:'' ,'exact_fixture':lambda:True};function('stop_owned',env)(phase);assert actions==[('shell','am','force-stop','owned')];checks+=1
# No adopted launch phase: unknown PID is preserved, even for our package name.
actions=[];env={'active_phase':None,'pkg':'owned','result':{},'save':lambda:None,'process':lambda _:'999','ad':lambda *a:actions.append(a)}
try:function('stop_owned',env)();raise RuntimeError('Unowned PID accepted')
except AssertionError:assert not actions;checks+=1
# No production lifecycle/mutation entry is called by this controller.
text=p.read_text();assert 'android-game-background-save.py'not in text and "force-stop',original"not in text and 'setprop'not in text and "'settings','put'"not in text;checks+=1
assert "'--es','fixture_case','bc4-layered'"in text;checks+=1
# Realexisting-tool evidence gate; synthetictext only, noGPUclaim.
import re
shadow=function('validate_bc_shadow',{'re':re})
log='[MESA-PANVK] panvk_debug: startup|bc_trace\n'
for levels,layers in [(8,1),(7,252)]:log+=f'[MESA-PANVK] bc_img: fmt=139 128x128x1 mips={levels} layers={layers} usage=0x7\n'
log+='[MESA-PANVK] bc_copyimg: sfmt=139 dfmt=139 regions=1\n'*7
for layer in [0,250]:
 for mip in range(7):
  w=128>>mip;g=(w+7)//8;log+=f'[MESA-PANVK] bc_decode: fmt=139 idx=1 level={mip} layer={layer}+1 ext={w}x{w}x1 disp={g}x{g}x1 src=0x100000 dst=0x200000\n'
assert shadow(log)['T2T_API_record_count']==7;checks+=1
for bad in [log.replace('startup|bc_trace','startup|trace|bc_trace'),log.replace('fmt=139','fmt=141'),log.replace('layer=250+1','layer=249+1'),log.replace('ext=2x2x1','ext=4x4x1'),log.replace('[MESA-PANVK] bc_copyimg: sfmt=139 dfmt=139 regions=1\n','',1),log+'[MESA-PANVK] bc_copyimg: sfmt=139 dfmt=139 regions=1\n',log.replace('mips=7 layers=252','mips=7 layers=1')]:
 try:shadow(bad);raise RuntimeError('Badshadowevidenceaccepted')
 except AssertionError:checks+=1
print(json.dumps({'checks':checks,'BC4_no_oldcase_no_hidden_drains_exactlayer_mips_raw_counts':True,'actual_rows_and_cleanup_functions':True,'wrong_counts_CPU_fallback_duplicates_TRACE_foreign_vendor_rejected':True,'unknown_replacement_crash_no_force_stop':True,'one_exact_owned_fixture_stop':True,'production_game_lifecycle_or_settings_mutation':False,'ADB_access':False}))
