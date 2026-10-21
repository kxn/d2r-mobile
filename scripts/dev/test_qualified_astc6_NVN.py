#!/usr/bin/env python3
"""Offline real-artifact identity + finite parser/row rejection tests; no ADB/GPU."""
import ast,copy,hashlib,json,struct,tempfile,shutil
from pathlib import Path
import qualified_astc6_NVN as q
R=q.ROOT;checks=0

def rejected(call,needle=None):
 global checks
 try:call()
 except (AssertionError,ValueError,KeyError)as error:
  if needle:assert needle in str(error),(needle,error)
  checks+=1
 else:raise AssertionError('invalid input accepted')

for fmt in (165,166):
 a=R/f'.local/build/native-qualified-astc6-NVN-{fmt}-v2';m=q.read(a/'candidate.apk.build.json');q.validate_artifact(a,m);checks+=1
 entry,baked,query,proof=q.provenance(fmt);data=Path(entry['input']['path']).read_bytes();q.input_contract(data,fmt);checks+=1
 for offset,value in [(24,128),(32,8),(36,158),(40,4),(44,81)]:
  bad=bytearray(data);struct.pack_into('<I',bad,offset,value);rejected(lambda bad=bad:q.input_contract(bad,fmt))
 rejected(lambda:q.input_contract(data,166 if fmt==165 else 165));rejected(lambda:q.input_contract(data[:100],fmt))
 bad=bytearray(data);mip=proof['mips'][4];o=mip['offset'];bad[o+16:o+32]=bad[o:o+16];struct.pack_into('<32s',bad,152+4*56+24,hashlib.sha256(bad[o:o+mip['bytes']]).digest())
 rejected(lambda:q.input_contract(bad,fmt),'two different real ASTC mip4 blocks')
 for key,value in [('native_fixture_expected_format',158),('native_payload_bytes',94208),('native_resource_producer_sha256','0'*64)]:
  bad=copy.deepcopy(m);bad[key]=value;rejected(lambda bad=bad:q.validate_artifact(a,bad))
 bad=copy.deepcopy(m);bad['qualified_ASTC6']['VkFormat']=158;rejected(lambda:q.validate_artifact(a,bad))
 # A malicious table and pack JSON agree on their own new SHA: the unchanged
 # strict loader must still reject mismatching real recipe/family bytes.
 for label,offset in [('VkFormat',160+48+32),('tail_mip',1216-8)]:
  with tempfile.TemporaryDirectory(prefix='qualified-astc6-NVN-')as tmp:
   target=Path(tmp)/'artifact';shutil.copytree(a,target);bad=copy.deepcopy(m);pack_path=target/'pack/native-pack.json';table=target/'resource4/native-layouts.bin';raw=bytearray(table.read_bytes());raw[offset]^=1;raw[128:160]=hashlib.sha256(raw[:128]+bytes(32)+raw[160:]).digest();table.write_bytes(raw)
   pack=q.read(pack_path);pack['table_sha256']=q.sha(table);pack_path.write_text(json.dumps(pack)+'\n');bad['native_pack']=q.rec(pack_path);bad['owned_resource_inputs']['native-layouts.bin']=q.sha(table)
   rejected(lambda:q.validate_artifact(target,bad),'serialized native recipe/family differs')
# Keep original ASTC4 row guard in the same real controller: no imports/execution.
p=Path(__file__).with_name('run-native-astc-linear-NVN-companion-v1.py');node=next(n for n in ast.parse(p.read_text()).body if isinstance(n,ast.FunctionDef)and n.name=='validate_native_rows');ns={};exec(compile(ast.Module([node],type_ignores=[]),str(p),'exec'),ns)
old=Path(__file__).with_name('test_native_linear_NVN_companion_guards.py').read_text();scope={};exec(compile(old,str(p.with_name('test_native_linear_NVN_companion_guards.py')),'exec'),dict(__file__=str(p.with_name('test_native_linear_NVN_companion_guards.py'))))
rows=[{'test':'native-resource-original-NVN','result':'passed','checks':447,'format':165,'mips':9,'family_version':2,'tail_levels':5,'no_pool_shadow':True,'full_all_mips':True,'tail_packaged_and_B2T':True,'native_offset':1056768,'pool_bytes':4194304,'game_modules_executed':False},{'test':'native-image-only-original-NVN','result':'passed','checks_added':36,'mips':9,'actual_ASTC_GPU':True,'sample_and_ReadTexels_without_Buffer':True,'CPU_same_value_without_Buffer':True,'late_real_Buffer_copy_payload_exact':True,'late_materialized_only':True},{'test':'native-previous-writer-ASTC-pending-FS','result':'passed','checks':40,'positive_cases':2,'unknown_negative_cases':1,'mips':[4,5,6],'old_TRANSFER_and_COMPUTE_histories_retained':True,'current_recording_denied':True,'both_positive_FS_versions_changed':True,'FS_pixels_reference_equal':True,'CS_pixels_reference_equal':True,'T2B_compressed_blocks_exact':True},{'native_allocation_lane':True,'imports':1,'import_bytes':53248,'metadata_bytes':184,'same_physical_pages_as_Buffer_imports':True,'no_payload_copy':True,'window_two_unions':True,'last_release_collected':True},{'native_resource_identity':True,'driver_sha256':q.DRIVER}]
rejected(lambda:ns['validate_native_rows'](rows,{'driver_sha256':q.DRIVER,'native_payload_bytes':53248,'native_fixture_expected_format':165}))
for fmt in (165,166):
 manifest={'driver_sha256':q.DRIVER,'native_payload_bytes':53248,'native_fixture_expected_format':fmt,'linear_candidate_mode':'qualified-ASTC6','qualified_ASTC6':{'VkFormat':fmt}};rows[0]['format']=fmt;ns['validate_native_rows'](rows,manifest);checks+=1
 for i,row in enumerate(rows):
  for k,v in row.items():
   bad=copy.deepcopy(rows);bad[i][k]=not v if isinstance(v,bool)else 0 if isinstance(v,int)else []if isinstance(v,list)else'bad';rejected(lambda bad=bad:ns['validate_native_rows'](bad,manifest))
 bad=copy.deepcopy(rows);bad[0]['format']=158;rejected(lambda:ns['validate_native_rows'](bad,manifest))
print(json.dumps({'passed':True,'checks':checks,'real_artifacts':2,'source_APK_modified':False,'synthetic_rows_are_acceptance_guard_tests_only':True,'device_access':False,'GPU_run':False}))
