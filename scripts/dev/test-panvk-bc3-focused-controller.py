#!/usr/bin/env python3
"""Offline contract/fault cleanup checks execute real controller functions; no ADB."""
from pathlib import Path
import ast,json,copy
p=Path(__file__).with_name('run-panvk-bc3-focused-fixture.py');tree=ast.parse(p.read_text());functions={n.name:n for n in tree.body if isinstance(n,ast.FunctionDef)}
checks=0
def function(name,env):
 exec(compile(ast.Module(body=[functions[name]],type_ignores=[]),str(p),'exec'),env);return env[name]
body={'test':'original022-BC3-capture-focused','result':'passed','regions':52,'images':7,'sampled_pixels':371731,'raw_BC_bytes':371920,'backend_CPU_BC_decode':False,'game_modules_executed':False,'checks':1488864}
config={'BC3_app_configuration':True,'pid':123,'uid':10174,'provider':'panvk','shared':False,'TRACE':False,'game_modules_executed':False}
terminal={'BC3_app_terminal':True,'pid':123,'uid':10174,'rc':0,'vendor_id':0x13b5,'shared':False,'game_modules_executed':False}
rows=[body,config,terminal];f=function('validate_rows',{});assert f(rows,'123')==terminal;checks+=1
for index,key,value in [(0,'regions',51),(0,'images',6),(0,'sampled_pixels',1),(0,'raw_BC_bytes',1),(0,'backend_CPU_BC_decode',True),(0,'game_modules_executed',True),(0,'checks',3),(1,'pid',999),(1,'shared',True),(1,'TRACE',True),(2,'rc',1),(2,'uid',0),(2,'vendor_id',0x5143)]:
 bad=copy.deepcopy(rows);bad[index][key]=value
 try:f(bad,'123');raise RuntimeError('Accepted invalid row '+key)
 except AssertionError:checks+=1
for bad in [rows+rows[:1],rows+rows[-1:],rows[1:]]:
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
print(json.dumps({'checks':checks,'actual_rows_and_cleanup_functions':True,'wrong_counts_CPU_fallback_duplicates_TRACE_foreign_vendor_rejected':True,'unknown_replacement_crash_no_force_stop':True,'one_exact_owned_fixture_stop':True,'production_game_lifecycle_or_settings_mutation':False,'ADB_access':False}))
