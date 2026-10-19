#version 450
layout(set=0,binding=0) uniform sampler2D source_image;
layout(location=0) out vec4 color;
void main() {
    // The runtime proves identical dimensions, RGBA8 UNORM, and full coverage.
    // No interpolation, sRGB conversion, crop, format reinterpretation or scale.
    color=texelFetch(source_image,ivec2(gl_FragCoord.xy),0);
}
