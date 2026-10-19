// 不支持逐片元模板导出时，用洞纹理筛掉非洞片元，由固定模板状态写 0x80。
// Without stencil export, discard non-hole fragments; fixed stencil writes 0x80.
#version 450
layout(set=0,binding=0) uniform usampler2D holes;
void main() { if (texelFetch(holes,ivec2(gl_FragCoord.xy),0).r==0) discard; }
