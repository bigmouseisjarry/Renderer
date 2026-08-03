#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"
#include "../clipmap/clipmap.glsl"
#include "common.glsl"

layout(location = 0) rayPayloadInEXT Payload RAY_PAYLOAD;

void main()
{
    RAY_PAYLOAD.objectID = 0;
    RAY_PAYLOAD.normal = vec3(0.0);
    RAY_PAYLOAD.diffuse = vec4(0.0);
    RAY_PAYLOAD.worldPos = vec3(0.0);
}