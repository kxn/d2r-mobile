"""Pinned real ASTC6 resources for the unchanged owned 0a54 NVN oracle.

Offline resource preparation/identity validation only. No GPU/layout encoder,
new resource protocol, device operation or synthetic acceptance receipt.
"""
from pathlib import Path
import hashlib,importlib.util,json,shutil,struct
ROOT=Path('/home/dl/d2r-mobile')
DRIVER='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
PRODUCER='8064b80022370de2832964ee22015539441981e7272270b0ff01f5b6c6062ca3'
LAYOUT='ab91c540847448360a44496b50045159'
PREP=ROOT/'.local/build/native-qualified-astc6-pilot32-v1/inputs/batch-prepare.json'
ACTUAL=ROOT/'.local/logs/native-qualified-astc6-pilot32-f830-q-v1'
COMPOSE=ROOT/'.local/build/native-qualified-astc6-pilot32-compose-v1'
PACKER=ROOT/'.local/worktrees/native-image-only-astc-app-v2/scripts/dev/pack-native-texture.py'
DRIVER_READY=ROOT/'.local/logs/turnip-native-astc-whole-linear-v1-source-ready.json'
PINS={
 ROOT/'.local/logs/native-qualified-astc6-pilot32-f830-q-v1-root-actual-review.json':'ff67805136174483a6d4fdbf623e11a30169707fc39fddbdc68cdbe0c0ab8ac2',
 ROOT/'.local/logs/native-qualified-astc6-d1fa237-source-ready-agent.json':'57a49fa74674961c6352dfa83352489c204ae0f0f3ecd6364ff99ad7ed04b4e3',
 COMPOSE/'native-overlay/composition.json':'8676e748175eecbc3bdcfc42038e428775cc5917c3293eec56bc5335f55806f3',
 COMPOSE/'native-pack/native-pack.json':'395107559ddf96fa8a95573e0b525fedc8256d7475152095f54f8167d83fe7c0',
 PREP:'157ba1ced6f267c998a9a2b934dac5bd85a7afe04773dc85c2c8ac83c1e05b4b',
 ACTUAL/'result.json':'61e97da921fb5fd77ec8faa017945241ce652fedf4701e955bfa10fcbf758e89',
 ACTUAL/'actual-batch-receipt-index.json':'35c903c14fb02facd545762aea1a8306af65aa8600acc7611749c11aacf0d334',
 ROOT/'.local/logs/native-astc-linear-NVN-root-actual-review.json':'9089fbe5d64b3842e8ae387506e6a22c9cc2034881a76e2711a675407f05af0f',
 PACKER:'f4ae133c09071606a48fce987ac9c1da442266e89d217a386c471eea8ee1b5cb',
 DRIVER_READY:'d056732a67157f95c662a08ed2cf4757b2f864c3733f40ee10c8b49dde810f5b',
}
CASES={165:'data/hd/character/enemy/andariel/textures/andariel_armor_ktint.texture',
 166:'data/hd/env/texture/act1/town/act1_town_camp/act1_town_camp_firepit01_a_alb.texture'}
def require(value,message):
 if not value:raise AssertionError(message)
def sha(path):
 with Path(path).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def rec(path):path=Path(path).resolve();return dict(path=str(path),sha256=sha(path),bytes=path.stat().st_size)
def read(path):return json.loads(Path(path).read_text())
def record(value):
 require(sha(value['path'])==value['sha256'],'record SHA mismatch')
 if 'bytes'in value:require(Path(value['path']).stat().st_size==value['bytes'],'record length mismatch')
 return Path(value['path'])
def input_contract(data,fmt):
 require(len(data)>=1080,'short original resource header')
 magic,version,header,length=struct.unpack_from('<8sIIQ',data)
 width,height,levels,vk,block,game,nvn,reserved=struct.unpack_from('<8I',data,24)
 require((magic,version,header,length)==(b'DMRSRC1\0',1,1080,len(data)),'original resource header identity')
 require((width,height,levels,vk,block,game,nvn,reserved)==(256,256,9,fmt,6,*{165:(99,125),166:(85,139)}[fmt],0),'qualified original resource shape/format')
 mips=[]
 for level in range(16):
  w,h,offset,size,digest=struct.unpack_from('<IIQQ32s',data,152+56*level)
  if level>=levels:
   require(not any((w,h,offset,size))and digest==bytes(32),'unused mip nonzero');continue
  require((w,h)==(width>>level,height>>level)and offset>=header and offset<=len(data)and size<=len(data)-offset,'original mip extent')
  require(size==((w+5)//6)*((h+5)//6)*16 and hashlib.sha256(data[offset:offset+size]).digest()==digest,'original ASTC6 compressed mip identity')
  mips.append(dict(level=level,width=w,height=h,offset=offset,bytes=size,sha256=digest.hex()))
 changed=mips[4];offset=changed['offset']
 require(changed['bytes']>=32 and data[offset:offset+16]!=data[offset+16:offset+32],'existing oracle requires two different real ASTC mip4 blocks')
 return mips

def provenance(fmt):
 require(fmt in CASES,'unsupported explicit ASTC6 case')
 for path,digest in PINS.items():require(sha(path)==digest,'qualified evidence changed: '+str(path))
 root=read(next(path for path in PINS if path.name=='native-qualified-astc6-pilot32-f830-q-v1-root-actual-review.json'))
 require(root['status']=='ROOT_REVIEWED_ACTUAL_F830_ASTC6_32_BAKE_AND_CLEANUP_PASS'and root['entry_count']==32 and root['original_unchanged']and root['cleanup_complete'],'actual32 root acceptance')
 record(root['result'])
 comp=read(COMPOSE/'native-overlay/composition.json');pack=read(COMPOSE/'native-pack/native-pack.json');ready=read(next(path for path in PINS if path.name=='native-qualified-astc6-d1fa237-source-ready-agent.json'))
 require(comp['complete']and comp['entry_count']==6217 and comp['replaced_entries']==32 and comp['unchanged_native_entries']==6185 and comp['selected_quality_approved_ASTC6_thumbnails_exact']and comp['upstream_UI_preserved']and not comp['source_or_payload_byte_patch']and not comp['game_binary_modified'],'complete original composition contract')
 record(ready['validated_all_output_pins'])
 require(ready['composition']['sha256']==PINS[COMPOSE/'native-overlay/composition.json']and not ready['new_actual_ASTC6_NVN_consumer_accepted'],'composition receipt is not new NVN acceptance')
 require(comp['driver_sha256']==pack['driver_binary_sha256']==DRIVER and comp['producer_source_sha256']==pack['producer_source_sha256']==PRODUCER and pack['complete']and pack['family_protocol']==2 and len(pack['entries'])==6217,'composition driver/producer/family identity')
 table=COMPOSE/'native-pack/native-layouts.bin';require(sha(table)==pack['table_sha256']==comp['native_table_sha256'],'complete composed table changed')
 prepared=read(PREP);require(prepared['complete']and len(prepared['entries'])==32,'qualified selection coverage')
 entries=[e for e in prepared['entries']if e['logical'].lower()==CASES[fmt]];require(len(entries)==1,'qualified case missing');entry=entries[0]
 require(entry['id']==hashlib.sha256(CASES[fmt].encode()).hexdigest(),'case ID mismatch')
 for key in ('source','texture','input','prepare_report'):record(entry[key])
 proofs=[e for e in comp['entry_proofs']if e['logical'].lower()==CASES[fmt]];require(len(proofs)==1,'selected composition proof missing');proof=proofs[0]
 require(proof['source']==entry['source']and proof['texture']==entry['texture'],'composition selected source differs')
 baked=Path(proof['actual_baked']);require(baked==ACTUAL/'batches/0000/items'/entry['id'],'baked outside actual owned batch')
 index=read(ACTUAL/'actual-batch-receipt-index.json');require(index['complete']and index['entries']==32 and index['batch_count']==1,'actual index incomplete');batch_path=record(index['results'][0]);batch=read(batch_path)
 require(batch['stage']=='actual-turnip-native-resource-batch-owned-passed'and not batch['cleanup_errors']and batch['original_unchanged']and all(batch[k]for k in ('fixture_PID_absent','fixture_uninstalled','fixture_tmp_absent','owned_clients_stopped')),'unclean actual batch')
 require(batch['driver_sha256']==DRIVER and batch['producer_source_sha256']==PRODUCER and batch['entry_count']==32 and batch['prepare_manifest_sha256']==PINS[PREP],'actual batch identity')
 for name in ('resource-batch-complete.json','resource-batch-receipts.jsonl'):
  require(sha(batch_path.parent/name)==batch['baked_outputs'][name],'actual batch receipt changed')
 receipts=[json.loads(line)for line in (batch_path.parent/'resource-batch-receipts.jsonl').read_text().splitlines()];rows=[row for row in receipts if row['entry_id']==entry['id']];require(len(rows)==1,'actual entry receipt absent');row=rows[0]
 require(row['complete']and row['original_driver_roundtrip_compressed_blocks_exact']and row['input_sha256']==entry['input']['sha256']and row['declaration_sha256']==entry['source']['sha256'],'selected actual source/input mismatch')
 require((row['format'],row['width'],row['height'],row['levels'],row['physical_image_bytes'],row['alignment'])==(fmt,256,256,9,53248,4096),'qualified actual shape/size')
 require(all(row[k]==root['actual_three_UUIDs'][k]for k in ('layout_UUID','device_UUID','driver_UUID'))and row['layout_UUID']==LAYOUT and row['producer_source_sha256']==PRODUCER,'actual UUID/producer mismatch')
 for name,key in [('native-resource.bin','native_payload_sha256'),('native-resource.json','native_metadata_sha256'),('native-resource-layout.bin','layout_record_sha256')]:
  require(sha(baked/name)==row[key]==batch['baked_outputs']['items/'+entry['id']+'/'+name],'actual opaque output changed')
 detail=next(e for e in pack['entries']if e['logical'].lower()==CASES[fmt]);require(detail['image_size']==53248 and detail['thumbnail_mip']==4 and detail['native_payload_sha256']==row['native_payload_sha256']and detail['source_declaration_sha256']==entry['source']['sha256'],'composed selected native detail')
 query=read(baked/'native-resource.json');require(query['complete']and query['driverID']==18 and query['layout_UUID']==LAYOUT and query['physical_image_bytes']==53248 and query['recipe']['VkFormat']==fmt and query['recipe']['mipLevels']==9,'actual query disagrees')
 mips=input_contract(Path(entry['input']['path']).read_bytes(),fmt)
 return entry,baked,query,{'VkFormat':fmt,'logical':CASES[fmt],'entry_id':entry['id'],'mips':mips,'actual_baked':str(baked),'evidence':{str(path):digest for path,digest in PINS.items()},'source':entry['source'],'input':entry['input'],'texture':entry['texture'],'native_payload_sha256':row['native_payload_sha256'],'actual_query':rec(baked/'native-resource.json'),'native_NVN_or_game_accepted':False,'same_block_mip4_cases_excluded':True,'old_ASTC4_receipt_is_ancestry_only':True}

def prepare(args,base,canonical,manifest):
 entry,baked,query,proof=provenance(args.qualified_format)
 out=args.output.resolve();out.mkdir(parents=True);shutil.copy2(canonical,out/'candidate.apk');shutil.copytree(base/'vulkan-fixture',out/'vulkan-fixture');shutil.copy2(base/'fixture-hashes.json',out/'fixture-hashes.json');(out/'resource4').mkdir()
 spec=importlib.util.spec_from_file_location('unchanged_strict_ASTC6_packer',PACKER);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.build([{'source':entry['source']['path'],'texture':entry['texture']['path'],'baked':str(baked)}],out/'pack',DRIVER_READY,driver_query=baked/'native-resource.json')
 pack=read(out/'pack/native-pack.json');require(pack['complete']and pack['family_protocol']==2 and len(pack['entries'])==1 and pack['driver_binary_sha256']==DRIVER and pack['producer_source_sha256']==PRODUCER and pack['entries'][0]['thumbnail_mip']==4,'original single-resource pack failed')
 for name,path in {'native-layouts.bin':out/'pack/native-layouts.bin','native-resource.bin':baked/'native-resource.bin','resource-input.bin':Path(entry['input']['path'])}.items():shutil.copy2(path,out/'resource4'/name)
 manifest.update(native_resource_pack_sha256=pack['pack_content_sha256'],native_resource_producer_sha256=PRODUCER,native_payload_bytes=53248,owned_resource_inputs={name:sha(out/'resource4'/name)for name in ['native-layouts.bin','native-resource.bin','resource-input.bin']},driver_sha256=DRIVER,apk_sha256=sha(out/'candidate.apk'),original_thin_owned_APK=rec(canonical),linear_candidate_mode='qualified-ASTC6',native_fixture_expected_format=args.qualified_format,qualified_ASTC6=proof,actual_query=rec(baked/'native-resource.json'),native_pack=rec(out/'pack/native-pack.json'),preparer=rec(Path(__file__)),source_runtime_unmodified='0a54ef2ce1902f1967f00ee28d8765fa7a978942',no_shader_changes=True,requires_actual_matrix_before_execute=True,installed=False,performance_claim=False)
 (out/'qualified-ASTC6.json').write_text(json.dumps(proof,indent=2)+'\n');(out/'candidate.apk.build.json').write_text(json.dumps(manifest,indent=2)+'\n');validate_artifact(out,manifest)
 return {'artifact':str(out),'mode':'qualified-ASTC6','APK':rec(out/'candidate.apk'),'descriptor':rec(out/'candidate.apk.build.json'),'actual_GPU_or_NVN_run':False}

def validate_artifact(artifact,manifest):
 require(manifest['linear_candidate_mode']=='qualified-ASTC6','not explicit qualified mode');fmt=manifest['native_fixture_expected_format'];entry,baked,query,proof=provenance(fmt)
 require(manifest['qualified_ASTC6']==proof and read(Path(artifact)/'qualified-ASTC6.json')==proof,'qualified proof changed')
 require(manifest['native_payload_bytes']==53248 and manifest['driver_sha256']==DRIVER and manifest['native_resource_producer_sha256']==PRODUCER,'qualified artifact identity')
 require(sha(Path(artifact)/'resource4/native-resource.bin')==proof['native_payload_sha256']and sha(Path(artifact)/'resource4/resource-input.bin')==entry['input']['sha256'],'qualified owned bytes differ')
 pack_path=record(manifest['native_pack']);require(pack_path.resolve()==(Path(artifact)/'pack/native-pack.json').resolve(),'single pack outside owned artifact');pack=read(pack_path);table=Path(artifact)/'resource4/native-layouts.bin';require(sha(table)==pack['table_sha256']==manifest['owned_resource_inputs']['native-layouts.bin']and len(table.read_bytes())==1216,'one-resource family table')
 require(pack['identity']==LAYOUT+'1e63ed8a4bbaa1bb6e51b8904fb7eb2f'+'3baa219f622918ab3ddda14a19a7cba6' and pack['driver_binary_sha256']==DRIVER and pack['producer_source_sha256']==PRODUCER and pack['pack_content_sha256']==manifest['native_resource_pack_sha256']and len(pack['entries'])==1,'qualified single table identity')
 detail=pack['entries'][0];require(detail['native_payload_sha256']==proof['native_payload_sha256']and detail['source_declaration_sha256']==entry['source']['sha256']and detail['thumbnail_mip']==4 and detail['image_size']==53248,'qualified single entry contract')
 require(sha(Path(artifact)/'candidate.apk')=='0c5fbf472fcb3b597100de0c02cddebb3ee679d883ce63d23354b2fd62ed459f','original thin APK changed')

 # Reuse the original strict loader to prove every serialized recipe byte,
 # rather than trusting a self-reported single-pack JSON/table SHA pair.
 spec=importlib.util.spec_from_file_location('qualified_original_strict_loader',PACKER);loader=importlib.util.module_from_spec(spec);spec.loader.exec_module(loader)
 expected,_,_,loaded_detail,_=loader.load_entry({'source':entry['source']['path'],'texture':entry['texture']['path'],'baked':str(baked)},1)
 raw=table.read_bytes();require(raw[160:1200]==expected and raw[1200:]==struct.pack('<QII',loaded_detail['original_layout_data'],4,0),'serialized native recipe/family differs from actual producer')
 require(raw[:8]==b'DMNTBL2\0'and raw[32:64].hex()==manifest['native_resource_pack_sha256']and raw[64:96].hex()==DRIVER and raw[96:128].hex()==PRODUCER,'single table header identity')
 require(hashlib.sha256(raw[:128]+bytes(32)+raw[160:]).digest()==raw[128:160],'single table digest')
