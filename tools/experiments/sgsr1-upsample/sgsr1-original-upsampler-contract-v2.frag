#version 450

//============================================================================================================
//
//
//                  Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
//                              SPDX-License-Identifier: BSD-3-Clause
//
//============================================================================================================

precision mediump float;
precision highp int;

////////////////////////
// USER CONFIGURATION //
////////////////////////

/*
* Operation modes:
* RGBA -> 1
* RGBY -> 3
* LERP -> 4
*/
#define OperationMode 1

#define EdgeThreshold 8.0/255.0

#define EdgeSharpness 2.0

// #define UseUniformBlock

////////////////////////
////////////////////////
////////////////////////

// Host-only contract candidate. NOT wired into runtime or production cache.
// Preserve the original ps_upsample_p descriptor/UBO interface and gain order.
layout(set=1,binding=1) uniform highp sampler2D ps0;
layout(set=1,binding=2) uniform highp sampler2D originalGain;
layout(set=1,binding=3,std140) uniform fp_c7 { highp vec4 originalC7[4096]; };
layout(location=2) in highp vec4 originalUV;
layout(location=0) out highp vec4 out_Target0;
highp vec4 ViewportInfo[1];
highp vec4 in_TEXCOORD0;
float fastLanczos2(float x)
{
	float wA = x-4.0;
	float wB = x*wA-wA;
	wA *= wA;
	return wB*wA;
}
vec2 weightY(float dx, float dy,float c, float std)
{
	float x = ((dx*dx)+(dy* dy))* 0.55 + clamp(abs(c)*std, 0.0, 1.0);
	float w = fastLanczos2(x);
	return vec2(w, w * c);	
}

vec3 sgsrFilter(highp vec2 mappedUV)
{
	in_TEXCOORD0=vec4(mappedUV,0.0,0.0);
	const int mode = OperationMode;
	float edgeThreshold = EdgeThreshold;
	float edgeSharpness = EdgeSharpness;

	vec4 color;
	if(mode == 1)
		color.xyz = textureLod(ps0,in_TEXCOORD0.xy,0.0).xyz;
	else
		color.xyzw = textureLod(ps0,in_TEXCOORD0.xy,0.0).xyzw;

	highp float xCenter;
	xCenter = abs(in_TEXCOORD0.x+-0.5);
	highp float yCenter;
	yCenter = abs(in_TEXCOORD0.y+-0.5);

	//todo: config the SR region based on needs
	//if ( mode!=4 && xCenter*xCenter+yCenter*yCenter<=0.4 * 0.4)
	if ( mode!=4)
	{
		highp vec2 imgCoord = ((in_TEXCOORD0.xy*ViewportInfo[0].zw)+vec2(-0.5,0.5));
		highp vec2 imgCoordPixel = floor(imgCoord);
		highp vec2 coord = (imgCoordPixel*ViewportInfo[0].xy);
		vec2 pl = (imgCoord+(-imgCoordPixel));
		vec4  left = textureGather(ps0,coord, mode);

		float edgeVote = abs(left.z - left.y) + abs(color[mode] - left.y)  + abs(color[mode] - left.z) ;
		if(edgeVote > edgeThreshold)
		{
			coord.x += ViewportInfo[0].x;

			vec4 right = textureGather(ps0,coord + vec2(ViewportInfo[0].x, 0.0), mode);
			vec4 upDown;
			upDown.xy = textureGather(ps0,coord + vec2(0.0, -ViewportInfo[0].y),mode).wz;
			upDown.zw  = textureGather(ps0,coord+ vec2(0.0, ViewportInfo[0].y), mode).yx;

			float mean = (left.y+left.z+right.x+right.w)*0.25;
			left = left - vec4(mean);
			right = right - vec4(mean);
			upDown = upDown - vec4(mean);
			color.w =color[mode] - mean;

			float sum = (((((abs(left.x)+abs(left.y))+abs(left.z))+abs(left.w))+(((abs(right.x)+abs(right.y))+abs(right.z))+abs(right.w)))+(((abs(upDown.x)+abs(upDown.y))+abs(upDown.z))+abs(upDown.w)));				
			float std = 2.181818/sum;
			
			vec2 aWY = weightY(pl.x, pl.y+1.0, upDown.x,std);				
			aWY += weightY(pl.x-1.0, pl.y+1.0, upDown.y,std);
			aWY += weightY(pl.x-1.0, pl.y-2.0, upDown.z,std);
			aWY += weightY(pl.x, pl.y-2.0, upDown.w,std);			
			aWY += weightY(pl.x+1.0, pl.y-1.0, left.x,std);
			aWY += weightY(pl.x, pl.y-1.0, left.y,std);
			aWY += weightY(pl.x, pl.y, left.z,std);
			aWY += weightY(pl.x+1.0, pl.y, left.w,std);
			aWY += weightY(pl.x-1.0, pl.y-1.0, right.x,std);
			aWY += weightY(pl.x-2.0, pl.y-1.0, right.y,std);
			aWY += weightY(pl.x-2.0, pl.y, right.z,std);
			aWY += weightY(pl.x-1.0, pl.y, right.w,std);

			float finalY = aWY.y/aWY.x;

			float maxY = max(max(left.y,left.z),max(right.x,right.w));
			float minY = min(min(left.y,left.z),min(right.x,right.w));
			finalY = clamp(edgeSharpness*finalY, minY, maxY);
					
			float deltaY = finalY -color.w;	
			
			//smooth high contrast input
			deltaY = clamp(deltaY, -23.0 / 255.0, 23.0 / 255.0);

			color.x = max((color.x+deltaY),0.0);
			color.y = max((color.y+deltaY),0.0);
			color.z = max((color.z+deltaY),0.0);
		}
	}

	color.w = 1.0;  //assume alpha channel is not used
	return color.xyz;
}

// The complete fallback comes from the current original decompiler output.
// Highp here preserves coordinate, constant and original arithmetic precision;
// the SGSR filter above retains its explicit official mediump arithmetic.
precision highp float;
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
    highp vec2 dims=vec2(textureSize(ps0,0));
    ViewportInfo[0]=vec4(1.0/dims,dims);
    highp vec2 uv=max(min(originalUV.xy*originalC7[0].xy,originalC7[2].zw),originalC7[2].xy);
    highp vec2 stepUV=ViewportInfo[0].xy;
    bool finite=finite2(originalUV.xy)&&finite2(originalC7[0].xy)&&
        finite2(originalC7[1].xy)&&finite2(originalC7[2].xy)&&finite2(originalC7[2].zw)&&
        !isnan(originalC7[1].z)&&!isinf(originalC7[1].z)&&finite2(uv);
    bool texelMatch=all(lessThanEqual(abs(originalC7[1].xy-stepUV),stepUV*0.0001));
    bool interior=all(greaterThan(uv,originalC7[2].xy+3.0*stepUV))&&
                  all(lessThan(uv,originalC7[2].zw-3.0*stepUV));
    if(!(finite&&texelMatch&&interior)) {originalComplete();return;}
    vec3 filtered=sgsrFilter(uv);
    highp vec2 gainUV=mix(originalUV.xy,vec2(0),isnan(originalUV.xy));
    float gain=texture(originalGain,gainUV).x;
    out_Target0=vec4(filtered*gain,1.0);
}
