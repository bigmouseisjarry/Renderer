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

// gl_WorldRayDirectionEXT		 vec3		世界空间射线方向	 
// gl_WorldRayOriginEXT			 vec3		世界空间射线起点	 
// gl_RayTminEXT				 float		traceRayEXT 传入的 tMin	 
// gl_RayTmaxEXT				 float		射线的当前最大距离	 
// gl_LaunchIDEXT				 uvec3		本次 dispatch 里的调用坐标	 
// gl_LaunchSizeEXT				 uvec3		dispatch 的三维尺寸	 
// gl_IncomingRayFlagsEXT		 uint		traceRayEXT 传入的 ray flags	 
// gl_CullMaskEXT				 uint		traceRayEXT 传入的 cull mask	 

void main()
{
	vec3 skyLight = FetchSkyLight(gl_WorldRayDirectionEXT);

	//RAY_PAYLOAD.dist = gl_RayTmaxEXT;	//初始化时已经是最远距离
	RAY_PAYLOAD.albedo = vec4(skyLight, 0.0f);
}