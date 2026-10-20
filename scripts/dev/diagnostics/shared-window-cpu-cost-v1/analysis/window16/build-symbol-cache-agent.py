"""Offline orchestration of NDK BinaryCacheBuilder methods; no device source."""
from pathlib import Path
import sys,json,hashlib,subprocess,re
R=Path('/home/dl/d2r-mobile');ndk=R/'.local/android-sdk/ndk/28.2.13676358';sys.path.insert(0,str(ndk/'simpleperf'))
from binary_cache_builder import BinaryCacheBuilder,BinaryCache
out=Path('/home/dl/d2r-mobile/.local/build/shared-import-windows-eddb4ca-cpu-cost-v2-analysis/window16/symbols');out.mkdir(exist_ok=False);inputs=out/'inputs';inputs.mkdir();cache=out/'binary_cache';cache.mkdir()
elf=R/'.local/build/shared-import-windows-eddb4ca-canonical-source/six-libraries/libdmhost_android.so';assert hashlib.sha256(elf.read_bytes()).hexdigest()=='52ddc501fa0639f4644b846e7504220c59568d65e8c24013969745dc284ceb52'
(inputs/'libdmhost_android.so').symlink_to(elf)
(inputs/'libvulkan_freedreno.so').symlink_to(R/'.local/build/turnip-host-import-v2/driver/libvulkan_freedreno.so')
(inputs/'libc++_shared.so').symlink_to(ndk/'toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so')
b=BinaryCacheBuilder(str(ndk),True);b.binary_cache_dir=cache;b.binary_cache=BinaryCache(cache)
b.collect_used_binaries(str(R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1/window16/official-simpleperf.data'));assert b.copy_binaries_from_symfs_dirs([inputs]);b.create_build_id_list()
llvm=ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin';bid=re.search(r'Build ID: (\w+)',subprocess.check_output([str(llvm/'llvm-readelf'),'-n',str(elf)],text=True))[1]
module_entries={k:v for k,v in b.binaries.items()if'libdmhost_android.so'in k};assert module_entries and all(v.removeprefix('0x')==bid for v in module_entries.values())
for path,ident in module_entries.items():assert hashlib.sha256(b.binary_cache.get_path_in_cache(path,ident).read_bytes()).hexdigest()=='52ddc501fa0639f4644b846e7504220c59568d65e8c24013969745dc284ceb52'
report={'scope':'OfficialNDK BinaryCacheBuilder.collect_used_binaries/copy_binaries_from_symfs_dirs/create_build_id_list only. Devicepull/source method deliberately nevercalled. No rebuilt/mutated ELF.','build_id':bid,'elf':str(elf),'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'host_module_entries':module_entries,'all_recorded_module_build_ids':b.binaries,'cache':str(cache),'cache_files':[{'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}for p in cache.rglob('*')if p.is_file()]}
p=Path('/home/dl/d2r-mobile/.local/logs/shared-import-windows-eddb4ca-cpu-cost-v2-analysis/window16/symbol-cache.json');p.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
