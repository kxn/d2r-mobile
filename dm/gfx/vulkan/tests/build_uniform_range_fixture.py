#!/usr/bin/env python3
"""Wrap existing owned real GLSL fixtures; never mutate a game shader/cache."""
from pathlib import Path
import argparse,hashlib,json,shutil,struct,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools"))
import dmsh_uniform_specialization as specialization
sha=lambda b:hashlib.sha256(b).digest()
W=lambda b:struct.unpack('<%dI'%(len(b)//4),b)
def envelope(base,legacy,candidate,binding,reg):
 h=[0x33505344,1,len(legacy),len(candidate)//4,binding,reg,192]
 return struct.pack('<7I',*h)+sha(base)+sha(legacy)+sha(candidate)+bytes(4)+legacy+candidate

def build(ordinary,pixel,out):
 assert not out.exists();shutil.copytree(ordinary,out)
 for p in pixel.iterdir():
  if p.is_file():shutil.copyfile(p,out/p.name)
 key=hashlib.sha256(bytes([40])*0x880).hexdigest()[:16]
 source=out/(key+'.frag');text=source.read_text();text=text.replace('void main()', 'layout(set=1,binding=2,std140) uniform RangeInput {vec4 range_data[4096];};\nvoid main()')
 before,end=text.rsplit('}',1);source.write_text(before+'color.r+=range_data[0].x;}'+end)
 subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(source),'-o',str(out/(key+'.spv'))],check=True,capture_output=True)
 base=(out/(key+'.spv')).read_bytes();legacy,proof=specialization.transform(base,{1:3},minimum_weight=0);assert legacy is not None,proof
 # Candidate deliberately keeps identical owned code: GPU pixels validate that
 # the new module still receives the real per-draw Spec0 (not its default).
 (out/(key+'.spec')).write_bytes(envelope(base,legacy,legacy[60:],2,13))
 meta=bytearray((out/(key+'.meta')).read_bytes());count=struct.unpack_from('<I',meta,12)[0]
 struct.pack_into('<I',meta,12,count+1);meta.extend(struct.pack('<8i',1,2,13,-1,0,1,0,0));(out/(key+'.meta')).write_bytes(meta)
 key='000000000000000b';base=(out/(key+'.spv')).read_bytes();legacy=(out/(key+'.spec')).read_bytes();wrapped=envelope(base,legacy,legacy[60:],2,5);(out/(key+'.spec')).write_bytes(wrapped)
 # Reuse the existing actual DPL wire; append the exact legacy-kind2 variant
 # as independent kind6, with the new complete envelope hash in its closure.
 pl=bytearray((out/(key+'.plocal')).read_bytes());struct.pack_into('<I',pl,20,struct.unpack_from('<I',pl,20)[0]+1);pl[96:128]=sha(wrapped)
 n=192;record=None
 while n<len(pl):
  kind,length=struct.unpack_from('<2I',pl,n)
  if kind==2:record=bytearray(pl[n:n+length])
  n+=length
 assert record is not None;struct.pack_into('<I',record,0,6);pl.extend(record);struct.pack_into('<I',pl,8,len(pl));(out/(key+'.plocal')).write_bytes(pl)
 ts=bytearray((out/(key+'.tssbo')).read_bytes());ts[56:88]=sha(wrapped);(out/(key+'.tssbo')).write_bytes(ts)
 pl[160:192]=sha(ts);(out/(key+'.plocal')).write_bytes(pl)
 (out/'range-fixture.json').write_text(json.dumps({'owned_only':True,'candidate_same_SPIRV_for_Module_Spec0_contract':True,'ordinary_source':str(ordinary),'pixel_source':str(pixel),'keys':[hashlib.sha256(bytes([40])*0x880).hexdigest()[:16],key]},indent=2)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--ordinary',type=Path,required=True);p.add_argument('--pixel',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.ordinary,a.pixel,a.output)
