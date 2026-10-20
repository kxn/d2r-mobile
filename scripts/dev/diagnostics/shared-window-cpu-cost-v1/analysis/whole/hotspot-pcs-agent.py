from pathlib import Path
import sys,collections,json,hashlib,subprocess,re
R=Path('/home/dl/d2r-mobile');N=R/'.local/android-sdk/ndk/28.2.13676358';sys.path.insert(0,str(N/'simpleperf'));from simpleperf_report_lib import ReportLib
OUT=R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v2-analysis/whole';sample=R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1/whole/official-simpleperf.data';elf=R/'.local/build/shared-import-windows-eddb4ca-canonical-source/six-libraries/libdmhost_android.so';cache=R/'.local/build/shared-import-windows-eddb4ca-cpu-cost-v2-analysis/whole/symbols/binary_cache';driver=R/'.local/build/turnip-host-import-v2/driver/libvulkan_freedreno.so';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
r=ReportLib();r.SetRecordFile(str(sample));r.SetSymfs(str(cache));stats={};addresses=set();driverids={};records=[]
while(s:=r.GetNextSample()):
 if s.thread_comm not in ('nvn-translate','dm-gpu-complete'):continue
 sy=r.GetSymbolOfCurrentSample();ch=r.GetCallChainOfCurrentSample();fr=[]
 for q in [sy]+[ch.entries[k].symbol for k in range(ch.nr)]:
  f={'module':q.dso_name,'symbol':q.symbol_name,'pc':int(q.vaddr_in_file)}
  if not fr or fr[-1]!=f:fr.append(f)
  if 'libdmhost_android.so'in f['module']:addresses.add(f['pc'])
  if 'libvulkan_freedreno.so'in f['module']:driverids[f['module']]=r.GetBuildIdForPath(f['module'])
 g=stats.setdefault(s.thread_comm,{'samples':0,'period':0,'self_pcs':collections.Counter(),'self_union':collections.Counter(),'inclusive_union':collections.Counter(),'actual_stacks':collections.Counter()});g['samples']+=1;g['period']+=s.period
 g['self_pcs'][(sy.dso_name,sy.symbol_name,int(sy.vaddr_in_file))]+=s.period
 def cats(f):
  n=f['symbol'];ds=f['module'];v=set()
  if n in ('vk_texture_shared_linear_access','vk_texture_shared_linear_conflict','alias_range_overlap','image_shared_alias_eligible'):v.add('linear_image_alias_guard')
  if n.startswith('alias_range_') and n!='alias_range_overlap':v.add('alias_index_cold_maintenance')
  if 'nvn_dirty'in n or 'cpu_observe'in n or 'cpu_observation'in n:v.add('dirty_and_CPU_observation')
  if 'image_upload'in n or 'nvn_deswizzle'in n or 'decode_astc'in n:v.add('image_deswizzle_decode_upload')
  if 'libvulkan_freedreno.so'in ds:v.add('Turnip_driver')
  if n in ('__memmove_aarch64_simd','memcpy','__memcpy_aarch64_simd'):v.add('libc_memcpy_memmove')
  if n in ('shader_bindings_selected_impl','texture_view_binding_impl','vk_texture_prepare_binding','vk_texture_descriptor'):v.add('shader_texture_binding_named_functions')
  return v
 for c in cats(fr[0]):g['self_union'][c]+=s.period
 for c in set().union(*(cats(f)for f in fr)):g['inclusive_union'][c]+=s.period
 if cats(fr[0])or s.thread_comm=='dm-gpu-complete':g['actual_stacks'][tuple((f['module'],f['symbol'],f['pc'])for f in fr[:24])]+=s.period
r.Close();assert driverids and all(v.removeprefix('0x')=='ca5a176f0769a98c705978593e297d1df74db2b7'for v in driverids.values())
LLVM=N/'toolchains/llvm/prebuilt/linux-x86_64/bin';raw=subprocess.check_output([str(LLVM/'llvm-addr2line'),'-a','-f','-C','-i','-e',str(elf),*[hex(v)for v in sorted(addresses)]],text=True);loc={};at=None
for l in raw.splitlines():
 if re.fullmatch('0x[0-9a-f]+',l):at=int(l,16);loc[at]=[]
 elif at is not None:loc[at].append(l)
threads={}
for name,g in stats.items():
 def table(c):return [{'category':k,'period':v,'percent':100*v/g['period']}for k,v in c.most_common()]
 def frame(ds,sym,pc):return {'module':ds,'symbol':sym,'pc':hex(pc),'exact_host_source':loc.get(pc)if'libdmhost_android.so'in ds else None}
 threads[name]={'samples':g['samples'],'period':g['period'],'self_unions':table(g['self_union']),'inclusive_unions_not_additive':table(g['inclusive_union']),'self_PCs':[dict(frame(*k),period=v,percent=100*v/g['period'])for k,v in g['self_pcs'].most_common(100)],'actual_stacks':[{'period':v,'percent':100*v/g['period'],'frames':[frame(*f)for f in k]}for k,v in g['actual_stacks'].most_common(60)]}
report={'scope':'OfficialReportLib existing99Hz usercpu record exact frozeneddb ELF and driver3c symbols, LLVMaddr2line inline locations. No new capture/device.','sample_sha256':h(sample),'ELF_sha256':h(elf),'Turnip_driver_sha256':h(driver),'Turnip_recorded_BuildID_verified':driverids,'threads':threads,'not_measured':['kernelCPU','GPUtime','DRAMbytes','exactcallcounts','perframeCPU','instantfrequency'],'analysis_source_sha256':h(Path(__file__))}
p=OUT/'hotspot-pcs-agent.json';p.write_text(json.dumps(report,indent=2)+'\n');print(p,h(p));
for n,g in threads.items():print(n,g['samples'],g['self_unions'])
