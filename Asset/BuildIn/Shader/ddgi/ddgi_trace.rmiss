#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"
#include "ddgi_layout.glsl"

layout(push_constant) uniform DDGIComputeSetting {
	int volumeLightID;
	float _padding[3];
} SETTING;

layout(location = 0) rayPayloadInEXT DDGIPayload RAY_PAYLOAD;

void main()
{
	vec3 skyLight = FetchSkyLight(gl_WorldRayDirectionEXT);

	//RAY_PAYLOAD.dist	//初始化时已经是最远距离
	RAY_PAYLOAD.diffuse = vec4(skyLight, 0.0f);
}