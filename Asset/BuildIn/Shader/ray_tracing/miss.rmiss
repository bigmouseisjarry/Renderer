#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(push_constant) uniform ray_trace_base_setting {
	uint mode;
} RAY_TRACE_BASE_SETTING;

layout(location = 0) rayPayloadInEXT vec3 HIT_VALUE;

void main()
{
    HIT_VALUE = vec3(0.0f);
    
    if(RAY_TRACE_BASE_SETTING.mode == 9) HIT_VALUE = FetchSkyLight(gl_WorldRayDirectionEXT);
}