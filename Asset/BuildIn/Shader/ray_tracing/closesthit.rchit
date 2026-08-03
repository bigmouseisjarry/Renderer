#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(push_constant) uniform ray_trace_base_setting {
	uint mode;
} RAY_TRACE_BASE_SETTING;

layout(location = 0) rayPayloadInEXT vec3 HIT_VALUE;
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

	float dist = length(worldPos.xyz - gl_WorldRayOriginEXT);

	if(RAY_TRACE_BASE_SETTING.mode == 0) HIT_VALUE = diffuse.xyz;
	if(RAY_TRACE_BASE_SETTING.mode == 1) HIT_VALUE = (normal.xyz + 1.0f) / 2.0f;
	if(RAY_TRACE_BASE_SETTING.mode == 2) HIT_VALUE = vec3(roughness, metallic, 0.0f);
	if(RAY_TRACE_BASE_SETTING.mode == 3) HIT_VALUE = worldPos.xyz;
	if(RAY_TRACE_BASE_SETTING.mode == 4) HIT_VALUE = vec3(dist / CAMERA.far);


    // surface cache
    SurfaceCacheData data;
    vec4 lighting;
    bool valid = FetchSurfaceCacheData(data, objectID, worldPos.xyz, worldNormal);
    valid = valid && FetchSurfaceCacheLighting(lighting, objectID, worldPos.xyz, worldNormal);
    if(RAY_TRACE_BASE_SETTING.mode == 5) HIT_VALUE = data.diffuse;
    if(RAY_TRACE_BASE_SETTING.mode == 6) HIT_VALUE = (data.normal.xyz + 1.0f) / 2.0f;
    if(RAY_TRACE_BASE_SETTING.mode == 7) HIT_VALUE = vec3(data.roughness, data.metallic, 0.0f);
    if(RAY_TRACE_BASE_SETTING.mode == 8) HIT_VALUE = data.emission.xyz;
    if(RAY_TRACE_BASE_SETTING.mode == 9) HIT_VALUE = lighting.xyz;
    if( RAY_TRACE_BASE_SETTING.mode >= 5 && 
        RAY_TRACE_BASE_SETTING.mode <= 9 &&
        !valid)                           HIT_VALUE = vec3(1.0, 0.0, 1.0);

}
