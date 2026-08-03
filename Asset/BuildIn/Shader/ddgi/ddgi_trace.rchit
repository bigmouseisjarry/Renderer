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
hitAttributeEXT vec2 HIT_ATTRIBS;

void main()
{
  	uint objectID       = gl_InstanceCustomIndexEXT;
    uint triangleID    	= gl_PrimitiveID;
	vec3 barycentrics 	= vec3(1.0f - HIT_ATTRIBS.x - HIT_ATTRIBS.y, HIT_ATTRIBS.x, HIT_ATTRIBS.y);


    mat4 model          = FetchModel(objectID);
    uvec3 index         = FetchTriangleIndex(objectID, triangleID, barycentrics);
    vec4 pos            = FetchTrianglePos(objectID, index, barycentrics);
    vec3 worldNormal    = FetchWorldNormal(FetchTriangleNormal(objectID, index, barycentrics), model);
    vec4 worldTangent   = FetchWorldTangent(FetchTriangleTangent(objectID, index, barycentrics), model);
    vec3 color          = FetchTriangleColor(objectID, index, barycentrics); 
    vec2 texCoord       = FetchTriangleTexCoord(objectID, index, barycentrics);    
    vec4 worldPos       = model * pos; 

	Material material   = FetchMaterial(objectID);
    vec4 diffuse        = FetchDiffuse(material, texCoord);
    vec3 emission       = FetchEmission(material);
    vec3 normal         = FetchNormal(material, texCoord, worldNormal, worldTangent);
    float roughness     = FetchRoughness(material, texCoord);
    float metallic      = FetchMetallic(material, texCoord);

    if(material.useVertexColor != 0)
        diffuse.xyz *= color.xyz;
    
	//RAY_PAYLOAD.dist      = length(worldPos.xyz - gl_WorldRayOriginEXT); 
    RAY_PAYLOAD.dist      = gl_HitTEXT; 
	RAY_PAYLOAD.normal    = vec4(worldNormal, 0.0f);
	RAY_PAYLOAD.diffuse   = vec4(diffuse.rgb, 0.0f);  	
	RAY_PAYLOAD.emission  = vec4(emission, 0.0f);

	RAY_PAYLOAD.normal.w  = roughness;   //粗糙度
	RAY_PAYLOAD.diffuse.w = metallic;  //金属度
}

