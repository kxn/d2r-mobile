"""Actual BC_TRACE-only config and cold-pause predicates, no device."""
import importlib.util,json
from pathlib import Path
P=Path(__file__).with_name('run-panvk-minegraph-game.py');sp=importlib.util.spec_from_file_location('controller',P);m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
checks=0
for line,want in [('[MESA-PANVK] panvk_debug: startup|kbase_diag|bc_trace\n',True),('[MESA-PANVK] panvk_debug: startup|bc_trace\n',False),('[MESA-PANVK] panvk_debug: startup|kbase_diag|bc_trace|trace\n',False),('[MESA-PANVK] panvk_debug: startup|kbase_diag|bc_trace|dump\n',False),('[MESA-PANVK] panvk_debug: startup|kbase_diag|bc_trace|sync\n',False),('[MESA-PANVK] panvk_debug: startup|kbase_diag|bc_trace|shader_map\n',False),('',False)]:assert m.bc_trace_banner(line)==want;checks+=1
assert m.PROPERTIES=={'debug.mesa.panvk.debug':'startup,bc_trace,kbase_diag','debug.mesa.pandecode.dump.file':''};checks+=1
assert len(m.PROPERTIES['debug.mesa.panvk.debug'].encode())<92;checks+=1
A=Path('/home/dl/d2r-mobile/.local/logs/native-bulk-bcbd751-malloc-control-v1/recovery/activities.txt').read_text();assert m.activity_stopped(A,'21077');checks+=1
for a,pid in [(A,'21078'),(A.replace('state=STOPPED','state=PAUSED'),'21077'),(A.replace(m.PKG+'/.MainActivity ',m.PKG+'/.OtherActivity '),'21077'),(A.replace(m.PKG+'/.MainActivity ',m.PKG+'/.MainActivityExtra '),'21077'),(A.replace('* Hist  #0:','*\nHist  #0:'),'21077')]:assert not m.activity_stopped(a,pid);checks+=1
before='[dm] Android native start pid=21077\n[dm] menu\n';fresh='[dm] [nvn] 图形挂起：在途 GPU 工作已清空\n';assert m.cold_pause_proof(before,before+fresh,A,'21077');checks+=1
for b,a in [(before,before),(before,before+'Android background save asked=1 commits=0\n'+fresh),(before,before+'Android background save asked=1 commits=2\n'+fresh),(before,'RESET'+fresh),(before.replace('21077','21078'),before+fresh)]:assert not m.cold_pause_proof(b,a,A,'21077');checks+=1
print(json.dumps({'BC_TRACE_exactflags_no_TRACE_DUMP_SYNC_SHADER_MAP':True,'passed':True,'checks':checks,'actual_activities_fixture_double_space_Hist_and_state_STOPPED':True,'unknown_replacement_and_different_activity_rejected':True,'positive_fresh_drain_and_no_conditional_save_branch_required':True,'device_access':False}))
