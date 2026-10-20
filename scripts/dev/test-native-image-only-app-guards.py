#!/usr/bin/env python3
"""Exact acceptance gate only; no controller import or device commands."""
import ast,copy
from pathlib import Path
p=Path(__file__).with_name('run-turnip-native-window-family-fixture-v1.py')
f=next(n for n in ast.parse(p.read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='validate_native_rows')
ns={};exec(compile(ast.Module(body=[f],type_ignores=[]),str(p),'exec'),ns)
rows=[{'test':'native-resource-original-NVN','result':'passed','checks':447,'format':158,'mips':9,'family_version':2,'tail_levels':5,'no_pool_shadow':True,'full_all_mips':True,'tail_packaged_and_B2T':True,'native_offset':1056768,'pool_bytes':4194304,'game_modules_executed':False},{'test':'native-image-only-original-NVN','result':'passed','checks_added':36,'mips':9,'actual_ASTC_GPU':True,'sample_and_ReadTexels_without_Buffer':True,'CPU_same_value_without_Buffer':True,'late_real_Buffer_copy_payload_exact':True,'late_materialized_only':True},{'test':'native-previous-writer-ASTC-pending-FS','result':'passed','checks':40,'positive_cases':2,'unknown_negative_cases':1,'mips':[4,5,6],'old_TRANSFER_and_COMPUTE_histories_retained':True,'current_recording_denied':True,'both_positive_FS_versions_changed':True,'FS_pixels_reference_equal':True,'CS_pixels_reference_equal':True,'T2B_compressed_blocks_exact':True,'resident_scope_history_preserved':True,'unknown_current_writer_changed_pixels':True,'unknown_without_intermediate_fence':True},{'test':'native-resident-owner-original-ASTC','result':'passed','checks_added':110,'actual_ASTC_GPU':True,'mips':9,'CPU_UNCACHED':True,'rounds':3,'GPU_changed_FS_reference_equal':True,'CPU_restore_all_mips_exact':True,'explicit_and_noFlush':True,'same_value_Flush':True,'three_chunks_without_explicit_host_wait':True,'owner_and_mip_history_retained':True,'Image_writer_to_Buffer_CS_full_payload':True,'game_modules_executed':False},{'native_allocation_lane':True,'imports':1,'import_bytes':135168,'metadata_bytes':184,'same_physical_pages_as_Buffer_imports':True,'no_payload_copy':True,'window_two_unions':True,'last_release_collected':True},{'native_resource_identity':True,'driver_sha256':'aa185'}]
gate=ns['validate_native_rows'];manifest={'driver_sha256':'aa185'};gate(rows,manifest);checks=1
for i,row in enumerate(rows):
 for k,v in row.items():
  bad=copy.deepcopy(rows);bad[i][k]=not v if isinstance(v,bool) else 0 if isinstance(v,int) else [] if isinstance(v,list) else 'bad'
  try:gate(bad,manifest)
  except (AssertionError,KeyError):checks+=1
  else:raise AssertionError((i,k,'invalidaccepted'))
for bad in [[],rows+copy.deepcopy(rows),[r for r in rows if r.get('test')!='native-image-only-original-NVN']]:
 try:gate(bad,manifest)
 except (AssertionError,KeyError):checks+=1
 else:raise AssertionError('partial/duplicategateaccepted')
print('exact resident ASTC plus original447/36/40 rowguards passed',checks,'no ADB')
