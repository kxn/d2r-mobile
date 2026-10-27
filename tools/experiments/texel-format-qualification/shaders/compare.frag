#version 450
layout(constant_id=0) const uint CONVERSION=1; // 0 raw negative reference, 1 hypothesis only
layout(constant_id=1) const uint INJECT_ERROR=0;
layout(set=0,binding=0,std430) readonly buffer Input { uvec4 values[]; };
layout(set=0,binding=1) uniform samplerBuffer typed_input;
layout(set=0,binding=3,std430) buffer Results { uint stats[]; };
layout(push_constant) uniform Params { uint base; uint count; uint width; uint unused; } p;
layout(location=0) out uint result_mask;
uint mask4(bvec4 b) {return uint(b.x)|(uint(b.y)<<1)|(uint(b.z)<<2)|(uint(b.w)<<3);}
void main() {
  uint i=uint(gl_FragCoord.y)*p.width+uint(gl_FragCoord.x);
  uvec4 raw=values[i];
  uvec4 typed=floatBitsToUint(texelFetch(typed_input,int(i)));
  uvec4 predicted=raw;
  if(CONVERSION==1) {
    bvec4 nan=equal(raw&uvec4(0x7f800000u),uvec4(0x7f800000u));
    nan=bvec4(nan.x&&((raw.x&0x007fffffu)!=0),nan.y&&((raw.y&0x007fffffu)!=0),
              nan.z&&((raw.z&0x007fffffu)!=0),nan.w&&((raw.w&0x007fffffu)!=0));
    predicted=raw|mix(uvec4(0),uvec4(0x00400000u),nan);
  }
  if(INJECT_ERROR!=0)predicted.x^=1u;
  uint x=p.base+i;
  uvec4 expected=uvec4(x,x^0x80000000u,x^0x7f800000u,((x<<13)|(x>>19))^0xdeadbeefu);
  uint mask=mask4(notEqual(typed,predicted))|(mask4(notEqual(raw,expected))<<4);
  result_mask=0x10000000u|mask;
  if(mask!=0 && atomicCompSwap(stats[5],0xffffffffu,i)==0xffffffffu) {
    stats[6]=mask;
    for(uint c=0;c<4;c++){stats[7+c]=raw[c];stats[11+c]=typed[c];stats[15+c]=predicted[c];}
  }
}
