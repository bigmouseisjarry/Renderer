#version 450
#extension GL_GOOGLE_include_directive : enable

#define ENABLE_RAY_TRACING 0
#include "../common/common.glsl"
#include "clipmap.glsl"

layout(location = 0) in vec4 IN_COLOR;
layout(location = 1) in vec2 IN_UV;
layout(location = 0) out vec4 OUT_COLOR;

void main() 
{
	float borderWidth = 0.05f;

	OUT_COLOR = IN_COLOR;
	//OUT_COLOR = mix(vec4(0.0, 0.0, 0.0, 1.0), IN_COLOR, min(min(IN_UV.x, min(IN_UV.y, min((1.0 - IN_UV.x), (1.0 - IN_UV.y)))) / borderWidth, 1.0));

	if(	IN_UV.x < borderWidth || IN_UV.x > 1.0 - borderWidth ||
	 	IN_UV.y < borderWidth || IN_UV.y > 1.0 - borderWidth)	OUT_COLOR = vec4(0.0, 0.0, 0.0, 1.0);
	//else 														OUT_COLOR = vec4(0.0, 0.0, 0.0, 0.0);
	//OUT_COLOR = vec4(IN_UV.xy, 0.0, 1.0);
}
