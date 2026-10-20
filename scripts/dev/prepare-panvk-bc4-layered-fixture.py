#!/usr/bin/env python3
"""One known BC4 pair only; official GFXR JSON/FillMemory data, no replay."""
from pathlib import Path
import argparse,json,hashlib,struct
P=argparse.ArgumentParser();P.add_argument('--converted',type=Path,required=True);P.add_argument('--output',type=Path,required=True);A=P.parse_args();H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert H(A.converted)=='8848a36dfdbcf8a9394ac907faa9180e2b74db02a18c15b0b0416d65d1d63a3a'
images={};bind={};sizes={};maps={};fills=[];ops={};prefix=[];imagebinds={};submit={}
for line in A.converted.open():
 d=json.loads(line);idx=d.get('index',-1)
 if idx>95779:break
 f=d.get('function',{});a=f.get('args',{});n=f.get('name')
 if n=='vkCreateImage':images[a['pImage']]=a['pCreateInfo']
 if n=='vkCreateBuffer':sizes[a['pBuffer']]=a['pCreateInfo']['size']
 if n=='vkBindBufferMemory':bind[a['buffer']]=(a['memory'],a['memoryOffset'])
 if n=='vkMapMemory':maps[a['memory']]=a['offset']
 if n=='vkBindImageMemory':imagebinds[a['image']]=a['memory']
 if n in ['vkCmdCopyBufferToImage','vkCmdCopyImage']:ops[idx]=a
 if 89492<=idx<=89538 and n and ('Cmd'in n or 'CommandBuffer'in n):prefix.append((idx,n,a))
 if n=='vkQueueSubmit'and idx in [94354,95779]:submit[idx]=a
 if idx<=94354 and d.get('meta',{}).get('name')=='FillMemoryCommand':fills.append((idx,d['meta']['args']))
for image,levels,layers in [(15042,8,1),(15286,7,252)]:
 c=images[image];assert c['format']=='VK_FORMAT_BC4_UNORM_BLOCK'and c['imageType']=='VK_IMAGE_TYPE_2D'and c['extent']=={'width':128,'height':128,'depth':1}and c['mipLevels']==levels and c['arrayLayers']==layers
assert submit[94354]['pSubmits'][0]['pCommandBuffers']==[9]and submit[95779]['pSubmits'][0]['pCommandBuffers']==[27]
# Before these source reads, this recording contains no draw/dispatch or buffer
# destination. Earlier B2I destinations bind separate image allocations.
for idx,n,a in prefix:
 assert n in ['vkBeginCommandBuffer','vkCmdResetQueryPool','vkCmdWriteTimestamp','vkCmdPipelineBarrier','vkCmdCopyBufferToImage']
 if n=='vkCmdCopyBufferToImage':assert imagebinds[a['dstImage']]not in [21,36,2292]
chosen=[89521,89522,89523,89530,89532,89534,89536];rows=[];proof=[];payload=bytearray();tight_bytes=0
for mip,index in enumerate(chosen):
 up=ops[index];cp=ops[94773+2*mip];w=128>>mip;bh=(w+3)//4;valid=bh*8
 assert up['commandBuffer']==9 and  up['dstImage']==15042 and  len(up['pRegions'])==1
 z=up['pRegions'][0];sub={'aspectMask':'0x00000001','mipLevel':mip,'baseArrayLayer':0,'layerCount':1}
 assert z['imageSubresource']==sub and z['imageOffset']=={'x':0,'y':0,'z':0}and z['imageExtent']=={'width':w,'height':w,'depth':1}and z['bufferImageHeight']==0
 assert z['bufferRowLength']==(0 if  mip<3 else 128)
 assert cp['commandBuffer']==27 and  cp['srcImage']==15042 and  cp['dstImage']==15286 and  len(cp['pRegions'])==1
 r=cp['pRegions'][0];assert r=={'srcSubresource':sub,'srcOffset':{'x':0,'y':0,'z':0},'dstSubresource':{**sub,'baseArrayLayer':250},'dstOffset':{'x':0,'y':0,'z':0},'extent':{'width':w,'height':w,'depth':1}}
 pitch=valid if mip<3 else 256;span=(bh-1)*pitch+valid;b=up['srcBuffer'];mem,off=bind[b];start=off+z['bufferOffset'];assert maps[mem]==0 and  z['bufferOffset']+span<=sizes[b]
 raw=bytearray(span);covered=bytearray(span);used=[]
 for fi,fill in fills:
  if fill['memory_id']!=mem:continue
  lo=max(start,fill['offset']);hi=min(start+span,fill['offset']+fill['size'])
  if hi<=lo:continue
  p=(A.converted.parent/fill['data']).resolve();assert p.is_relative_to(A.converted.parent.resolve())and p.stat().st_size==fill['size'];data=p.read_bytes();raw[lo-start:hi-start]=data[lo-fill['offset']:hi-fill['offset']];covered[lo-start:hi-start]=b'\1'*(hi-lo);used.append({'index':fi,'file':str(p),'sha256':H(p),'range':[lo,hi]})
 assert all(covered),'Unknown sourcebytes; do not guessGPU result'
 while len(payload)%16:payload.append(0)
 row=[mip,w,w,len(payload),span,pitch,valid,bh,b,index,94773+2*mip,0];rows.append(row);payload+=raw;tight_bytes+=valid*bh;proof.append({'row':row,'source_memory':mem,'memory_range':[start,start+span],'official_fills':used,'actual_source_B2T':up,'actual_T2T':cp})
assert tight_bytes==10928
header=[0x34434250,1,7,0x49,128,8,128,7,252,250,94354,95779,len(payload),tight_bytes,512,128]
body=struct.pack('<16I',*header)+b''.join(struct.pack('<12I',*r)for r in rows)+payload
assert not A.output.exists();A.output.mkdir(parents=True);(A.output/'bc4-layered.bin').write_bytes(body)
manifest={'schema':1,'producer_sha256':H(Path(__file__)),'official_converted_sha256':H(A.converted),'source_capture_sha256':'cc82695545af4cdcddfb62d5c9d13ef9ef4d314167d157f76ed3bf757de69dd5','source_submit_index':94354,'T2T_submit_index':95779,'source_image':15042,'destination_image':15286,'source_levels':8,'destination_levels':7,'destination_layers':252,'only_destination_layer_read':250,'rows':proof,'dataset_sha256':hashlib.sha256(body).hexdigest(),'tight_raw_bytes':tight_bytes,'upload_payload_bytes_with_actual_row_padding':len(payload),'source_prefix_no_shader_or_buffer_writer':prefix,'limitations':['One oldofficialcapture subset, not v8objects/WAIT identities.','First3mips tight; mip3..6 use actual256B rowstride aftercaptured lower-mip rewrites. Sourcebytes fromordered mappedFillMemory throughsubmit94354; not later149328 memory.','Targetunwritten layers and sourceunwrittenmip7 never read.','NoAPIreplay,game,driverchange,timing orGPUcompletion attribution.']};(A.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({'dataset_sha256':manifest['dataset_sha256'],'bytes':len(body),'payload':len(payload),'tight':tight_bytes}))
