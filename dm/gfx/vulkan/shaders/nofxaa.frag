#version 450
// Upstream DM_NVN_NOFXAA: sample the original input without edge filtering.
// Keep its original vertex UV, descriptor/sampler, output and render target.
layout(set=1,binding=1) uniform sampler2D source_image;
layout(location=1) in vec4 source_uv;
layout(location=0) out vec4 color;
void main() {
  vec2 uv=mix(source_uv.xy,vec2(0),isnan(source_uv.xy));
  color=textureLod(source_image,uv,0.0);
}
