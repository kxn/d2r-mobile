"""Organize existing official CSV/ReportLib/TraceProcessor output, no measurement."""
from pathlib import Path
import json,csv,io,hashlib,re,collections
R=Path('/home/dl/d2r-mobile');DATA=R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1';AN=R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v2-analysis'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def tables(p):
 text=p.read_text();result=[]
 for block in text.strip().split('\n\n'):
  row=list(csv.reader(io.StringIO(block)))
  if row and len(row)>1:result.append([dict(zip(row[0],r))for r in row[1:]])
 return result
def number(s):
 if s=='[NULL]':return None
 try:return float(s)
 except ValueError:return s
report={}
for phase in ['whole','window16']:
 d=DATA/phase;a=AN/phase;csvrows=list(csv.reader((d/'official-per-thread-stat.csv').open()));seconds=next(float(r[1])for r in csvrows if r and r[0]=='Total test time');threads={};pid=None
 for row in csvrows:
  if len(row)<5 or not row[1].isdigit()or not row[2].isdigit():continue
  name,proc,tid,count,event=row[:5];pid=int(proc);key=int(tid);t=threads.setdefault(key,{'comm':name,'tid':key,'events':{}})
  if event=='task-clock':value=float(count.removesuffix('(ms)'))
  else:
   try:value=int(count)
   except ValueError:value=None
  t['events'][event]={'count':value,'official_rate':row[5:-1]}
 task_total=sum(t['events'].get('task-clock',{}).get('count',0)for t in threads.values())
 chosen={t['comm']:{**t,'task_clock_ms':t['events']['task-clock']['count'],'mean_cores':t['events']['task-clock']['count']/1000/seconds}for t in threads.values()if t['comm']in ('nvn-translate','dm-gpu-complete')}
 simple=json.load(open(a/'simpleperf-official-caller-analysis.json'));ops=json.load(open(a/'official-existing-operation-samples.json'));capture=json.load(open(d/'official-cpu-capture.json'))
 sched=tables(a/'official-scheduler-sql.txt');bounds=next(t[0]for t in sched if t[0].get('section')=='trace_bounds');duration=float(bounds['seconds']);states=next(t for t in sched if t[0].get('section')=='target_thread_states');cpu={}
 for row in states:
  k=row['comm'];z=cpu.setdefault(k,{'tid':int(row['tid']),'states_ms':{},'trace_wall_ms':duration*1000});z['states_ms'][row['state']]=z['states_ms'].get(row['state'],0)+float(row['state_ms'])
 for z in cpu.values():
  v=z['states_ms'];z['Running_ms']=v.get('Running',0);z['runnable_ms']=v.get('R',0)+v.get('R+',0);z['sleep_S_ms']=v.get('S',0);z['uninterruptible_D_ms']=v.get('D',0);z['covered_state_ms']=sum(v.values());z['uncovered_boundary_ms']=z['trace_wall_ms']-z['covered_state_ms'];z['offCPU_observed_ms']=sum(v.values())-z['Running_ms'];z['mean_cores']=z['Running_ms']/z['trace_wall_ms']
 per_cpu=next(t for t in sched if 'frequency_covered_ms'in t[0] and'cpu'in t[0]);freq=collections.defaultdict(lambda:[0,0,0])
 for row in per_cpu:
  z=freq[row['comm']];z[0]+=float(row['running_ms']);covered=number(row['frequency_covered_ms']);f=number(row['weighted_mhz'])
  if covered is not None and f is not None:z[1]+=covered;z[2]+=covered*f
 for comm,(run,cover,weight)in freq.items():cpu[comm].update(observed_frequency_MHz=weight/cover if cover else None,frequency_coverage_percent=100*cover/run if run else None)
 operations=next(t for t in sched if 'frequency_coverage_pct'in t[0]);selected_operations=[{k:number(v)if k not in ('comm','name')else v for k,v in row.items()}for row in operations if row['name']in ['NVN command replay','Vulkan completion worker fence','Vulkan present call','Vulkan CPU waits queue completion','Vulkan CPU publish','Vulkan completion command pool reset']]
 rawwaits=tables(a/'official-waits-sql.txt');waits=next(t for t in rawwaits if t[0].get('section')=='named_scopes');concurrent=next(t[0]for t in rawwaits if t[0].get('section')=='concurrent_work');pidrun=float(next(t[0]['running_ms']for t in sched if t[0].get('section')=='whole_PID_running'))
 rawstates={k:json.load(open(d/(k+'.json')))for k in ['before-stat','after-stat','before-record','after-record','before-scheduler','after-scheduler']}
 brackets={}
 for kind in ('stat','record','scheduler'):
  before,after=rawstates['before-'+kind],rawstates['after-'+kind]
  props={}
  for group in ('buffer_sync','texture_bindings','pool_lookup','pool_windows','stage_barriers'):
   props[group]={k:after[group][k]-v for k,v in before[group].items()if isinstance(v,int)and not isinstance(v,bool)and k in after[group]and isinstance(after[group][k],int)}
  brackets[kind]={'reported':capture[kind+'_bracket'],'work_delta_ADB_bracket_only':props,'area_hp_level_experience_equal':{k:before[k]==after[k]for k in ['area','hp','level','experience']},'before_position':[before['x'],before['y']],'after_position':[after['x'],after['y']]}
 health=[row for tab in sched for row in tab if row.get('section')=='health_full']
 report[phase]={'ordinary_app_PID':pid,'official_stat':{'seconds':seconds,'whole_PID_task_clock_ms':task_total,'whole_PID_mean_cores':task_total/1000/seconds,'roles':chosen,'source_sha256':sha(d/'official-per-thread-stat.csv'),'official_rates_scope':'Tool reported per-event rates/cycles, not instantaneous CPU frequency or energy.'},
 'official_user99Hz':{'all_PID_samples':simple['all_PID_samples'],'all_PID_event_period':simple['all_PID_period'],'metadata':simple['official_record_metadata'],'roles':[{'tid':t['tid'],'comm':t['comm'],'samples':t['samples'],'event_period':t['event_period'],'self_leafs':t['self_leafs'][:12],'retained_frame_depth_samples':t['retained_frame_depth_samples'],'anchor_presence_samples':t['anchor_presence_samples']}for t in simple['threads']if t['comm']in ['nvn-translate','dm-gpu-complete']],'existing_operation_sample_contexts':ops,'source_sha256':sha(d/'official-simpleperf.data')},
 'official_Perfetto':{'bounds':bounds,'whole_PID_running_ms':pidrun,'whole_PID_mean_cores':pidrun/(duration*1000),'thread_on_offCPU':cpu,'scoped_running_frequency':selected_operations,'named_wait_wall_intervals_nonadditive':waits,'guest_wait_overlap':concurrent,'health_error_or_data_loss_nonzero':health,'source_sha256':sha(d/'completion-scheduler.pftrace')},
 'separate_tool_brackets':brackets,'public_thermal_log':{'path':str(d/'thermal-public-api.log'),'sha256':sha(d/'thermal-public-api.log')},'visual':{'path':str(d/'HUD.png'),'sha256':sha(d/'HUD.png'),'reviewed':'Same original camp/HP55/level1, HUD/globes/belt/terrain/character present, no obvious new black blocks; viewpoint/animation differ slightly, not pixel-equivalent. Overlay25 is not a performance verdict.'}}
result=json.load(open(DATA/'result.json'));assert result['cleanup_complete']and not result['cleanup_errors']
w=report['whole'];n=report['window16'];rel=lambda a,b:100*(b/a-1)
stat_diff={}
for role in ['nvn-translate','dm-gpu-complete']:
 a=w['official_stat']['roles'][role]['task_clock_ms'];b=n['official_stat']['roles'][role]['task_clock_ms'];stat_diff[role]={'whole_ms':a,'window_ms':b,'delta_ms':b-a,'relative_percent':rel(a,b),'description_only':True}
final={'status':'ACTUAL_CAPTURE_RESTORED_OFFLINE_OFFICIAL_ANALYSIS_COMPLETE_DESCRIPTIVE_LIMITS','source_commit':'eddb4ca860d3d60c2d4524b9a697cd79ed2a100a','capture_result':{'path':str(DATA/'result.json'),'sha256':sha(DATA/'result.json')},'UI_cleanup':{'path':str(DATA/'own-ui-cleanup.json'),'sha256':sha(DATA/'own-ui-cleanup.json')},'runs':report,'observed_stat_role_differences':stat_diff,
 'main_findings':['Window mode adds visible steady operation-admission CPU work: vk_pool_operation_begin observed inclusive44/605 NvN samples (7.27%) versus1/596 whole; includes child collectors/pin/locks and overlaps subgroups. Not44 samples all removable, not7.27% frame regression.','Window samples include5 leaf pool_operation_add,3 leaf/15 inclusive pin,2 leaf/4 inclusive unpin,9 resolve,3 leaf/8 inclusive resolve_locked,1 leaf/2 inclusive shared_access_host_phase. The short snapshot lock has only sparse observed context, not a measured bottleneck.','No pool reserve/union samples in either steady record; window promotions13 already occurred before tools, unchanged across all brackets, so this does not measure rare union/initial GUP cost.','Stat NvN CPU5608.93->5772.78ms (+2.92% descriptive), completion316.18->305.27ms (-3.45% descriptive); periods8.003/8.001s, all targets60. PSO173/31 vs181/33 and different work prevent net CPU/FPS/heat attribution.','Perfetto whole hadlatePSO173->180/variants31->33, window181/33 stable. Whole-only19 queue-completion waits426.25ms cannot be assigned to window policy; wall waits overlap CPU/other threads.','Guest SyncWait uses3 rotating objects; observed guest-fence wall5.327s/5.214s overlapped substantial actual translation Running3.377s/3.807s. Not wasted CPU or pureGPU time, and not additive with completion worker fence7.287s/7.640s.'],
 'source_mechanism_boundaries':['Whole window0 returns before collector. Window steady already uses pool_operation_pin fast path BEFORE reserve, no steady allocation/sort; do not propose duplicate reserve elision already implemented.','Additional collector exact-dedup O(n^2), per-range pool/span proof, refs and lock/unlock are concrete added work. Physical identity, complete range/prefix, epoch/reentrancy and retained lifetime must remain for any later optimization.','A future optimization can target actual range collection/pin representation only after preserving these proofs. This report implements no new optimization.'],
 'correctness_and_cleanup':'Both original5walks/HUD/freshsave2stop completed. Exact originald7 APK/6377 ELF/29modules/UI4/950/old driver/cache/temp restored; owned installer XML CAS-cleaned; no game/tools left running; Q returned. No main/runtime/device changes during analysis.',
 'limits':['Stat,99Hz,Perfetto are independent windows; never multiply samplepercent by another window runtime or addtheir costs.','Simpleperf lost/truncated counts reported0; retained callchain root omissions still visible, inclusive functions overlap.','Perfetto reports no nonzero error/data_loss rows in thisquery; boundary state coverage gaps explicitly retained.','Existing global counter brackets extend outside8s; no exact per-frame denominator; shader_present selected0/not a present count.','Same-area/HUD and actualtarget60 are not pixel-identical workload or matchedPSO/variants. No FPS/RAM/energy benefit inferred from this CPUrun; no new memorybaseline.'],
 'artifacts':{'official_ReportLib_analysis':str(AN),'TraceProcessor_SQL':[str(R/('.local/build/shared-window-eddb-cpu-cost-'+p+'-official-scheduler.sql'))for p in ['whole','window16']],'operation_context_filter':str(R/'.local/build/shared-window-eddb-report-existing-operation-samples.py')},'no_extra_device_captures':True}
out=R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1-official-analysis.json';out.write_text(json.dumps(final,indent=2)+'\n')
print(out,sha(out));print('statdiff',stat_diff)
for phase in ['whole','window16']:
 x=report[phase];print(phase,'wholePIDstat',x['official_stat']['whole_PID_task_clock_ms'],'cores',x['official_stat']['whole_PID_mean_cores']);print('scheduler roles',x['official_Perfetto']['thread_on_offCPU'])
