// 对应 nvn_rtex.inc 的 rx_fs1：反投影、Catmull-Rom、补洞、深度与模板导出。
// Original rx_fs1: backward projection, Catmull-Rom, hole fill, depth/stencil export.
#version 450
#ifndef DM_RX_MASK
#extension GL_ARB_shader_stencil_export : require
#endif
layout(set=0,binding=0) uniform sampler2D hdr;
layout(set=0,binding=1) uniform sampler2D dep;
layout(set=0,binding=2) uniform usampler2D stc;
layout(push_constant) uniform K { mat4 M; uint hole; float amount; vec2 pad; } k;
layout(location=0) out vec4 color;
#ifdef DM_RX_MASK
layout(location=1) out uint hole_mask;
#endif
uint stencil_at(ivec2 p) { return texelFetch(stc,p,0).r; }
float zproj(vec2 p,float z,vec2 extent) {
  vec4 c=k.M*vec4((p.x+.5)/extent.x*2-1,1-(p.y+.5)/extent.y*2,z,1);
  return c.z/c.w;
}
vec3 sample_hdr(vec2 p,vec2 extent) { return textureLod(hdr,p/extent,0).rgb; }
void main() {
  ivec2 hi=textureSize(hdr,0)-1; vec2 extent=vec2(hi+1);
  vec2 q=floor(gl_FragCoord.xy),p=q;
  for (int it=0;it<2;it++) {
    ivec2 pi=clamp(ivec2(roundEven(p)),ivec2(0),hi);
    float z=texelFetch(dep,pi,0).r;
    vec4 c=k.M*vec4((p.x+.5)/extent.x*2-1,1-(p.y+.5)/extent.y*2,z,1);
    vec2 qq=vec2((c.x/c.w+1)*.5*extent.x-.5,(1-c.y/c.w)*.5*extent.y-.5);
    p+=q-qq;
  }
  bool oob=any(lessThan(p,vec2(-.5)))||any(greaterThan(p,vec2(hi)+.5));
  p=clamp(p,vec2(0),vec2(hi));
  ivec2 b0=clamp(ivec2(floor(p)),ivec2(0),hi-1);
  uint s4=stencil_at(b0)|stencil_at(b0+ivec2(1,0))|
          stencil_at(b0+ivec2(0,1))|stencil_at(b0+ivec2(1,1));
  ivec2 cp=clamp(ivec2(roundEven(p)),ivec2(0),hi);
  bool hole=(s4&k.hole)!=0||oob;
#ifdef DM_RX_MASK
  hole_mask=hole?1u:0u;
#else
  gl_FragStencilRefARB=hole?0x80:0;
#endif
  if ((s4&k.hole)==0) {
    vec3 t00=texelFetch(hdr,b0,0).rgb,t10=texelFetch(hdr,b0+ivec2(1,0),0).rgb;
    vec3 t01=texelFetch(hdr,b0+ivec2(0,1),0).rgb,t11=texelFetch(hdr,b0+ivec2(1,1),0).rgb;
    vec3 lo=min(min(t00,t10),min(t01,t11)),hc=max(max(t00,t10),max(t01,t11));
    vec3 avg=(t00+t10+t01+t11)*.25;
    vec2 sp=p+.5,tp1=floor(sp-.5)+.5,f=sp-tp1;
    vec2 w0=f*(-.5+f*(1-.5*f)),w1=1+f*f*(-2.5+1.5*f);
    vec2 w2=f*(.5+f*(2-1.5*f)),w3=f*f*(-.5+.5*f);
    vec2 w12=w1+w2,t12=tp1+w2/w12,t0=tp1-1,t3=tp1+2;
    vec3 acc=sample_hdr(vec2(t12.x,t0.y),extent)*(w12.x*w0.y)+
             sample_hdr(vec2(t0.x,t12.y),extent)*(w0.x*w12.y)+
             sample_hdr(t12,extent)*(w12.x*w12.y)+
             sample_hdr(vec2(t3.x,t12.y),extent)*(w3.x*w12.y)+
             sample_hdr(vec2(t12.x,t3.y),extent)*(w12.x*w3.y);
    float wt=w12.x*w0.y+w0.x*w12.y+w12.x*w12.y+w3.x*w12.y+w12.x*w3.y;
    vec3 cr=clamp(acc/wt,lo,hc);
    color=vec4(max(cr+k.amount*(cr-avg),vec3(0)),1);
    gl_FragDepth=zproj(p,texelFetch(dep,cp,0).r,extent); return;
  }
  const ivec2 dirs[8]=ivec2[8](ivec2(1,0),ivec2(-1,0),ivec2(0,1),ivec2(0,-1),
      ivec2(1,1),ivec2(-1,1),ivec2(1,-1),ivec2(-1,-1));
  vec3 sum=vec3(0);float wsum=0;int bestr=1000;ivec2 bests=cp;
  for (int d=0;d<8;d++) for (int r=1;r<=24;r++) {
    ivec2 s=cp+dirs[d]*(4*r);
    if (any(lessThan(s,ivec2(0)))||any(greaterThan(s,hi))) break;
    if ((stencil_at(s)&k.hole)!=0) continue;
    vec3 v=texelFetch(hdr,s,0).rgb;if (max(v.r,max(v.g,v.b))<=0) continue;
    float w=1.0/float(r*r);sum+=v*w;wsum+=w;
    if (r<bestr) { bestr=r;bests=s; } break;
  }
  if (wsum>0) {
    color=vec4(sum/wsum,1);
    gl_FragDepth=zproj(vec2(bests),texelFetch(dep,bests,0).r,extent);return;
  }
  color=vec4(texelFetch(hdr,cp,0).rgb,1);
  gl_FragDepth=zproj(p,texelFetch(dep,cp,0).r,extent);
}
