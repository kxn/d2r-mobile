#!/usr/bin/env python3
# 自有 GLSL 测试素材：验证 NVN 计算与绘制，不含游戏着色器或内容。
# Original GLSL fixtures verify NVN compute and drawing without game shaders or content.
import hashlib
import ast
import re
from pathlib import Path
import struct
import subprocess
import sys
import os
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'gfx/tools'))
from dmsh_storage_bias import bias_storage_offsets

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
driver_alternate_proofs = {}
vertex = '''#version 450
#extension GL_ARB_shader_draw_parameters : require
void main() { vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(p[gl_VertexIndex-gl_BaseVertexARB],0,1); }
'''
fragment = '''#version 450
layout(set=1,binding=1,std140) uniform Color { vec4 color[4096]; };
layout(location=0) out vec4 value;
void main() { value=color[0]; }
'''
compute = '''#version 450
layout(local_size_x=64) in;
layout(set=2,binding=1,std430) readonly buffer Src { uint src[]; };
layout(set=2,binding=2,std430) writeonly buffer Dst { uint dst[]; };
void main() { uint i=gl_GlobalInvocationID.x; dst[i]=src[i]*3+7; }
'''
texel_compute = '''#version 450
layout(local_size_x=64) in;
layout(set=2,binding=1) uniform usamplerBuffer source;
layout(set=2,binding=2,std430) writeonly buffer Dst { uint dst[]; };
void main() { uint i=gl_GlobalInvocationID.x; dst[i]=texelFetch(source,int(i)).r*3+7; }
'''
sample_compute = '''#version 450
layout(local_size_x=64) in;
layout(set=2,binding=1) uniform texture2D source;
layout(set=2,binding=2) uniform sampler filtering;
layout(set=2,binding=3,std430) writeonly buffer Dst { uint dst[]; };
void main() { uint i=gl_GlobalInvocationID.x;
dst[i]=uint(round(texture(sampler2D(source,filtering),vec2(.5)).r*255.0)); }
'''
storage_fragment = '''#version 450
layout(set=1,binding=1,std430) readonly buffer Color { uint color[]; };
layout(location=0) out vec4 value;
void main() { value=uintBitsToFloat(uvec4(color[0],color[1],color[2],color[3])); }
'''
storage_texel_fragment = '''#version 450
layout(r32ui,set=1,binding=1) readonly uniform uimageBuffer colors;
layout(location=0) out vec4 value;
void main() { value=uintBitsToFloat(uvec4(imageLoad(colors,0).r,imageLoad(colors,1).r,
imageLoad(colors,2).r,imageLoad(colors,3).r)); }
'''
storage_texel_write_compute = '''#version 450
layout(local_size_x=1) in;
layout(r32ui,set=2,binding=1) writeonly uniform uimageBuffer output_image;
void main() { uint i=gl_GlobalInvocationID.x; imageStore(output_image,int(i),uvec4(256u+i*17u)); }
'''
storage_texel_read_compute = '''#version 450
layout(local_size_x=1) in;
layout(r32ui,set=2,binding=1) readonly uniform uimageBuffer input_image;
layout(set=2,binding=2,std430) writeonly buffer Output { uint output_words[]; };
void main() { uint i=gl_GlobalInvocationID.x;output_words[i]=imageLoad(input_image,int(i)).r*3u+7u; }
'''
storage_vertex = '''#version 450
layout(set=0,binding=1,std430) readonly buffer Positions { uint positions[]; };
void main() { uint i=uint(gl_VertexIndex)*2; gl_Position=vec4(uintBitsToFloat(positions[i]),uintBitsToFloat(positions[i+1]),0,1); }
'''
texel_vertex = '''#version 450
layout(set=0,binding=1) uniform usamplerBuffer positions;
void main() { int i=gl_VertexIndex*2;
gl_Position=vec4(uintBitsToFloat(texelFetch(positions,i).r),uintBitsToFloat(texelFetch(positions,i+1).r),0,1); }
'''
storage_texel_vertex = texel_vertex.replace('uniform usamplerBuffer positions',
    'layout(r32ui) readonly uniform uimageBuffer positions').replace('texelFetch(', 'imageLoad(')
storage_bounds = '''#version 450
layout(local_size_x=1) in;
layout(set=2,binding=1,std430) readonly buffer Src { uint src[]; };
layout(set=2,binding=2,std430) buffer Dst { uint dst[]; };
void main() { uint n=src.length(),m=dst.length();
dst[0]=n;dst[1]=m;dst[2]=src[0];dst[3]=src[n-1];dst[m-1]=0x91abcdefu; }
'''
storage_copy = '''#version 450
layout(local_size_x=1) in;
layout(set=2,binding=1,std430) readonly buffer Src { uint src[]; };
layout(set=2,binding=2,std430) writeonly buffer Dst { uint dst[]; };
void main() { for(uint i=0;i<min(src.length(),dst.length());i++) dst[i]=src[i]; }
'''
driver_fragment = '''#version 450
layout(set=1,binding=1,std140) uniform Addresses { uvec4 addresses[4096]; };
layout(set=1,binding=2,std140) uniform Handles { uvec4 handles[4096]; };
layout(location=0) out vec4 value;
void main() { value=vec4(addresses[81].z,addresses[82].z,handles[22].z,handles[86].z>>20)/255.0;
if(any(notEqual(addresses[4095],uvec4(0))) || any(notEqual(handles[4095],uvec4(0)))) value=vec4(1,0,1,1); }
'''
image_compute = '''#version 450
layout(local_size_x=4,local_size_y=4) in;
layout(rgba8,set=2,binding=1) writeonly uniform image2D target_image;
void main() { imageStore(target_image,ivec2(gl_GlobalInvocationID.xy),vec4(1,0,0.5,1)); }
'''
image_alias_fragment = '''#version 450
layout(set=1,binding=1) uniform texture2D source_image;
layout(set=1,binding=2) uniform sampler filtering;
layout(rgba8,set=1,binding=3) writeonly uniform image2D destination_image;
layout(location=0) out vec4 value;
void main() { vec4 c=texelFetch(sampler2D(source_image,filtering),ivec2(0),0);
imageStore(destination_image,ivec2(gl_FragCoord.xy)+ivec2(8,0),c.brga);value=c; }
'''
image_dependency_compute = '''#version 450
layout(local_size_x=1,local_size_y=1) in;
layout(rgba8,set=2,binding=1) readonly uniform image2D source_image;
layout(rgba8,set=2,binding=2) writeonly uniform image2D target_image;
void main() { ivec2 p=ivec2(gl_GlobalInvocationID.xy);
imageStore(target_image,p,imageLoad(source_image,p)+vec4(0,0.25,0,0)); }
'''
image_fragment = '''#version 450
layout(rgba8,set=1,binding=1) writeonly uniform image2D target_image;
layout(location=0) out vec4 value;
void main() { imageStore(target_image,ivec2(gl_FragCoord.xy),vec4(0,0.25,1,1)); value=vec4(1,0,0,1); }
'''
depth_fragment = '''#version 450
layout(set=1,binding=1) uniform texture2D depth_image;
layout(set=1,binding=2) uniform sampler filter_state;
layout(location=0) out vec4 value;
void main() { value=vec4(texelFetch(sampler2D(depth_image,filter_state),ivec2(gl_FragCoord.xy),0).r,0,0,1); }
'''
textured_vertex = """#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=0) out vec2 tc;
void main() { gl_Position=vec4(position,0,1); tc=uv; }
"""
textured_fragment = """#version 450
layout(set=1,binding=1) uniform texture2D image;
layout(set=1,binding=2) uniform sampler filter_state;
layout(location=0) in vec2 tc;
layout(location=0) out vec4 value;
void main() { value=texture(sampler2D(image,filter_state),tc); }
"""
instance_vertex = '''#version 450
#extension GL_ARB_shader_draw_parameters : require
layout(location=0) in vec4 instance_color;
layout(location=0) out vec4 shade;
void main() { vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
gl_Position=vec4(p[gl_VertexIndex-gl_BaseVertexARB],0,1); shade=instance_color;
shade.a=float(gl_InstanceIndex-gl_BaseInstanceARB+1)/8.0; }
'''
instance_fragment = '''#version 450
layout(location=0) in vec4 shade;
layout(location=0) out vec4 value;
void main() { value=shade; }
'''
ui_vertex = '''#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(set=0,binding=1,std140) uniform Projection { mat4 projection; };
layout(location=0) out vec2 tc;
void main() { vec4 p=projection*vec4(position,1); gl_Position=vec4(p.x,-p.y,p.z,p.w); tc=uv; }
'''
quad_vertex = '''#version 450
#extension GL_ARB_shader_draw_parameters : require
layout(location=0) in vec2 position;
layout(location=0) out vec4 shade;
void main() { gl_Position=vec4(position,0,1); shade=vec4(float(gl_BaseVertexARB)/16.0,0.5,1,1); }
'''
cover_vertex = '''#version 450
layout(location=0) in vec4 position;
void main() { float _x=position.x;float _y=position.y;float _z=position.z;
gl_Position.x=_x;gl_Position.y=_y;gl_Position.z=_z;gl_Position.w=1.0; }
'''
cover_w_vertex = cover_vertex.replace('gl_Position.w=1.0;', 'float _w=position.w;gl_Position.w=_w;')
discard_fragment = fragment.replace('value=color[0];', 'if(gl_FragCoord.x<8.0) discard; value=color[0];')
sample_mask_fragment = fragment.replace('value=color[0];', 'gl_SampleMask[0]=0;value=color[0];')
mrt_fragment = fragment.replace('void main()', 'layout(location=1) out vec4 second;\nvoid main()').replace('value=color[0];', 'value=color[0];second=vec4(.25,.5,.75,1);')
# 夹具静态事实也走原提取函数，不能手填游戏着色器白名单。
# Fixture facts use the original extractor too, without game shader whitelists.
original=Path(__file__).resolve().parents[3]/'gfx/tools/dmsh_build.py'
node=next(n for n in ast.parse(original.read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='raster_facts')
scope={'re':re};exec(compile(ast.Module(body=[node],type_ignores=[]),str(original),'exec'),scope)
float16_compute = '''#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
layout(local_size_x=64) in;
layout(set=2,binding=1,std430) readonly buffer Input { uint values[]; } src;
layout(set=2,binding=2,std430) buffer Output { uint values[]; } dst;
void main(){uint i=gl_GlobalInvocationID.x;
float16_t h=float16_t(float(src.values[i]&1023u)*0.25);
float16_t result=h*float16_t(2.0)+float16_t(0.5);
dst.values[i]=floatBitsToUint(float(result));}
'''
zero_fragment = '''#version 450
layout(set=1,binding=1) uniform sampler2D input_image;
layout(set=1,binding=2,std140) uniform Coefficient { vec4 values[4096]; };
layout(location=0) out vec4 value;
void main(){value=textureLod(input_image,gl_FragCoord.xy/16.0,values[0].x)*values[1].x;}
'''
nofxaa_vertex = '#version 450\nlayout(location=1) out vec4 source_uv;\nvoid main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));\nvec2 xy=p[gl_VertexIndex];gl_Position=vec4(xy,0,1);source_uv=vec4(xy.x*.5+.5,.5-xy.y*.5,0,0);}\n'
nofxaa_fragment = '#version 450\nlayout(set=1,binding=1) uniform sampler2D input_image;\nlayout(location=1) in vec4 source_uv;\nlayout(location=0) out vec4 color;\nvoid main(){color=textureLod(input_image,source_uv.xy,0.0)*vec4(.5,.5,.5,1);}\n'
bloom_vertex=cover_vertex.replace('void main()', 'layout(location=0) out vec2 source_uv;\nvoid main()').replace('gl_Position.w=1.0;', 'gl_Position.w=1.0;source_uv=vec2(position.x*.5+.5,.5-position.y*.5);')
bloom_sample='''#version 450
layout(set=1,binding=1) uniform sampler2D source_image;
layout(location=0) in vec2 source_uv;
layout(location=0) out vec4 color;
void main(){color=textureLod(source_image,source_uv,0.0);}
'''
bloom_upsample=bloom_sample.replace('color=textureLod(source_image,source_uv,0.0);', 'vec2 d=.5/vec2(textureSize(source_image,0));vec3 sum=vec3(0);for(int y=-1;y<=1;y+=2)for(int x=-1;x<=1;x+=2)sum+=textureLod(source_image,source_uv+vec2(x,y)*d,0.0).rgb;color=vec4(sum*.25,1);')
bloom_extract=bloom_sample.replace('void main()','''layout(set=1,binding=2,std140) uniform Coefficients { vec4 coeff[4096]; };
layout(set=1,binding=3,std140) uniform Threshold { vec4 threshold[4096]; };
void main()''').replace('color=textureLod(source_image,source_uv,0.0);', 'vec3 c=textureLod(source_image,source_uv,0.0).rgb;float l=c.r*.2126+c.g*coeff[0].x+c.b*coeff[0].y;color=vec4(l>=threshold[0].x?c/(1+l):vec3(0),1);')
uniform_selector_fragment = '#version 450\nlayout(set=1,binding=1,std140) uniform Selector { vec4 values[4096]; };\nlayout(location=0) out vec4 color;\nvoid main(){if(values[0].x<.5)color=vec4(values[0].x,1,0,1);else color=vec4(values[0].x,0,1,1);}\n'
wide_uniform_fragment = '''#version 450
layout(set=1,binding=2,std140) uniform First { vec4 values[4096]; };
layout(set=1,binding=1,std140) uniform Second { vec4 extra[4096]; };
layout(location=0) out vec4 color;
void main(){color=vec4(values[0].x,extra[0].x,1-values[0].x,1);}
'''
uniform_bits_fragment=uniform_selector_fragment.replace('if(values[0].x<.5)color=vec4(values[0].x,1,0,1);else color=vec4(values[0].x,0,1,1);','uint flag=floatBitsToUint(values[0].x);if(flag==1u)color=vec4(.25,1,0,1);else color=vec4(.75,0,1,1);')
# Read every byte of both 64KiB driver ranges on the real GPU, including
# fixed gaps/tail. A per-stage atomic counter preserves immutable A/B/A draws
# without changing the SSBO descriptor (which itself appears in c0).
driver_tables = '''#version 450
layout(set=SET,binding=1,std140) uniform Addresses { uvec4 addresses[4096]; };
layout(set=SET,binding=2,std140) uniform Handles { uvec4 handles[4096]; };
layout(set=SET,binding=3,std430) buffer Output { uint count;uint p0,p1,p2;uvec4 results[]; };
void snapshot(){uint record=atomicAdd(count,1u);if(record>=6u)return;
for(uint i=0u;i<4096u;i++){results[record*8192u+i]=addresses[i];results[record*8192u+4096u+i]=handles[i];}}
'''
driver_vertex = driver_tables.replace('SET','0') + '''
void main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexIndex],0,1);if(gl_VertexIndex==0)snapshot();}
'''
driver_full_fragment = driver_tables.replace('SET','1') + '''
layout(location=0) out vec4 color;
void main(){color=vec4(1);snapshot();}
'''
driver_compute = driver_tables.replace('SET','2') + '''
layout(local_size_x=1) in;
void main(){snapshot();}
'''
# Integer color attachments need neither vertex nor fragment storage atomics.
# One 1px point per uvec4 emits the VS table, while FS independently reads its
# own table at the same pixel. Both complete64KiB ranges fit128x64 pixels.
driver_point_vertex = '''#version 450
layout(set=0,binding=1,std140) uniform Addresses { uvec4 addresses[4096]; };
layout(set=0,binding=2,std140) uniform Handles { uvec4 handles[4096]; };
layout(location=0) flat out uvec4 vertex_table;
void main(){uint i=uint(gl_VertexIndex);vertex_table=i<4096u?addresses[i]:handles[i-4096u];
vec2 pixel=vec2(float(i%128u)+.5,float(i/128u)+.5);
gl_Position=vec4(pixel.x/64.0-1.0,1.0-pixel.y/32.0,0,1);gl_PointSize=1.0;}
'''
driver_point_fragment = '''#version 450
layout(set=1,binding=1,std140) uniform Addresses { uvec4 addresses[4096]; };
layout(set=1,binding=2,std140) uniform Handles { uvec4 handles[4096]; };
layout(location=0) flat in uvec4 vertex_table;
layout(location=0) out uvec4 vertex_value;
layout(location=1) out uvec4 fragment_value;
void main(){uint i=uint(gl_FragCoord.y)*128u+uint(gl_FragCoord.x);
vertex_value=vertex_table;fragment_value=i<4096u?addresses[i]:handles[i-4096u];}
'''
# Independent diagnostic: both Uniform accesses are always in bounds and
# precede resource selection. Keep the original 51/52 modules untouched.
driver_clamped_vertex = driver_point_vertex.replace(
    'vertex_table=i<4096u?addresses[i]:handles[i-4096u];',
    'uint j=i&4095u;uvec4 a=addresses[j];uvec4 b=handles[j];vertex_table=i<4096u?a:b;')
driver_clamped_fragment = driver_point_fragment.replace(
    'vertex_value=vertex_table;fragment_value=i<4096u?addresses[i]:handles[i-4096u];',
    'uint j=i&4095u;uvec4 a=addresses[j];uvec4 b=handles[j];vertex_value=vertex_table;fragment_value=i<4096u?a:b;')
# Own generic typed-buffer fixture; runtime must still prove the bound format.
texel_storage_fragment = '''#version 450
layout(set=1,binding=1) uniform samplerBuffer texels;
layout(set=1,binding=2,std140) uniform Selector { vec4 selector[4096]; };
layout(location=0) out vec4 value;
void main(){vec4 c=texelFetch(texels,int(gl_FragCoord.x)-1);
if(selector[0].x<.5)value=c;else value=vec4(c.b,c.g,c.r,c.a);}
'''
texel_storage_bits_fragment = '''#version 450
layout(set=1,binding=1) uniform samplerBuffer texels;
layout(location=0) out uvec4 bits;
void main(){bits=floatBitsToUint(texelFetch(texels,int(gl_FragCoord.x)));}
'''
# Generic two-word hybrid: old selector/loop plus extra flags with numeric uses.
uniform_predicate_fragment = '''#version 450
layout(set=1,binding=1,std140) uniform First { vec4 first[4096]; };
layout(set=1,binding=2,std140) uniform Second { vec4 second[4096]; };
layout(location=0) out uvec4 value;
void main(){uint a=floatBitsToUint(first[0].x);uint b=floatBitsToUint(second[20].x);
uint sum=0u;for(uint i=0u;i<min(a,4u);++i)sum+=i+1u;
if(a==1u)sum+=20u;else if(a==2u)sum+=30u;else sum+=40u;
if((b&16u)!=0u)sum+=100u;if((b&2u)==2u)sum+=200u;
value=uvec4(sum,a,floatBitsToUint(second[20].x*2.0),b);}
'''
driver_sparse_compute = '''#version 450
layout(local_size_x=1) in;
layout(set=2,binding=1,std140) uniform Addresses { uvec4 addresses[4096]; };
layout(set=2,binding=2,std140) uniform Handles { uvec4 handles[4096]; };
layout(set=2,binding=3,std430) writeonly buffer Output { uvec4 result; };
void main(){uint value;if((handles[22].z&1u)!=0u)value=handles[23].x;else value=handles[24].z;
result=uvec4(value,addresses[53].x^addresses[53].y^addresses[53].z,
addresses[36].x^addresses[36].y^addresses[36].z,handles[86].z);}
'''
driver_sparse_other = driver_sparse_compute.replace('handles[23].x','handles[27].z').replace('handles[24].z','handles[28].z').replace('addresses[53]','addresses[55]').replace('addresses[36]','addresses[38]').replace('handles[86].z','handles[90].x')
driver_sparse_dynamic = driver_sparse_compute.replace('uint value;if((handles[22].z&1u)!=0u)value=handles[23].x;else value=handles[24].z;', 'uint value=handles[handles[22].z&31u].x;')
driver_sparse_zero = '''#version 450
layout(local_size_x=1) in;
layout(set=2,binding=2,std140) uniform Handles { uvec4 handles[4096]; };
layout(set=2,binding=3,std430) writeonly buffer Output { uvec4 result; };
void main(){result=handles[4095];}
'''
generated_markers=set()
for marker, stage, suffix, code, bindings in (
    (59,5,'comp',driver_sparse_compute,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0],[2,3,15,0,1,1,0,0]]),
    (60,5,'comp',driver_sparse_other,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0],[2,3,15,0,1,1,0,0]]),
    (61,5,'comp',driver_sparse_dynamic,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0],[2,3,15,0,1,1,0,0]]),
    (62,5,'comp',driver_sparse_zero,[[1,2,2,0,0,1,0,0],[2,3,15,0,1,1,0,0]]),
    (63,5,'comp',driver_sparse_compute,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0],[2,3,15,0,1,1,0,0]]),
    (64,5,'comp',driver_sparse_compute,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0],[2,3,15,0,1,1,0,0]]),
    (57,0,'vert',driver_clamped_vertex,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0]]),
    (58,1,'frag',driver_clamped_fragment,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0]]),
    (56,1,'frag',uniform_predicate_fragment,[[1,1,4,0,0,1,0,0],[1,2,5,0,0,1,0,0]]),
    (54,1,'frag',texel_storage_bits_fragment,[[7,1,0,-1,0,1,2,2]]),
    (53,1,'frag',texel_storage_fragment,[[7,1,0,-1,0,1,2,2],[1,2,3,0,0,1,0,0]]),
    (51,0,'vert',driver_point_vertex,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0]]),
    (52,1,'frag',driver_point_fragment,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0]]),
    (48,0,'vert',driver_vertex,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0],[2,3,15,0,1,1,0,0]]),
    (49,1,'frag',driver_full_fragment,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0],[2,3,15,0,1,1,0,0]]),
    (50,5,'comp',driver_compute,[[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0],[2,3,15,0,1,1,0,0]]),
    (47,1,'frag',wide_uniform_fragment,[[1,2,3,0,0,1,0,0],[1,1,8,0,0,1,0,0]]),
    (41,0,'vert',bloom_vertex,[]),
    (46,1,'frag',bloom_extract,[[3,1,0,0,0,1,2,3],[1,2,1,0,0,1,0,0],[1,3,7,0,0,1,0,0]]),
    (42,1,'frag',bloom_extract,[[3,1,0,0,0,1,2,3],[1,2,1,0,0,1,0,0],[1,3,7,0,0,1,0,0]]),
    (43,1,'frag',bloom_sample,[[3,1,0,0,0,1,2,3]]),
    (44,1,'frag',bloom_upsample,[[3,1,0,0,0,1,2,3]]),
    (45,1,'frag',bloom_sample,[[3,1,0,0,0,1,2,3]]),
    (40,1,'frag',uniform_bits_fragment,[[1,1,3,0,0,1,0,0]]),
    (39,1,'frag',uniform_selector_fragment,[[1,1,3,0,0,1,0,0]]),
    (37,0,'vert',nofxaa_vertex,[]),
    (38,1,'frag',nofxaa_fragment,[[3,1,0,0,0,1,2,3]]),
    (36,1,'frag',zero_fragment,[[3,1,0,0,0,1,2,3],[1,2,7,0,0,1,0,0]]),
    (65,0,'vert','''#version 450
layout(location=0)in vec2 p;layout(location=1)in vec4 a;layout(location=2)in vec4 b;layout(location=3)in vec4 c;
layout(location=0)out vec4 color;
void main(){gl_Position=vec4(p,0,1);color=a+b+c;}
''',[]),
    (66,1,'frag','''#version 450
layout(location=0)in vec4 color;layout(location=0)out vec4 value;
void main(){value=color;}
''',[]),
    (35,5,'comp',float16_compute,[[2,1,0,0,0,1,0,0],[2,2,1,0,1,1,0,0]]),
    (0, 0, 'vert', vertex, []),
    (13, 0, 'vert', vertex, []),
    (14, 0, 'vert', quad_vertex, []),
    (15, 0, 'vert', cover_vertex, []),
    (16, 0, 'vert', cover_w_vertex, []),
    (17, 1, 'frag', discard_fragment, [[1,1,3,0,0,1,0,0]]),
    (18, 1, 'frag', sample_mask_fragment, [[1,1,3,0,0,1,0,0]]),
    (19, 1, 'frag', fragment, [[1,1,3,0,0,1,0,0]]),
    (20, 1, 'frag', fragment, [[1,1,3,0,0,1,0,0]]),
    (21, 1, 'frag', mrt_fragment, [[1,1,3,0,0,1,0,0]]),
    (3, 0, 'vert', textured_vertex, []),
    (4, 1, 'frag', textured_fragment, [[6,1,0,-1,0,1,2,3],[5,2,0,-1,0,1,2,0]]),
    (1, 1, 'frag', fragment, [[1, 1, 3, 0, 0, 1, 0, 0]]),
    (5, 1, 'frag', storage_fragment, [[2, 1, 0, 0, 0, 1, 0, 0]]),
    (6, 1, 'frag', driver_fragment, [[1,1,0,0,0,1,0,0],[1,2,2,0,0,1,0,0]]),
    (7, 5, 'comp', image_compute, [[4,1,0,-1,4,1,2,3]]),
    (34, 1, 'frag', image_alias_fragment, [[6,1,0,-1,0,1,2,3],[5,2,0,-1,0,1,2,0],[4,3,0,-1,4,1,2,3]]),
    (30, 5, 'comp', image_dependency_compute, [[4,1,0,-1,0,1,2,3],[4,2,1,-1,4,1,2,3]]),
    (31, 1, 'frag', storage_texel_fragment, [[9,1,0,-1,3,1,2,3]]),
    (32, 5, 'comp', storage_texel_write_compute, [[9,1,0,-1,4,1,2,3]]),
    (33, 5, 'comp', storage_texel_read_compute, [[9,1,0,-1,3,1,2,3],[2,2,0,0,1,1,0,0]]),
    (8, 1, 'frag', image_fragment, [[4,1,0,-1,4,1,2,3]]),
    (9, 1, 'frag', depth_fragment, [[6,1,0,-1,0,1,2,3],[5,2,0,-1,0,1,2,0]]),
    (2, 5, 'comp', compute, [[2, 1, 0, 0, 0, 1, 0, 0], [2, 2, 1, 0, 1, 1, 0, 0]]),
    (22, 5, 'comp', texel_compute, [[7,1,0,-1,0,1,2,3],[2,2,0,0,1,1,0,0]]),
    (81, 5, 'comp', sample_compute.replace('local_size_x=64','local_size_x=1').replace('uniform sampler filtering','uniform samplerShadow filtering').replace('sampler2D(source,filtering),vec2(.5)', 'sampler2DShadow(source,filtering),vec3(.5)').replace('.r*255.0', '*255.0'), [[6,1,0,-1,0,1,2,1027],[5,2,0,-1,0,1,2,0],[2,3,0,0,1,1,0,0]]),
    (82, 5, 'comp', sample_compute.replace('texture2D source','textureCube source').replace('local_size_x=64','local_size_x=1').replace('uniform sampler filtering','uniform samplerShadow filtering').replace('sampler2D(source,filtering),vec2(.5)', 'samplerCubeShadow(source,filtering),vec4(.5,.5,1.,.5)').replace('.r*255.0', '*255.0'), [[6,1,0,-1,0,1,2,1029],[5,2,0,-1,0,1,2,0],[2,3,0,0,1,1,0,0]]),
    (23, 5, 'comp', sample_compute, [[6,1,0,-1,0,1,2,3],[5,2,0,-1,0,1,2,0],[2,3,0,0,1,1,0,0]]),
    (24, 0, 'vert', storage_vertex, [[2,1,0,0,0,1,0,0]]),
    (25, 5, 'comp', storage_bounds, [[2,1,0,0,0,1,0,0],[2,2,1,0,1,1,0,0]]),
    (26, 5, 'comp', storage_copy, [[2,1,0,0,0,1,0,0],[2,2,1,0,1,1,0,0]]),
    (27, 5, 'comp', storage_bounds, [[8,1,0,0,0,1,0,0],[2,2,1,0,1,1,0,0]]),
    (28, 0, 'vert', texel_vertex, [[7,1,0,-1,0,1,2,3]]),
    (29, 0, 'vert', storage_texel_vertex, [[9,1,0,-1,0,1,2,3]]),
    (10, 0, 'vert', instance_vertex, []),
    (11, 1, 'frag', instance_fragment, []),
    (12, 0, 'vert', ui_vertex, [[1,1,3,0,0,1,0,0]]),
):
    if marker in generated_markers:raise ValueError(f"Duplicate owned shader marker: {marker}")
    generated_markers.add(marker)
    key = hashlib.sha256(bytes([marker])*0x880).hexdigest()[:16]
    source = root / (key + '.' + suffix)
    source.write_text(code)
    subprocess.run(['glslangValidator', '-V', '--target-env', 'vulkan1.1', str(source), '-o', str(root / (key + '.spv'))], check=True, capture_output=True)
    bias=0
    if os.environ.get('DMSH_VK_STORAGE_BIAS')=='1':
        binary=root/(key+'.spv')
        transformed,bias,reason=bias_storage_offsets(binary.read_bytes(),
            {b[1]:b[2]+(16 if b[0]==8 else 0) for b in bindings if b[0] in (2,8)},stage)
        binary.write_bytes(transformed)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(root / (key + '.spv'))], check=True)
    local = (4,4,1) if marker == 7 else ((64,1,1) if stage == 5 and (marker<25 or marker==35) else (1,1,1))
    header = [0x314b5644, stage, 2 if stage == 5 else stage, len(bindings), 0, *local]
    if marker>=15 and marker!=20:
        msl=subprocess.check_output(['spirv-cross','--msl','--msl-version','20300','--msl-argument-buffers','--msl-texture-buffer-native','--msl-pad-fragment-output',str(root/(key+'.spv'))],text=True)
        facts=scope['raster_facts']('vs' if stage==0 else 'ps',msl)
        flags=int(facts.get('pos_w')=='one')|(int(facts.get('discard',1))<<1)|(int(facts.get('sample_mask',1))<<2)
        header[0]=0x324b5644;header += [facts.get('pos_attr',-1)&0xffffffff,flags,sum(1<<i for i in facts.get('colors',[])),1]
    if bias:
        if len(header)==8:header += [0xffffffff,6,0,1]
        header[0]=0x334b5644;header += [bias,64 if stage==1 else 0,32 if stage==5 else 16,1]
    if marker in (39,40,53,56):
        import dmsh_uniform_specialization as policy
        policy.SPEC_ID=int(os.environ.get('DMSH_FIXTURE_SPEC_ID','0'))
        recipe,proof=policy.transform((root/(key+'.spv')).read_bytes(),({1:4} if marker==56 else {2 if marker==53 else 1:3}),minimum_weight=0)
        assert recipe is not None,proof
        (root/(key+'.spec')).write_bytes(recipe)
    if marker==56:
        from dmsh_uniform_predicates import transform as predicate_transform,unpack as unpack_predicates
        base=(root/(key+'.spv')).read_bytes();recipe=(root/(key+'.spec')).read_bytes()
        payload,proof=predicate_transform(base,recipe,{1:4,2:5},minimum_weight=0)
        assert payload is not None,proof
        unpack_predicates(payload,base,recipe,proof)
        (root/(key+'.pred')).write_bytes(payload)
    if marker in (53,54):
        from dmsh_texel_ssbo import pair,unpack_pair
        payload,proof=pair((root/(key+'.spv')).read_bytes(),(root/(key+'.spec')).read_bytes()if marker==53 else None,{1},1,
                           os.environ.get('DMSH_VK_TEXEL_LOAD_MODEL','identity'))
        assert payload is not None,proof
        unpack_pair(payload,(root/(key+'.spv')).read_bytes(),(root/(key+'.spec')).read_bytes()if marker==53 else None,proof)
        driver_alternate_proofs[key]={'texel_storage_alternative':proof}
        (root/(key+'.tssbo')).write_bytes(payload)
    c1=struct.pack('<2f',float('nan') if marker==46 else .7152,.0722) if marker in (42,46) else b''
    header[4]=len(c1)
    (root / (key + '.meta')).write_bytes(struct.pack('<%dI'%len(header), *header) + b''.join(struct.pack('<8i', *b) for b in bindings)+c1)
from dmsh_driver_keys import emit as emit_driver_keys
for path in sorted(root.glob('*.meta')):
    emit_driver_keys(root,path.stem,driver_alternate_proofs.get(path.stem,{}))
# Deliberately absent/damaged OPTIONAL proof files for owned programs. Their
# shaders and metadata stay valid; runtime must retain the full-key behavior.
missing=hashlib.sha256(bytes([63])*0x880).hexdigest()[:16]
(root/(missing+'.dkey')).unlink()
damaged=hashlib.sha256(bytes([64])*0x880).hexdigest()[:16]
path=root/(damaged+'.dkey');data=bytearray(path.read_bytes());data[-1]^=1;path.write_bytes(data)
print('Vulkan fixture shaders:', root)
