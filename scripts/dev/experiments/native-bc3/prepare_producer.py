#!/usr/bin/env python3
"""Freeze and minimally widen the original producer; no driver, device or game work."""
import argparse, difflib, hashlib, io, json, subprocess, tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
BASE='b11147358b1eb54d1b1863e94b3114768d9ef441'
PROFILE='turnip-coherent-host-bc3-probe-v1'
P=argparse.ArgumentParser();P.add_argument('--output',type=Path,required=True);a=P.parse_args()
out=a.output.resolve()
if out.exists():raise ValueError('Use a new producer directory; never overwrite the old 8064 source or partial output')
ready=ROOT/'.local/logs/turnip-native-bulk-producer-v1-source-ready-v2.json'
r=json.loads(ready.read_text());frozen=ROOT/'.local/worktrees/turnip-native-bulk-producer-v1'
for row in r['producer_source_identity']['CMake_inputs']:
 if hashlib.sha256(Path(row['path']).read_bytes()).hexdigest()!=row['sha256']:raise ValueError('Frozen source differs')
out.mkdir(parents=True);source=out/'source';source.mkdir()
archive=subprocess.check_output(['git','archive',BASE],cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(archive)) as tar:tar.extractall(source,filter='data')
prefix=Path('scripts/dev/native-image-payload');diff=[]
def change(name,before,after):
 p=source/prefix/name;s=p.read_text()
 if s.count(before)!=1:raise ValueError('Non-unique source anchor: '+name)
 t=s.replace(before,after);p.write_text(t)
 diff.extend(difflib.unified_diff(s.splitlines(True),t.splitlines(True),fromfile='a/'+str(prefix/name),tofile='b/'+str(prefix/name)))
change('CMakeLists.txt','option(DM_NATIVE_TURNIP_HOST_ASTC "Explicit experimental Turnip host-image ASTC contract" OFF)',
'''option(DM_NATIVE_TURNIP_HOST_ASTC "Explicit experimental Turnip host-image ASTC contract" OFF)
option(DM_NATIVE_TURNIP_HOST_BC3 "Independent BC3 native producer probe" OFF)
if(DM_NATIVE_TURNIP_HOST_BC3 AND NOT DM_NATIVE_TURNIP_HOST_ASTC)
  message(FATAL_ERROR "BC3 probe requires the original Turnip coherent host contract")
endif()''')
change('CMakeLists.txt','  string(APPEND DM_NATIVE_PRODUCER_CONTENT "contract-profile:turnip-coherent-host-astc-v1\\n")',
'''  if(DM_NATIVE_TURNIP_HOST_BC3)
    string(APPEND DM_NATIVE_PRODUCER_CONTENT "contract-profile:turnip-coherent-host-bc3-probe-v1\\n")
    target_compile_definitions(native_image_payload PRIVATE DM_NATIVE_TURNIP_HOST_BC3=1)
  else()
    string(APPEND DM_NATIVE_PRODUCER_CONTENT "contract-profile:turnip-coherent-host-astc-v1\\n")
  endif()''')
change('CMakeLists.txt','  if(DM_NATIVE_TURNIP_HOST_ASTC)\n    target_compile_definitions(native_image_payload_embed PRIVATE DM_NATIVE_TURNIP_HOST_ASTC=1)\n  endif()',
'''  if(DM_NATIVE_TURNIP_HOST_ASTC)
    target_compile_definitions(native_image_payload_embed PRIVATE DM_NATIVE_TURNIP_HOST_ASTC=1)
  endif()
  if(DM_NATIVE_TURNIP_HOST_BC3)
    target_compile_definitions(native_image_payload_embed PRIVATE DM_NATIVE_TURNIP_HOST_BC3=1)
  endif()''')
change('resource_bake.inc','  CHECK((h->game_format == 95 && h->nvn_format == 0x79 &&',
'''  CHECK(
#ifdef DM_NATIVE_TURNIP_HOST_BC3
        (h->game_format == 61 && h->nvn_format == 0x44 &&
         h->vk_format == VK_FORMAT_BC3_UNORM_BLOCK && h->block == 4) ||
        (h->game_format == 62 && h->nvn_format == 0x48 &&
         h->vk_format == VK_FORMAT_BC3_SRGB_BLOCK && h->block == 4) ||
#endif
        (h->game_format == 95 && h->nvn_format == 0x79 &&''')
change('native_import.inc','  query.format = VK_FORMAT_BC3_UNORM_BLOCK;',
'''#ifdef DM_NATIVE_TURNIP_HOST_BC3
  query.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
#else
  query.format = VK_FORMAT_BC3_UNORM_BLOCK;
#endif''')
change('fixture.c','  if ((format == VK_FORMAT_BC3_UNORM_BLOCK && !bc) ||',
'''  if (((format == VK_FORMAT_BC3_UNORM_BLOCK || format == VK_FORMAT_BC3_SRGB_BLOCK) && !bc) ||''')
change('fixture.c','      if (format == VK_FORMAT_BC3_UNORM_BLOCK) {',
'''      if (format == VK_FORMAT_BC3_UNORM_BLOCK || format == VK_FORMAT_BC3_SRGB_BLOCK) {''')
change('fixture.c','                              VK_FORMAT_BC3_UNORM_BLOCK,',
'''                              VK_FORMAT_BC3_UNORM_BLOCK,
#ifdef DM_NATIVE_TURNIP_HOST_BC3
                              VK_FORMAT_BC3_SRGB_BLOCK,
#endif''')
change('fixture.c','      if (native_mode && formats[n] != VK_FORMAT_ASTC_4x4_UNORM_BLOCK &&',
'''      if (native_mode &&
#ifdef DM_NATIVE_TURNIP_HOST_BC3
          formats[n] != VK_FORMAT_BC3_UNORM_BLOCK && formats[n] != VK_FORMAT_BC3_SRGB_BLOCK &&
#endif
          formats[n] != VK_FORMAT_ASTC_4x4_UNORM_BLOCK &&''')
# Record final complete diff instead of overlapping sequential hunks.
patch=[]
for name in ['CMakeLists.txt','fixture.c','native_import.inc','resource_bake.inc']:
 relative=str(prefix/name);old=subprocess.check_output(['git','show',BASE+':'+relative],cwd=ROOT,text=True);new=(source/relative).read_text()
 patch.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+relative,tofile='b/'+relative))
(out/'producer.patch').write_text(''.join(patch))
inputs={name:hashlib.sha256((source/prefix/name).read_bytes()).hexdigest() for name in ['CMakeLists.txt','fixture.c','native_import.inc','resource_bake.inc','resource_batch.inc','resource_format.h','sample.comp']}
identity=hashlib.sha256((''.join(k+':'+v+'\n' for k,v in inputs.items())+'contract-profile:'+PROFILE+'\n').encode()).hexdigest()
ledger={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(source.rglob('*')) if p.is_file()}
result={'schema':1,'complete':True,'base_revision':BASE,'source':str(source),'profile':PROFILE,'producer_source_sha256':identity,'CMake_inputs':inputs,'source_ledger':ledger,'frozen_ready_sha256':hashlib.sha256(ready.read_bytes()).hexdigest(),'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'patch_sha256':hashlib.sha256((out/'producer.patch').read_bytes()).hexdigest(),'built':False,'device_executed':False,'coherent_qualification':'NOT_GRANTED; requires new exact driver and real GPU fixtures'}
(out/'source-ready.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_ledger'},indent=2))
