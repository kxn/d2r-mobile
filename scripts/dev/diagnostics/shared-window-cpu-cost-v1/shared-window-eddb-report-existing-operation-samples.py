"""Filter existing official ReportLib samples; no recording/device/timer calls."""
from pathlib import Path
import sys,json,hashlib
R=Path('/home/dl/d2r-mobile');N=R/'.local/android-sdk/ndk/28.2.13676358';sys.path.insert(0,str(N/'simpleperf'))
from simpleperf_report_lib import ReportLib
functions=['vk_pool_operation_begin','pool_operation_add','pool_operation_pin','pool_operation_unpin','pool_span_resolve','pool_span_resolve_locked','vk_pool_reserve_ranges','pool_policy_promote','shared_pool_union_quiescent','shared_access_host_phase']
for phase in ['whole','window16']:
 sample=R/f'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1/{phase}/official-simpleperf.data';out=R/f'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v2-analysis/{phase}'
 expected=json.load(open(R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1/result.json'))['phases'][phase]['first_PID']
 r=ReportLib();r.SetRecordFile(str(sample));r.SetSymfs(str(R/f'.local/build/shared-import-windows-eddb4ca-cpu-cost-v2-analysis/{phase}/symbols/binary_cache'))
 counts={f:{'leaf_samples':0,'leaf_period':0,'inclusive_samples':0,'inclusive_period':0,'foreign_leaf_contexts':{}}for f in functions};total=0;period=0;ids={}
 while(s:=r.GetNextSample()):
  assert s.pid==expected
  if s.thread_comm!='nvn-translate':continue
  assert r.GetEventOfCurrentSample().name=='cpu-clock:u';total+=1;period+=s.period
  leaf=r.GetSymbolOfCurrentSample();chain=r.GetCallChainOfCurrentSample();frames=[leaf]+[chain.entries[n].symbol for n in range(chain.nr)]
  names={q.symbol_name for q in frames if'libdmhost_android.so'in q.dso_name}
  for q in frames:
   if'libdmhost_android.so'in q.dso_name:ids[q.dso_name]=r.GetBuildIdForPath(q.dso_name)
  for f in functions:
   c=counts[f]
   if leaf.symbol_name==f and'libdmhost_android.so'in leaf.dso_name:c['leaf_samples']+=1;c['leaf_period']+=s.period
   if f in names:
    c['inclusive_samples']+=1;c['inclusive_period']+=s.period
    if'libdmhost_android.so'not in leaf.dso_name:c['foreign_leaf_contexts'][leaf.symbol_name]=c['foreign_leaf_contexts'].get(leaf.symbol_name,0)+1
 r.Close();assert ids and all(v.removeprefix('0x')=='6bf92d2494f59128bb639651bb0d9ee28e199eed'for v in ids.values())
 for c in counts.values():c['inclusive_percent']=100*c['inclusive_period']/period;c['leaf_percent']=100*c['leaf_period']/period
 (out/'official-existing-operation-samples.json').write_text(json.dumps({'source':'Original recorded Simpleperf cpu-clock:u via NDK ReportLib, no new measurement','data_sha256':hashlib.sha256(sample.read_bytes()).hexdigest(),'nvn_samples':total,'nvn_period':period,'recorded_host_BuildIDs':ids,'functions':counts,'limits':['Inclusive groups overlap and retained callchains can be incomplete; do not sum.','Zero observed sample is not zero cost, especially rare union with no promotion in this bracket.','Event period/percentage is sampling weight, not exact call count or whole-frame exclusive wall duration.']},indent=2)+'\n')
 print(phase,{f: (v['leaf_samples'],v['inclusive_samples'])for f,v in counts.items()})
