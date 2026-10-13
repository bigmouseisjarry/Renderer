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
// 命中点的重心坐标，由硬件的固定函数三角形求交器自动写入
hitAttributeEXT vec2 HIT_ATTRIBS;

//                   rayPayloadEXT                       hitAttributeEXT
//                                                       
// 写入方            任意持有者（rgen/hit 系）           rint（自定义几何）或固定函数求交器
//                                                       
// 读取方            配对的另一端                        rahit / rchit
//                                                       
// 方向              双向                                单向
//                                                       
// 类型              任意值类型 struct                   float~vec4
//                                                       
// 生命周期          整次 trace                          单次命中 
//                                                       
// 槽位              location 0..7 可多个                一个



// gl_InstanceCustomIndexEXT                     uint                build TLAS 时填的 24-bit 自定义ID，稳定不随 build 顺序变
// gl_PrimitiveID                                uint                geometry 内的三角形下标
// gl_InstanceID                                 uint                实例在 TLAS 数组里的下标,紧凑但随 build 顺序变
// gl_GeometryIndexEXT                           uint                实例的 BLAS 内有多个 geometry 时，命中在第几个
// gl_HitKindEXT                                 uint                三角形正面 = 254、背面 = 255，自定义几何则是 rint 传的用户值
//                                                                   
//                                                                   
// gl_HitTEXT                                    float               命中距离 t
// hitAttributeEXT                               vec2                重心坐标
// gl_RayTmaxEXT                                 float               在 rchit 里恒等于 gl_HitTEXT
//                                                                   
//                                                                   
// gl_WorldRayOriginEXT / DirectionEXT           vec3                世界空间射线
// gl_ObjectRayOriginEXT / DirectionEXT          vec3                物体空间射线
// gl_RayTminEXT                                 float               traceRayEXT 的 tMin
// gl_IncomingRayFlagsEXT                        uint                ray flags
// gl_CullMaskEXT                                uint                traceRayEXT 传入的 cull mask
//                                                                   
//                                                                   
// gl_ObjectToWorldEXT                           mat4                该实例的 物体→世界 矩阵
// gl_WorldToObjectEXT                           mat4                世界→物体（逆）

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
    vec4 albedo         = FetchAlbedo(material, texCoord);
    vec3 emission       = FetchEmission(material);
    vec3 normal         = FetchNormal(material, texCoord, worldNormal, worldTangent);
    float roughness     = FetchRoughness(material, texCoord);
    float metallic      = FetchMetallic(material, texCoord);

    if(material.useVertexColor != 0)
        albedo.xyz *= color.xyz;
    
	//RAY_PAYLOAD.dist      = length(worldPos.xyz - gl_WorldRayOriginEXT); 
    RAY_PAYLOAD.dist      = gl_HitTEXT; 
	RAY_PAYLOAD.normal    = vec4(worldNormal, 0.0f);
	RAY_PAYLOAD.albedo    = vec4(albedo.rgb, 0.0f);	
    RAY_PAYLOAD.emission  = vec4(emission, 0.0f);
	RAY_PAYLOAD.normal.w  = roughness;   //粗糙度
	RAY_PAYLOAD.albedo.w  = metallic;  //金属度
}

