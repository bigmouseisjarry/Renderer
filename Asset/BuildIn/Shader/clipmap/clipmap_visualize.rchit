#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"
#include "clipmap.glsl"

layout(set = 1, binding = 0, rgba16f) uniform readonly image3D VXGI_CLIPMAP;
layout(set = 1, binding = 1) buffer VXGI_CLIPMAP_BUFFER {
	ClipmapInfo clipmapInfo;
};
layout(set = 1, binding = 2, rgba16f) uniform image2D OUT_COLOR;

layout(location = 0) rayPayloadInEXT vec3 HIT_VALUE;
hitAttributeEXT vec2 HIT_ATTRIBS;


vec4 SampleVoxel(vec4 worldPos)
{
    int mipLevel = FetchClipmapMipLevel(clipmapInfo, worldPos.xyz);
	ClipmapLevel clipmapLevel = clipmapInfo.levels[mipLevel];

    // 采样体素
    vec3 direction = normalize(gl_WorldRayDirectionEXT);
    ivec3 layers = FetchClipmapLayers(direction);
    vec3 weight = direction * direction;
    // vec3 weight = vec3(1.0f);
    vec4 outColor = vec4(0.0f);
    float totalWeight = 0.0f;
    for(int i = 0; i < 3; i++)
    {
        vec4 color = imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos.xyz, layers[i]));
        if(color.a > 0.001)
        {
            outColor += color * weight[i];
            totalWeight += weight[i];
        }
    }
    if(totalWeight > 0.001) outColor /= totalWeight;    

    return outColor;
}

void main()
{
    uint objectID       = gl_InstanceCustomIndexEXT;
    uint triangleID    	= gl_PrimitiveID;
	vec3 barycentrics 	= vec3(1.0f - HIT_ATTRIBS.x - HIT_ATTRIBS.y, HIT_ATTRIBS.x, HIT_ATTRIBS.y);

    mat4 model          = FetchModel(objectID);
    uvec3 index         = FetchTriangleIndex(objectID, triangleID, barycentrics);
    vec4 pos            = FetchTrianglePos(objectID, index, barycentrics);
    vec4 worldPos       = model * pos; 

    // vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;    // 精度不高

    vec4 outColor = SampleVoxel(worldPos);

    // vec4 colors[6] = {
	// 	imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 0)),
	// 	imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 1)),
	// 	imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 2)),
	// 	imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 3)),
	// 	imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 4)),
	// 	imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 5)),
	// };
    
    HIT_VALUE = outColor.rgb;
}
