#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"
#include "clipmap.glsl"

layout(location = 0) rayPayloadInEXT vec3 HIT_VALUE;

void main()
{
    HIT_VALUE = vec3(0.0f);
}