#!/usr/bin/env python3
"""Owned GLSL fixtures for the real optional Vulkan shader/descriptor loader."""
from pathlib import Path
import argparse
import json
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import dmsh_pixel_local_wire as wire


def build(root):
    root.mkdir(parents=True,exist_ok=True)
    def compile(text,stage):
        source=root/('owned.'+stage);source.write_text(text);out=root/'owned.spv'
        subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(source),'-o',str(out)],check=True,capture_output=True)
        code=out.read_bytes();source.unlink();out.unlink();return code
    def meta(stage,bindings,position=-1):
        return struct.pack('<12I',0x324b5644,stage,stage,len(bindings),0,1,1,1,position&0xffffffff,int(position>=0),1,1)+b''.join(struct.pack('<8i',*b) for b in bindings)
    def publish(number,blobs,reflection=None,corrupt=None):
        key=f'{number:016x}';payload,proof=wire.build(blobs,{'descriptor_set':struct.unpack_from('<I',blobs['meta'],8)[0],**(reflection or {})})
        if corrupt:payload=corrupt(bytearray(payload))
        for suffix,data in blobs.items():(root/(key+'.'+suffix)).write_bytes(data)
        (root/(key+'.plocal')).write_bytes(payload)
        return proof
    fragment=compile('''#version 450
layout(set=1,binding=1)uniform sampler2D a;layout(set=1,binding=2)uniform sampler2D b;
layout(set=1,binding=3)uniform sampler2D c;layout(set=1,binding=4)uniform sampler2D history;
layout(location=1)in vec2 uv;layout(location=0)out vec4 color;
void main(){color=texture(a,uv)+texture(b,uv)+texture(c,uv)+texture(history,uv);}''','frag')
    bindings=[(3,i,i-1,-1,0,1,0,2) for i in range(1,5)]
    blobs={'spv':fragment,'meta':meta(1,bindings)};publish(1,blobs)
    vertex=compile('''#version 450
layout(location=0)in vec3 pos;layout(location=3)in vec2 tex;
layout(set=0,binding=1,std140)uniform Coeff{vec4 v[4];}coeff;
layout(location=1)out vec2 uv;
void main(){gl_Position=vec4(pos,1.0);uv=fma(tex,coeff.v[3].xy,coeff.v[2].zw);}''','vert')
    publish(2,{'spv':vertex,'meta':meta(0,[(1,1,3,-1,0,1,0,0)],0)})
    def changed(offset,value):
        def corrupt(data):struct.pack_into('<I',data,offset,value);return bytes(data)
        return corrupt
    controls={3:lambda b:bytes(b[:-4]),4:changed(192+96+4,127),5:changed(20,6),
              6:changed(192,6),7:changed(192+8,9),8:changed(192+32,0),
              9:lambda b:bytes(b[:-1])+bytes([b[-1]^128]),10:changed(32,0)}
    for number,corrupt in controls.items():publish(number,blobs,corrupt=corrupt)
    # All five actual source modules are independently transformed and loaded.
    source=compile('''#version 450
layout(set=1,binding=1,std140)uniform A{vec4 first[4096];};
layout(set=1,binding=2,std140)uniform B{vec4 second[4096];};
layout(set=1,binding=3)uniform sampler2D image;
layout(set=1,binding=4)uniform samplerBuffer bufferImage;
layout(location=1)in vec2 uv;layout(location=0)out vec4 color;
void main(){uint a=floatBitsToUint(first[0].x),b=floatBitsToUint(second[20].x);
uint s=0;for(uint i=0;i<min(a,4u);i++)s+=i+1u;
if(a==1u)s+=20u;else s+=40u;if((b&16u)!=0u)s+=100u;if((b&2u)==2u)s+=200u;
color=texture(image,uv)+texelFetch(bufferImage,int(a&3u))+vec4(float(s),first[0].x,second[20].x,b);}
''','frag')
    from dmsh_uniform_specialization import transform
    from dmsh_uniform_predicates import transform as predicates
    from dmsh_texel_ssbo import pair
    spec,sp=transform(source,{1:4},minimum_weight=0)
    if spec is None:raise ValueError(sp)
    pred,pp=predicates(source,spec,{1:4,2:5},minimum_weight=0)
    if pred is None:raise ValueError(pp)
    alt,tp=pair(source,spec,{4},1)
    if alt is None:raise ValueError(tp)
    bindings=[(1,1,4,-1,0,1,0,0),(1,2,5,-1,0,1,0,0),(3,3,0,-1,0,1,0,2),(7,4,1,-1,0,1,0,2)]
    proof=publish(11,{'spv':source,'meta':meta(1,bindings),'spec':spec,'pred':pred,'tssbo':alt},{'texel_storage_alternative':tp})
    if proof['records']!=5:raise ValueError('all five modules must have their own positive proof')
    (root/'fixture.json').write_text(json.dumps({'records':proof['records'],'negative_keys':list(controls)},indent=2)+'\n')
    return root


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);args=p.parse_args();print(build(args.output))
