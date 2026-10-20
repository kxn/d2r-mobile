from pathlib import Path
import hashlib,json,importlib.util,sys,subprocess
R=Path('/home/dl/d2r-mobile');P='8064b80022370de2832964ee22015539441981e7272270b0ff01f5b6c6062ca3';A=R/'.local/build/turnip-native-bulk-6217-aa185-v1';A.mkdir(exist_ok=False)
def H(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def record(p):return {'path':str(Path(p).resolve()),'sha256':H(p),'bytes':Path(p).stat().st_size}
prepared_path=R/'.local/build/turnip-native-bulk-act1-character-full4-v1-inputs/batch-prepare.json';prepared=json.loads(prepared_path.read_text());assert H(prepared_path)=='e68a8d45226c2820d2b1fc4d3a3f67b62a6c88e0950b86cc7c837a2da07143c8'
ready_path=R/'.local/logs/turnip-native-bulk-controller-v2-source-ready.json';ready=json.loads(ready_path.read_text());assert H(ready_path)=='1f88055509ac41148b4d6cea1c97b48a119d929c67511be7c401d00ea5b2066c'
tooltree=R/'.local/worktrees/turnip-bulk-producer-gates-v3';sys.path.insert(0,str(tooltree/'scripts/dev'))
spec=importlib.util.spec_from_file_location('composer',tooltree/'scripts/dev/compose-bulk-turnip-native-overlay.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
inputs=[];results=[];global_reports=[];tar_bytes=0
for label,count,entry_count in [('first1',1,128),('remaining48',48,6089)]:
 root=R/'.local/logs'/('turnip-native-bulk-controller-v2-q-'+label);p=root/'result.json';d=json.loads(p.read_text());assert d['stage']=='actual-turnip-native-resource-batches-owned-passed' and len(d['runs'])==count and d['entry_count']==entry_count and not d['cleanup_errors']
 assert all(d.get(key) is True for key in ['original_unchanged','fixture_PID_absent','fixture_uninstalled','fixture_tmp_absent','owned_clients_stopped']);assert d['original_before']==d['original_after']
 assert d['producer_source_sha256']==P and d['driver_sha256']=='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'
 assert d['controller_sha256']==ready['ordinary_UID_controller']['sha256']
 installer=d['installer_receipt'];assert H(installer['path'])==installer['sha256'];i=json.loads(Path(installer['path']).read_text());assert i['owned_client_complete'] and i['xml_cleanup_complete'] and i['install_resolved']
 global_reports.append(record(p));inputs.extend([record(p),record(installer['path'])])
 index_path=root/'actual-batch-receipt-index.json';index=json.loads(index_path.read_text());assert index['complete'] and index['entries']==entry_count and len(index['results'])==count and index['producer_source_sha256']==P and index['source_pack_manifest_sha256']==H(prepared_path)
 assert H(index_path)==d['receipt_index_sha256'];inputs.append(record(index_path));results.extend(index['results'])
 for run in d['runs']:
  assert run['PID_absent_immediately_before_launch'] and run['PID_absent_before_tar'] and run['PID_absent_after'] and run['owned_phase_removed_after_verified_pull']
  assert run['result']=='passed' and run['stage']=='oracle-passed' and run['final']['formats']==run['actual_batch_complete']['entries']
  archive=run['archive'];assert H(archive['path'])==archive['sha256'] and Path(archive['path']).stat().st_size==archive['bytes'];tar_bytes+=archive['bytes']
  maps=root/(run['name']+'-maps.txt');mapped=[]
  for line in maps.read_text().splitlines():
   cols=line.split(None,5)
   if len(cols)!=6 or cols[5] not in run['driver_alias_stat']:continue
   st=run['driver_alias_stat'][cols[5]];major,minor=[int(x,16) for x in cols[3].split(':')]
   assert (major,minor,int(cols[4]))==(st['major'],st['minor'],st['inode']) and st['sha256']==d['driver_sha256'];mapped.append(line)
  assert mapped==run['mapped_driver'];inputs.append(record(maps))
index={'schema':1,'complete':True,'producer_source_sha256':P,'source_pack_manifest_sha256':H(prepared_path),'results':results,'scope':'All49 exact actual ordinaryUID batch receipts; first128 retained, not rebaked','entries':6217,'batches':49,'source_transactions':global_reports}
index_path=A/'actual-batch-receipt-index.json';index_path.write_text(json.dumps(index,indent=2)+'\n')
h=module.Fingerprints();acceptance_path=R/'.local/logs/native-one-game-bb4ac54-q-v3-root-functional-acceptance.json';acceptance=module.receipt_gate(acceptance_path,'8a946c344f3e7a7d1d1d41e3d1b29c57e80c651d8a3264ad620251b351a8ecd0',h)
module.producer_gate.source_ready_gate(ready_path,H(ready_path),P,acceptance,h)
first_result=json.loads(Path(results[0]['path']).read_text());first_id=prepared['batches'][0]['entry_ids'][0];query=Path(results[0]['path']).parent/'items'/first_id/'native-resource.json'
coverage=module.producer_gate.actual_batches_gate(index_path,H(index_path),ready,P,prepared,H(prepared_path),acceptance,query,h)
assert len(coverage)==6217
specs=[];sizes=[];formats={};levels={};shapes={}
for entry in prepared['entries']:
 cov=coverage[entry['id']];specs.append({'source':entry['source']['path'],'texture':entry['texture']['path'],'baked':cov['baked']})
 meta=json.loads(Path(cov['metadata']).read_text());sizes.append(meta['physical_image_bytes']);f=meta['recipe']['VkFormat'];formats[f]=formats.get(f,0)+1;l=meta['recipe']['mipLevels'];levels[l]=levels.get(l,0)+1;shape=f"{meta['recipe']['width']}x{meta['recipe']['height']}";shapes[shape]=shapes.get(shape,0)+1
specs_path=A/'entries.json';specs_path.write_text(json.dumps(specs,indent=2)+'\n')
assert sum(sizes)==696385536
report={'status':'ALL6217_ACTUAL_ORIGINAL_BAKE_RECEIPTS_VERIFIED_NOT_GAME_SAMPLING_PERFORMANCE','source_ready':record(ready_path),'prepared':record(prepared_path),'actual_index':record(index_path),'entries':record(specs_path),'actual_driver_query':record(query),'producer_source_sha256':P,'driver_sha256':'aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db','three_UUIDs':{k:json.loads(query.read_text())[k] for k in ['layout_UUID','device_UUID','driver_UUID']},'source_transactions':global_reports,'actual_batches':49,'entries_count':6217,'first128_rebaked':False,'native_payload_bytes':sum(sizes),'max_native_entry_bytes':max(sizes),'native_entry_size_counts':{str(size):sizes.count(size) for size in sorted(set(sizes))},'formats':formats,'levels':levels,'shapes':shapes,'source_packaged_payload_bytes':414950400,'linear_ASTC_bytes':401317552,'native_vs_source_packaged_byte_increase':sum(sizes)-414950400,'native_table_fixed_bytes':160+6217*1056,'source_quality':'Identical existing full4 ASTC source/thumbnail per selected entry, no reencoding','all_archive_sha_verified':True,'raw_tar_total_bytes':tar_bytes,'maps_alias_inode_sha_rechecked':True,'ordinary_UID_complete_cleanup_rechecked':True,'gate_source':record(tooltree/'scripts/dev/native_bulk_producer_gate.py'),'verified_files':h.finish(),'additional_source_records':inputs,'limits':['Actual processing through original driver host image copy roundtrip, not all6217 sampled GPU/game adoption','Source/native/table extents are not resident RAM saving','AA native tiling padding increases packaged payload bytes; cost retained','No device access, game, FPS/DRAM/heat claim']}
p=A/'actual-receipts-verification.json';p.write_text(json.dumps(report,indent=2)+'\n');print(p,H(p));print('verified',len(coverage),'native',sum(sizes),'formats',formats,'max',max(sizes));print('query',query)
