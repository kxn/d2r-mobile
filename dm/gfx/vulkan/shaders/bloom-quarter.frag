// Original nvn_mtl.m bloomq_make_pipeline algorithm and coefficient order.
// Four linear clamp samples use the actual full-resolution source dimensions.
#version 450
layout(set=0,binding=0) uniform sampler2D source_image;
layout(push_constant) uniform Parameters {
  float threshold; float coeff_g; float coeff_b; float source_width; float source_height;
} p;
layout(location=0) in vec2 source_uv;
layout(location=0) out vec4 color;
void main() {
  vec2 d=vec2(1.0/p.source_width,1.0/p.source_height);
  vec3 c=vec3(0);
  for(int y=-1;y<=1;y+=2) for(int x=-1;x<=1;x+=2) {
    vec3 v=texture(source_image,source_uv+vec2(x,y)*d).rgb;
    float luminance=v.r*0.2126+v.g*p.coeff_g+v.b*p.coeff_b;
    if(luminance>=p.threshold) c+=v/(1.0+luminance);
  }
  color=vec4(c*0.25,1);
}
