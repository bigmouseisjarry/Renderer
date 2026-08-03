#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"
#include "common.glsl"

layout(location = 0) rayPayloadInEXT Payload RAY_PAYLOAD;

void main()
{
    RAY_PAYLOAD.throughput = vec3(0.0);
	RAY_PAYLOAD.lightColor = FetchSkyLight(gl_WorldRayDirectionEXT);
    RAY_PAYLOAD.distance = MAX_RAY_TRACING_DISTANCE;
    RAY_PAYLOAD.pdf = 1.0f;
    RAY_PAYLOAD.numBounce++;
}