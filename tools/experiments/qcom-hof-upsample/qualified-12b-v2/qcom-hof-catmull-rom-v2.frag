#version 450
#extension GL_QCOM_image_processing : require
precision highp float;
precision highp int;
// Independent 12b-qualified coordinate experiment, no production integration.
// Required legal runtime resources, NOT created by this compile probe:
// binding4 = same source image as binding1, dedicated IMAGE_PROCESSING sampler;
// binding5 = R16_SFLOAT 2D_ARRAY SAMPLE_WEIGHT view, filterSize4x4,
// filterCenter(1,1), numPhases1024, per-phase Catmull-Rom table described below.
// 12b empirical firsttap=floor(q-C-.5), nearestphase=frac(q-.5). Use q=u;
// paired table representative t=h/32. Not universal VulkanQCOM semantics.
// Table weights are Catmull-Rom offsets[-1,0,1,2] at t, outer product in2D;
// layer=vPhase*32+hPhase. R16-quantized sum needs actual output qualification.
// Fixed kernel deliberately replaces C7.k interior sharpening, NOT NVN equivalence.
layout(set=1,binding=1) uniform sampler2D ps0;
layout(set=1,binding=2) uniform sampler2D originalGain;
layout(set=1,binding=3,std140) uniform fp_c7 { vec4 originalC7[4096]; };
layout(set=1,binding=4) uniform sampler2D hardwareSource;
layout(set=1,binding=5) uniform sampler2DArray hardwareWeights;
layout(location=2) in vec4 originalUV;
layout(location=0) out vec4 out_Target0;
void originalComplete()
{
    float _142 = max(min(originalUV.x * originalC7[0].x, originalC7[2].z), originalC7[2].x);
    float _146 = max(min(originalUV.y * originalC7[0].y, originalC7[2].w), originalC7[2].y);
    vec2 _167 = vec2(_142 + (-originalC7[1].x), _146);
    vec3 _175 = textureLod(ps0, mix(_167, vec2(0.0), isnan(_167)), 0.0).xyz;
    vec2 _179 = vec2(_142, _146);
    vec3 _183 = textureLod(ps0, mix(_179, vec2(0.0), isnan(_179)), 0.0).xyz;
    float _185 = _183.x;
    float _187 = _183.y;
    float _189 = _183.z;
    vec2 _195 = vec2(_142, _146 + (-originalC7[1].y));
    vec3 _199 = textureLod(ps0, mix(_195, vec2(0.0), isnan(_195)), 0.0).xyz;
    vec2 _203 = vec2(_142 + originalC7[1].x, _146);
    vec3 _207 = textureLod(ps0, mix(_203, vec2(0.0), isnan(_203)), 0.0).xyz;
    vec2 _211 = vec2(originalUV.x, originalUV.y);
    vec4 _214 = texture(originalGain, mix(_211, vec2(0.0), isnan(_211)));
    float _215 = _214.x;
    vec2 _219 = vec2(_142, _146 + originalC7[1].y);
    vec3 _223 = textureLod(ps0, mix(_219, vec2(0.0), isnan(_219)), 0.0).xyz;
    out_Target0.x = fma(_223.x, -originalC7[1].z, fma(_207.x, -originalC7[1].z, fma(_199.x, -originalC7[1].z, _175.x * (-originalC7[1].z)) + fma(_185 * originalC7[1].z, 4.0, _185))) * _215;
    out_Target0.y = fma(_223.y, -originalC7[1].z, fma(_207.y, -originalC7[1].z, fma(_199.y, -originalC7[1].z, _175.y * (-originalC7[1].z)) + fma(_187 * originalC7[1].z, 4.0, _187))) * _215;
    out_Target0.z = fma(_223.z, -originalC7[1].z, fma(_207.z, -originalC7[1].z, fma(_199.z, -originalC7[1].z, _175.z * (-originalC7[1].z)) + fma(_189 * originalC7[1].z, 4.0, _189))) * _215;
    out_Target0.w = 1.0;
}


bool finite2(vec2 a) {return !any(isnan(a))&&!any(isinf(a));}
void main() {
    vec2 dims=vec2(textureSize(ps0,0));
    vec2 stepUV=1.0/dims;
    vec2 uv=max(min(originalUV.xy*originalC7[0].xy,originalC7[2].zw),originalC7[2].xy);
    bool finite=finite2(originalUV.xy)&&finite2(originalC7[0].xy)&&
        finite2(originalC7[1].xy)&&finite2(originalC7[2].xy)&&finite2(originalC7[2].zw)&&
        !isnan(originalC7[1].z)&&!isinf(originalC7[1].z)&&finite2(uv);
    bool interior=all(greaterThan(uv,originalC7[2].xy+3.0*stepUV))&&
                  all(lessThan(uv,originalC7[2].zw-3.0*stepUV));
    if(!(finite&&interior)) {originalComplete();return;}
    vec2 hardwareUV=uv;
    vec3 filtered=textureWeightedQCOM(hardwareSource,hardwareUV,hardwareWeights).xyz;
    if(any(isnan(filtered))||any(isinf(filtered))) {originalComplete();return;}
    // No upper1 clipping before original gain; alpha and raw gainUV preserved.
    vec2 gainUV=mix(originalUV.xy,vec2(0),isnan(originalUV.xy));
    float gain=texture(originalGain,gainUV).x;
    out_Target0=vec4(filtered*gain,1.0);
}
