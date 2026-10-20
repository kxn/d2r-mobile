#!/usr/bin/env python3
"""Extract ONLY 52 known BC3 source ranges from existing official GFXR convert output.
No command replay or capture parser: consumes official JSON + exported FillMemory files.
Payload is the mapped-memory content at captured submit149328, not recording time.
"""
import argparse,hashlib,json,struct
from pathlib import Path
P=argparse.ArgumentParser();P.add_argument('--converted',type=Path,required=True);P.add_argument('--objects',type=Path,required=True);P.add_argument('--output',type=Path,required=True);A=P.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(A.converted)=='8848a36dfdbcf8a9394ac907faa9180e2b74db02a18c15b0b0416d65d1d63a3a'
assert sha(A.objects)=='923a3f836fd5a36119ec5a0d91f420a5ad738924b133f637abb22ae3f46a4f11'
D=json.loads(A.objects.read_text());assert D['submitted_at_index']==149328 and D['ordinal']==186 and D['B2T_API_count']==52
assert D['capture_SHA256']=='cc82695545af4cdcddfb62d5c9d13ef9ef4d314167d157f76ed3bf757de69dd5'
targets=D['B2T_targets'];assert len(targets)==52
images=list(dict.fromkeys(t['dstImage']for t in targets));assert len(images)==7
bindings={};sizes={};map_offsets={};fills=[];observed={}
for line in A.converted.open():
 d=json.loads(line);idx=d.get('index',-1)
 if idx>149328:break
 f=d.get('function',{});a=f.get('args',{});name=f.get('name')
 if name=='vkCreateBuffer':sizes[a['pBuffer']]=a['pCreateInfo']['size']
 if name=='vkBindBufferMemory':bindings[a['buffer']]=(a['memory'],a['memoryOffset'])
 if name=='vkMapMemory':map_offsets[a['memory']]=a['offset']
 if name=='vkCmdCopyBufferToImage':observed[idx]=a
 if d.get('meta',{}).get('name')=='FillMemoryCommand':fills.append((idx,d['meta']['args']))
intervals=[];rows=[];payload=bytearray();x=y=shelf=0
for t in targets:
 assert t['format']in ('VK_FORMAT_BC3_UNORM_BLOCK','VK_FORMAT_BC3_SRGB_BLOCK') and t['imageType']=='VK_IMAGE_TYPE_2D'and t['arrayLayers']==1
 assert len(t['regions'])==1;z=t['regions'][0];assert observed[t['index']]['pRegions']==t['regions']and observed[t['index']]['srcBuffer']==t['srcBuffer']and observed[t['index']]['dstImage']==t['dstImage']
 sub=z['imageSubresource'];w=z['imageExtent']['width'];h=z['imageExtent']['height'];m=sub['mipLevel'];b=((w+3)//4)*((h+3)//4)*16
 assert z['bufferOffset']==z['bufferRowLength']==z['bufferImageHeight']==0 and z['imageOffset']=={'x':0,'y':0,'z':0}and z['imageExtent']['depth']==1
 assert sub=={'aspectMask':'0x00000001','mipLevel':m,'baseArrayLayer':0,'layerCount':1}
 assert w==max(1,t['extent']['width']>>m)and h==max(1,t['extent']['height']>>m)and b==sizes[t['srcBuffer']]
 mem,start=bindings[t['srcBuffer']];assert map_offsets[mem]==0
 raw=bytearray(b);covered=bytearray(b);used=[]
 for idx,fill in fills:
  if fill['memory_id']!=mem:continue
  lo=max(start,fill['offset']);hi=min(start+b,fill['offset']+fill['size'])
  if lo>=hi:continue
  f=(A.converted.parent/fill['data']).resolve();assert f.is_relative_to(A.converted.parent.resolve())and f.is_file()and f.stat().st_size==fill['size']
  data=f.read_bytes();raw[lo-start:hi-start]=data[lo-fill['offset']:hi-fill['offset']];covered[lo-start:hi-start]=b'\1'*(hi-lo)
  used.append({'fill_index':idx,'file':str(f),'sha256':sha(f),'memory_range':[lo,hi]})
 assert all(covered),'Unknown bytes in required capture source'
 if x+w>1024:x=0;y+=shelf;shelf=0
 fmt=0x48 if t['format']=='VK_FORMAT_BC3_SRGB_BLOCK'else 0x44
 row=[images.index(t['dstImage']),fmt,t['extent']['width'],t['extent']['height'],t['mipLevels'],m,w,h,len(payload),b,x,y,t['srcBuffer'],t['index'],0,0]
 rows.append(row);intervals.append({'row':row,'captured_image':t['dstImage'],'source_memory':mem,'source_range':[start,start+b],'payload_sha256':hashlib.sha256(raw).hexdigest(),'ordered_source_fills':used});payload+=raw;x+=w;shelf=max(shelf,h)
height=y+shelf;assert height==640 and len(payload)==371920
assert not A.output.exists();A.output.mkdir(parents=True)
body=struct.pack('<8I',0x33434250,1,52,7,1024,height,len(payload),16)+b''.join(struct.pack('<16I',*r)for r in rows)+payload
(A.output/'bc3-capture.bin').write_bytes(body)
manifest={'schema':1,'kind':'known-original-BC3-capture-functional-subset-not-replay','source_capture_sha256':D['capture_SHA256'],'official_converted_sha256':sha(A.converted),'object_review_sha256':sha(A.objects),'producer_sha256':sha(Path(__file__)),'submit_index':149328,'capture_submit_ordinal':186,'images':images,'regions':52,'payload_bytes':len(payload),'atlas':[1024,height],'dataset_sha256':hashlib.sha256(body).hexdigest(),'rows':intervals,'limitations':['Old c5ec capture inputs only; not v7 object identities or lifetime/WAIT state.','No original game module runs in this GPU fixture.','FillMemory contents composed in official order through submit, never guessed at vkCmdCopy recording time.']}
(A.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({'dataset_sha256':manifest['dataset_sha256'],'bytes':len(body),'regions':52,'images':7,'payload_bytes':len(payload)}))
