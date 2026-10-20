"""Offline authentication of existing table + real official LLDB native-use records.
No target code, timing or extra device observations. Normal-game identity printf
is stdout, while native.log only receives stderr; its successful LOG follows the
same frozen validator that checks the active driver, three UUIDs and pack hashes.
"""
import hashlib,json,struct

def verify(table_path,composition,raw,native):
 if not __debug__:raise RuntimeError('Native identity verification requires normal Python assertions; optimized execution rejected')
 b=table_path.read_bytes();assert hashlib.sha256(b).hexdigest()==composition['native_table_sha256'],'native table bytes changed'
 assert struct.unpack_from('<8sIIIIQ',b)==(b'DMNTBL2\x00',2,160,1,1040,1216),'exact one-entry native table required'
 c=bytearray(b);c[128:160]=b'\0'*32;assert hashlib.sha256(c).digest()==b[128:160],'table digest'
 recipe=b[208:1200];assert len(recipe)==992 and recipe[:8]==b'DMNRES1\x00'
 c=bytearray(recipe);c[960:992]=b'\0'*32;assert hashlib.sha256(c).digest()==recipe[960:992],'recipe digest'
 size,align=struct.unpack_from('<QQ',recipe,16);assert(size,align)==(135168,4096)
 expected={name:recipe[pos:pos+n].hex()for name,pos,n in [('layout_uuid',112,16),('device_uuid',128,16),('driver_uuid',144,16),('input_sha256',160,32),('payload_sha256',192,32)]}
 assert b[32:64].hex()==composition['native_pack_content_sha256'] and b[64:96].hex()==composition['driver_sha256'] and b[96:128].hex()==recipe[288:320].hex()==composition['producer_source_sha256'],'driver/pack/producer identity'
 assert '[native resources] fixed table installed, 1216 metadata bytes;'in native,'validator success LOG missing'
 # native_resource_install reaches this LOG only after active-driver ELF/hash,
 # three UUIDs, producer, pack and recipe/table digest validation succeeded.
 events=raw.get('events',[]);assert not raw.get('errors'),'LLDB read errors';binds=[e for e in events if e['kind']=='bind_entry'];uses=[e for e in events if e['kind']=='use_validated'];assert len(binds)==1
 bind=binds[0];transfers=[e for e in uses if e['arguments']['stages']&0x1000];fragments=[e for e in uses if e['arguments']['stages']&0x80];assert len(transfers)==1 and len(fragments)==1,'both actual TRANSFER and FRAGMENT native-use records required'
 value=lambda x:x['unsigned'];address=bind['arguments']['address'];assert bind['arguments']['bytes']==size and value(bind['requirements']['size'])==size and value(bind['requirements']['alignment'])==align and address%align==0,'actual Builder/storage alignment'
 typed=transfers[0].get('typed_B2T',{});actual=typed.get('actual_copy',{});parameters=typed.get('parameters',{});assert not typed.get('error') and actual and parameters,'typed actual B2T view/level/ROI required'
 number=lambda values,key:values[key]['unsigned']
 mip=number(actual,'imageSubresource.mipLevel');layer=number(actual,'imageSubresource.baseArrayLayer');layers=number(actual,'imageSubresource.layerCount');assert 0<=mip<9 and layer==0 and layers==1,'actual B2T mip/layer outside recipe'
 offsets=[number(actual,'imageOffset.'+x)for x in ['x','y','z']];extents=[number(actual,'imageExtent.'+x)for x in ['width','height','depth']];dimensions=[max(1,256>>mip),max(1,256>>mip),1]
 assert all(o>=0 and n>0 and o<=limit and n<=limit-o for o,n,limit in zip(offsets,extents,dimensions)),'actual B2T ROI outside native mip'
 assert number(parameters,'src')>0 and number(parameters,'tex')>0 and number(parameters,'has_view')in(0,1),'typed B2T original source/view missing'
 serials=set();pointers=set();pids={bind['PID']};frames=[]
 for use in(transfers[0],fragments[0]):
  pids.add(use['PID']);assert use['arguments']['address']==address and use['arguments']['bytes']==size
  allocation=use['allocation'];assert value(allocation['address'])==address and value(allocation['bytes'])==size and value(allocation['storage.shared_import'])==1 and value(allocation['storage.memory'])!=0 and value(allocation['released'])==0
  assert value(use['owner']['shared'])==1 and value(use['owner']['shared_closing'])==0 and value(use['owner']['identity'])==value(allocation['identity'])
  matched=0
  for row in use.get('caller_images',[]):
   if not row.get('native_entry'):continue
   i=row['image'];entry=row['native_entry'];serials.add(value(i['tex.serial']));pointers.add(value(row['image_pointer']));matched+=1;frames.append(row['function'])
   assert value(i['tex.mem'])==address and value(i['tex.L.size'])==size and value(i['tex.L.align'])==align and value(i['native_pool'])==use['arguments']['token']and value(i['memory'])==value(allocation['storage.memory'])and value(i['initialized'])==1
   assert value(entry['id'])==1 and value(entry['recipe.image_size'])==size and value(entry['recipe.image_alignment'])==align and(value(entry['recipe.format']),value(entry['recipe.width']),value(entry['recipe.height']),value(entry['recipe.levels']))==(158,256,256,9)
   assert row['NVN_packaged_layout_bytes']['hex']=='444d4e0201000000'and{k:v['hex']for k,v in row['recipe_bytes'].items()}==expected,'captured actual recipe/identity mismatch'
  assert matched,'no actual image/recipe on use stack'
 assert len(serials)==len(pointers)==len(pids)==1,'transfer/fragment serial or PID mismatch'
 return {'identity_proven':True,'source':'authenticated table + validator-success stderr LOG + official LLDB raw actual recipe; no stdout JSON expectation','PID':next(iter(pids)),'serial':next(iter(serials)),'entry':1,'bytes':size,'alignment':align,'UUID_and_payload_input_hashes':expected,'driver_sha256':composition['driver_sha256'],'transfer_and_fragment_proven':True,'caller_frames':frames,'actual_B2T':{'mip':mip,'layer':layer,'layers':layers,'offset':offsets,'extent':extents},'boundary':'one material actual transfer and fragment resource use; not FPS, DRAM, broad visual parity or full texture adoption'}
