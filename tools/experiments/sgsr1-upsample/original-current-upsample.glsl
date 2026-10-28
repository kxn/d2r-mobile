#version 450

layout(binding = 3, std140) uniform fp_c7
{
    vec4 _m0[4096];
} fp_c7_1;

layout(binding = 1) uniform sampler2D fp_t_cb196610_1057006E;
layout(binding = 2) uniform sampler2D fp_t_cb196610_10578070;

layout(location = 2) in vec4 _29;
layout(location = 0) out vec4 _32;

void main()
{
    float _142 = max(min(_29.x * fp_c7_1._m0[0].x, fp_c7_1._m0[2].z), fp_c7_1._m0[2].x);
    float _146 = max(min(_29.y * fp_c7_1._m0[0].y, fp_c7_1._m0[2].w), fp_c7_1._m0[2].y);
    vec2 _167 = vec2(_142 + (-fp_c7_1._m0[1].x), _146);
    vec3 _175 = textureLod(fp_t_cb196610_1057006E, mix(_167, vec2(0.0), isnan(_167)), 0.0).xyz;
    vec2 _179 = vec2(_142, _146);
    vec3 _183 = textureLod(fp_t_cb196610_1057006E, mix(_179, vec2(0.0), isnan(_179)), 0.0).xyz;
    float _185 = _183.x;
    float _187 = _183.y;
    float _189 = _183.z;
    vec2 _195 = vec2(_142, _146 + (-fp_c7_1._m0[1].y));
    vec3 _199 = textureLod(fp_t_cb196610_1057006E, mix(_195, vec2(0.0), isnan(_195)), 0.0).xyz;
    vec2 _203 = vec2(_142 + fp_c7_1._m0[1].x, _146);
    vec3 _207 = textureLod(fp_t_cb196610_1057006E, mix(_203, vec2(0.0), isnan(_203)), 0.0).xyz;
    vec2 _211 = vec2(_29.x, _29.y);
    vec4 _214 = texture(fp_t_cb196610_10578070, mix(_211, vec2(0.0), isnan(_211)));
    float _215 = _214.x;
    vec2 _219 = vec2(_142, _146 + fp_c7_1._m0[1].y);
    vec3 _223 = textureLod(fp_t_cb196610_1057006E, mix(_219, vec2(0.0), isnan(_219)), 0.0).xyz;
    _32.x = fma(_223.x, -fp_c7_1._m0[1].z, fma(_207.x, -fp_c7_1._m0[1].z, fma(_199.x, -fp_c7_1._m0[1].z, _175.x * (-fp_c7_1._m0[1].z)) + fma(_185 * fp_c7_1._m0[1].z, 4.0, _185))) * _215;
    _32.y = fma(_223.y, -fp_c7_1._m0[1].z, fma(_207.y, -fp_c7_1._m0[1].z, fma(_199.y, -fp_c7_1._m0[1].z, _175.y * (-fp_c7_1._m0[1].z)) + fma(_187 * fp_c7_1._m0[1].z, 4.0, _187))) * _215;
    _32.z = fma(_223.z, -fp_c7_1._m0[1].z, fma(_207.z, -fp_c7_1._m0[1].z, fma(_199.z, -fp_c7_1._m0[1].z, _175.z * (-fp_c7_1._m0[1].z)) + fma(_189 * fp_c7_1._m0[1].z, 4.0, _189))) * _215;
    _32.w = 1.0;
}

