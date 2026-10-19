"""Flag existing owned raster modules for runtime V2 admission, not math proof."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-fixture',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(exist_ok=False,parents=True)
inputs={};outputs={}
def emit(key,raw,spv):
 for suffix,data in [('meta',raw),('spv',spv)]:
  target=a.output/(key+'.'+suffix);target.write_bytes(data);outputs[target.name]=hashlib.sha256(data).hexdigest()
for marker in [0,1,41,43]:
 key=hashlib.sha256(bytes([marker])*0x880).hexdigest()[:16];raw=(a.source_fixture/(key+'.meta')).read_bytes();spv=(a.source_fixture/(key+'.spv')).read_bytes();inputs[key+'.meta']=hashlib.sha256(raw).hexdigest();inputs[key+'.spv']=hashlib.sha256(spv).hexdigest();magic=struct.unpack_from('<I',raw)[0]
 if marker in [1,43]:
  if magic==0x314b5644:
   h=list(struct.unpack('<8I',raw[:32]));h[0]=0x324b5644;raw=struct.pack('<12I',*h,0xffffffff,24,15,1)+raw[32:]
  else:
   assert magic in [0x324b5644,0x334b5644];raw=bytearray(raw);flags=struct.unpack_from('<I',raw,36)[0];struct.pack_into('<I',raw,36,(flags&7)|24);raw=bytes(raw)
 if marker==43:
  # Owned functional depth-fetch contract probe, distinct from actual compiler oracle.
  with tempfile.TemporaryDirectory() as td:
   src=Path(td)/'fetch.frag';out=Path(td)/'fetch.spv'
   src.write_text('#version 450\nlayout(set=1,binding=1) uniform sampler2D source_image;\nlayout(location=0) out vec4 value;\nvoid main(){ ivec2 sz=textureSize(source_image,0); ivec2 p=clamp(ivec2(gl_FragCoord.xy),ivec2(0),sz-ivec2(1)); value=texelFetch(source_image,p,0); }\n')
   subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(src),'-o',str(out)],check=True,capture_output=True)
   subprocess.run(['spirv-val','--target-env','vulkan1.1',str(out)],check=True,capture_output=True);spv=out.read_bytes()
 emit(key,raw,spv)
 if marker==1:
  for bad,flags in [('000000000000f002',16),('000000000000f003',0x80000000)]:
   b=bytearray(raw);struct.pack_into('<I',b,36,flags);emit(bad,b,spv)
(a.output/'manifest.json').write_text(json.dumps({'scope':__doc__,'source':str(a.source_fixture),'source_inputs':inputs,'files':outputs,'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},indent=2)+'\n')
