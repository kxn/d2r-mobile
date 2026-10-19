#!/usr/bin/env python3
"""Owned shader artifacts for real two-subpass NVN runtime GPU tests."""
import hashlib,struct,subprocess,sys
from pathlib import Path
# Wire builder is the separately reviewed tool, not a second schema encoder.
sys.path.insert(0,sys.argv[2])
import dmsh_pixel_local_wire as wire
root=Path(sys.argv[1]);root.mkdir(parents=True,exist_ok=True)
items=[(180,0,'''#version 450
layout(location=0)in vec3 p;layout(location=1)in vec2 t;layout(location=1)out vec2 uv;
void main(){gl_Position=vec4(p,1);uv=t;}
''',[],0,0),
(181,1,'''#version 450
layout(location=0)out vec4 a;layout(location=1)out vec4 b;layout(location=2)out vec4 c;
void main(){a=vec4(64./255.,0,0,1);b=vec4(0,64./255.,0,1);c=vec4(0,0,64./255.,1);}
''',[],-1,7),
(182,1,'''#version 450
layout(location=1)in vec2 uv;layout(location=0)out vec4 color;
layout(set=1,binding=1)uniform sampler2D a;layout(set=1,binding=2)uniform sampler2D b;
layout(set=1,binding=3)uniform sampler2D c;layout(set=1,binding=4)uniform sampler2D h;
void main(){color=textureLod(a,uv,0)+textureLod(b,uv,0)+textureLod(c,uv,0)+textureLod(h,uv,0);}
''',[(3,k,k-1,0,0,1,0,2)for k in range(1,5)],-1,1)]
items.append((183,1,'''#version 450
layout(location=1)in vec2 uv;layout(location=0)out vec4 color;layout(set=1,binding=1)uniform sampler2D a;
void main(){color=texture(a,uv);}
''',[(3,1,0,0,0,1,0,2)],-1,1))
items.append((184,1,items[2][2].replace('void main()', 'layout(set=1,binding=5)uniform sampler2D neighbor;\nvoid main()').replace('textureLod(h,uv,0);','textureLod(h,uv,0)+textureLod(neighbor,uv+vec2(.01,0),0);'),items[2][3]+[(3,5,4,0,0,1,0,2)],-1,1))
items.append((185,1,items[1][2].replace('64./255.,0,0,1','64./255.,0,0,0'),[],-1,7))
items.append((186,0,items[0][2].replace('void main()', 'layout(set=0,binding=1)uniform sampler2D source;layout(location=2)out vec4 extra;\nvoid main()').replace('uv=t;','uv=t;extra=textureLod(source,vec2(.5),0);'),[(3,1,0,0,0,1,0,2)],0,0))
items.append((187,1,items[2][2].replace('void main()', 'layout(location=2)in vec4 extra;\nvoid main()').replace('textureLod(h,uv,0);','textureLod(h,uv,0)+extra;'),items[2][3],-1,1))
# Conservative metadata resource: legal extra VS sampled schema remains subject
# to runtime alias checks even when this module does not use the resource.
items.append((188,0,items[0][2].replace('void main()', 'layout(set=0,binding=1)uniform sampler2D source;\nvoid main()'),[(3,1,0,0,0,1,0,2)],0,0))
for marker,stage,text,bindings,pos,colors in items:
 key=hashlib.sha256(bytes([marker])*0x880).hexdigest()[:16]
 source=root/(key+('.vert'if stage==0 else'.frag'));source.write_text(text)
 path=root/(key+'.spv');subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(source),'-o',str(path)],check=True)
 blobs={'spv':path.read_bytes(),'meta':struct.pack('<12I',0x324b5644,stage,stage,len(bindings),0,1,1,1,pos&0xffffffff,int(pos>=0),colors,1)+b''.join(struct.pack('<8i',*b)for b in bindings)}
 for suffix,data in blobs.items():(root/(key+'.'+suffix)).write_bytes(data)
 payload,proof=wire.build(blobs,{'descriptor_set':stage})
 (root/(key+'.plocal')).write_bytes(payload)
 print(marker,key,proof['records'])
