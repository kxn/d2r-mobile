"""Offline grouping of official Simpleperf samples/unwound frames + LLVM lines."""
from pathlib import Path
import sys,collections,json,hashlib,subprocess,re
R=Path('/home/dl/d2r-mobile');NDK=R/'.local/android-sdk/ndk/28.2.13676358';sys.path.insert(0,str(NDK/'simpleperf'))
from simpleperf_report_lib import ReportLib
sample=R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1/window16/official-simpleperf.data';elf=R/'.local/build/shared-import-windows-eddb4ca-canonical-source/six-libraries/libdmhost_android.so';cache=Path('/home/dl/d2r-mobile/.local/build/shared-import-windows-eddb4ca-cpu-cost-v2-analysis/window16/symbols/binary_cache');LLVM=NDK/'toolchains/llvm/prebuilt/linux-x86_64/bin';expected_bid=json.load(open(Path('/home/dl/d2r-mobile/.local/logs/shared-import-windows-eddb4ca-cpu-cost-v2-analysis/window16/symbol-cache.json')))['build_id']
expected_pid=json.load(open(R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1/result.json'))['phases']['window16']['first_PID']
r=ReportLib();r.SetRecordFile(str(sample));r.SetSymfs(str(cache));metadata=r.MetaInfo();groups={};addresses=set();bids={};whole=collections.Counter();whole_dso=collections.Counter();all_samples=0
interesting={'pool_span_resolve_locked','pool_span_require_locked','pool_span_resolve','pool_operation_pin','pool_address','vk_pool_operation_begin','shader_bindings_selected_impl','sync_page','sync_queued_page','sync_queued_page_changed','record_pool_range','mtl_sub_index','buffer_stage_pending','vk_texture_view_binding','pool_access_cache_take','pool_access_cache_retire','pool_access_cache_trim','nvn_dirty_since','dirty_since','image_upload_contents','image_upload_contents_cold','image_get','vk_texture_view_binding','nvn_dirty_protect','nvn_dirty_cpu_observation_begin','nvn_dirty_cpu_observation_end','vk_pool_shared_cpu_observe_prepare','vk_pool_shared_cpu_observe_begin','vk_pool_shared_cpu_observe_end','submission_complete_effects','vkResetCommandPool','memcpy'}
while(s:=r.GetNextSample()):
 assert s.pid==int(expected_pid);event=r.GetEventOfCurrentSample().name;assert event=='cpu-clock:u';sy=r.GetSymbolOfCurrentSample();chain=r.GetCallChainOfCurrentSample();fr=[]
 for x in [sy]+[chain.entries[k].symbol for k in range(chain.nr)]:
  f=(x.dso_name,x.symbol_name,int(x.vaddr_in_file))
  if not fr or fr[-1]!=f:fr.append(f)
  if'libdmhost_android.so'in f[0]:bids[f[0]]=r.GetBuildIdForPath(f[0]);addresses.add(f[2])
 g=groups.setdefault((s.pid,s.tid,s.thread_comm),{'period':0,'samples':0,'self_modules':collections.Counter(),'self_leaf':collections.Counter(),'inclusive':collections.Counter(),'foreign_nearest_app':collections.Counter(),'hot_leaf_callers':collections.Counter(),'depth':collections.Counter(),'anchor_present':collections.Counter(),'actual_stacks':collections.Counter()});g['period']+=s.period;g['samples']+=1;all_samples+=1;whole[(s.tid,s.thread_comm)]+=s.period;whole_dso[sy.dso_name]+=s.period
 g['self_modules'][sy.dso_name]+=s.period;g['self_leaf'][(sy.dso_name,sy.symbol_name)]+=s.period;g['depth'][len(fr)]+=1
 for name in ['vk_draw','vk_draw_record','mtl_exec','x_thread','__pthread_start(void*)']:
  if any(x[1]==name for x in fr):g['anchor_present'][name]+=1
 for key in set((x[0],x[1])for x in fr):g['inclusive'][key]+=s.period
 if 'libdmhost_android.so'not in sy.dso_name:
  near=next((x for x in fr if'libdmhost_android.so'in x[0]),None)
  if near:g['foreign_nearest_app'][(sy.dso_name,sy.symbol_name,near[1],near[2])]+=s.period
 if sy.symbol_name in interesting:
  caller=next((x for x in fr[1:]if'libdmhost_android.so'in x[0]),None)
  g['hot_leaf_callers'][(sy.symbol_name,caller[1]if caller else '<no-app-caller>',caller[2]if caller else 0)]+=s.period
  g['actual_stacks'][(sy.symbol_name,tuple(fr[:18]))]+=s.period
r.Close();assert bids and all(x.removeprefix('0x')==expected_bid for x in bids.values())
lines=subprocess.check_output([str(LLVM/'llvm-addr2line'),'-a','-f','-C','-i','-e',str(elf),*[hex(x)for x in sorted(addresses)]],text=True);locations={};at=None
for line in lines.splitlines():
 if re.fullmatch('0x[0-9a-f]+',line):at=int(line,16);locations[at]=[]
 elif at is not None:locations[at].append(line)
rows=[];total=sum(whole.values())
for (pid,tid,comm),g in sorted(groups.items(),key=lambda kv:-kv[1]['period']):
 def arr(c,names,limit=50):
  return [dict(zip(names,key),period=period,percent=100*period/g['period'])for key,period in c.most_common(limit)]
 foreign=arr(g['foreign_nearest_app'],['module','leaf','nearest_app','pc'],40)
 for d in foreign:d['source']=locations.get(d['pc']);d['pc']=hex(d['pc'])
 callers=arr(g['hot_leaf_callers'],['leaf','caller','pc'],80)
 for d in callers:d['source']=locations.get(d['pc']);d['pc']=hex(d['pc'])
 stacks=[]
 for (leaf,frames),period in g['actual_stacks'].most_common(50):
  stacks.append({'leaf':leaf,'period':period,'percent':100*period/g['period'],'frames':[{'module':d,'symbol':n,'pc':hex(pc),'source':locations.get(pc)if'libdmhost_android.so'in d else None}for d,n,pc in frames]})
 rows.append({'pid':pid,'tid':tid,'comm':comm,'samples':g['samples'],'event_period':g['period'],'whole_PID_period_percent':100*g['period']/total,'self_modules':[{'module':k,'period':v,'percent':100*v/g['period']}for k,v in g['self_modules'].most_common()],'self_leafs':arr(g['self_leaf'],['module','symbol']),'observed_inclusive':arr(g['inclusive'],['module','symbol']),'foreign_nearest_app':foreign,'hot_leaf_callers':callers,'actual_hot_stacks':stacks,'retained_frame_depth_samples':dict(g['depth']),'anchor_presence_samples':dict(g['anchor_present'])})
report={'method':'ExistingofficialSimpleperf ReportLib weightedcpu-clock:uperiods and actualretainedunwoundframes; officialLLVM exact frozeneddb ELF inline source. Inclusiveunique(module,symbol)oncepersample, notexclusiveorwholewalltime. No recording, customsampling or targettimer.','data':str(sample),'data_sha256':hashlib.sha256(sample.read_bytes()).hexdigest(),'ELF':str(elf),'ELF_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'ELF_build_id':expected_bid,'recorded_build_ids_verified':bids,'official_record_metadata':metadata,'all_PID_samples':all_samples,'all_PID_period':total,'whole_self_modules':[{'module':k,'period':v,'percent':100*v/total}for k,v in whole_dso.most_common()],'threads':rows,'limitations':['cpu-clock:uperiodshares are notretiredinstructions, exactcallcounts, exclusivewalltime orGPUtime.','Simpleperf windowis separatefromPerfetto; do notmultiplyitsproportionsbydifferentwindowreplaytime.','Keepfailedunwinds and nojoiner preserveactualavailablepaths; rootanchorabsence isreported, notassertedvalidfullunwind. Inclusiveancestorscanmissunwoundframes; do notsum overlappinginclusivepercentages.','Vendorobfuscatedsymbols are retainedliterally. Knownnearbyhostcallers provide observedcallcontext, notproofwhichvendorinternalalgorithmpolls/waits.']}
f=Path('/home/dl/d2r-mobile/.local/logs/shared-import-windows-eddb4ca-cpu-cost-v2-analysis/window16/simpleperf-official-caller-analysis.json');f.write_text(json.dumps(report,indent=2)+'\n');print(f)
for d in rows[:8]:
 print(d['comm'],d['tid'],'samples',d['samples'],'PIDshare',d['whole_PID_period_percent'],'modules',d['self_modules'])
 if d['comm']=='nvn-translate':print('NvN callers',d['hot_leaf_callers']);print('foreign',d['foreign_nearest_app'][:12]);print('anchors',d['anchor_presence_samples'])
