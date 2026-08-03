#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_samplerless_texture_functions : enable
#extension GL_ARB_separate_shader_objects : enable

#include "math.glsl"
#include "intersection.glsl"
#include "rand.glsl"

#ifndef ENABLE_RAY_TRACING
#define ENABLE_RAY_TRACING 1                        //启用硬件光追
#endif

#define FRAMES_IN_FLIGHT 2							//帧缓冲数目
#define WINDOW_WIDTH 2048                           //32 * 64   16 * 128
#define WINDOW_HEIGHT 1152                          //18 * 64   9 * 128
//#define WINDOW_WIDTH 1920                           
//#define WINDOW_HEIGHT 1080    

#define HALF_WINDOW_WIDTH (WINDOW_WIDTH / 2)
#define HALF_WINDOW_HEIGHT (WINDOW_HEIGHT / 2)

#define MAX_BINDLESS_RESOURCE_SIZE 102400	        //bindless 单个binding的最大描述符数目
#define MAX_PER_FRAME_RESOURCE_SIZE 102400			//全局的缓冲大小（个数）,包括动画，材质等
#define MAX_PER_FRAME_OBJECT_SIZE 102400		    //全局最大支持的物体数目

#define DIRECTIONAL_SHADOW_SIZE 4096				//方向光源尺寸
#define DIRECTIONAL_SHADOW_CASCADE_LEVEL 4			//CSM级数

#define POINT_SHADOW_SIZE 512						//点光源尺寸，分辨率对性能影响也比较大
#define MAX_POINT_SHADOW_COUNT 4					//阴影点光源最大数目
#define MAX_POINT_LIGHT_COUNT 10240					//点光源最大数目
#define MAX_VOLUME_LIGHT_COUNT 100                   //体积光源最大数目

#define CLUSTER_TRIANGLE_SIZE 128                   //每个cluster内的三角形数目，固定尺寸
#define CLUSTER_GROUP_SIZE 32                       //每个cluster ghroup内的最大cluster数目
#define MAX_PER_FRAME_CLUSTER_SIZE 102400           //全局最大支持的cluster数目
#define MAX_PER_FRAME_CLUSTER_GROUP_SIZE 20480      //全局最大支持的cluster group数目
#define MAX_PER_PASS_PIPELINE_STATE_COUNT 64        //每个mesh pass支持的最大的不同管线状态数目
#define MAX_SUPPORTED_MESH_PASS_COUNT 32            //全局支持的最大mesh pass数目 

#define MAX_LIGHTS_PER_CLUSTER 8                    //每个cluster最多支持存储的光源数目
#define LIGHT_CLUSTER_GRID_SIZE 64                  //cluster based lighting裁剪时使用的tile像素尺寸
#define LIGHT_CLUSTER_DEPTH 128                     //cluster based lighting裁剪时Z轴的划分数量    
#define LIGHT_CLUSTER_WIDTH (WINDOW_WIDTH / LIGHT_CLUSTER_GRID_SIZE)    // X轴
#define LIGHT_CLUSTER_HEIGHT (WINDOW_HEIGHT / LIGHT_CLUSTER_GRID_SIZE)  // Y轴
#define LIGHT_CLUSTER_NUM   (LIGHT_CLUSTER_WIDTH * LIGHT_CLUSTER_HEIGHT * LIGHT_CLUSTER_DEPTH)

#define DIFFUSE_IBL_SIZE 32							//漫反射IBL尺寸
#define SPECULAR_IBL_SIZE 128						//镜面反射IBL尺寸

#define MIN_RAY_TRACING_DISTANCE 0.001              //光追最短追踪距离
#define MAX_RAY_TRACING_DISTANCE 10000.0            //光追最长追踪距离

#define DDGI_IRRADIANCE_PROBE_SIZE 8                //DDGI使用的辐照度贴图，单个probe的纹理尺寸
#define DDGI_DEPTH_PROBE_SIZE 16                    //DDGI使用的深度贴图，单个probe的纹理尺寸

#define HALF_SIZE_SSSR true                         //SSSR是否使用半精度

#define VOLUMETRIC_FOG_SIZE_X 320                   //体积雾使用的屏幕空间texture3d的分辨率
#define VOLUMETRIC_FOG_SIZE_Y 180
#define VOLUMETRIC_FOG_SIZE_Z 128

#define CLIPMAP_VOXEL_COUNT 64                      //clipmap的边长
#define CLIPMAP_MIN_VOXEL_SIZE 0.2                  //clipmap的mip0层级体素尺寸
#define CLIPMAP_MIPLEVEL 5                          //clipmap的mip层级

#define MAX_GIZMO_PRIMITIVE_COUNT 102400            //gizmo可以绘制的最大图元数目

#define SURFACE_CACHE_SIZE 4096
#define SURFACE_CACHE_PADDING 1	                        // 所有分配块的左边和上边是一像素的padding
#define MAX_SURFACE_CACHE_LOD 5	                        // 取小于
#define MAX_SURFACE_CACHE_LOD_SIZE 128			        // 最大的一级LOD的单块分辨率
#define MIN_SURFACE_CACHE_LOD_SIZE 8			        // 最小的一级LOD的单块分辨率，也是光照计算的基本单位
#define SURFACE_CACHE_TILE_COUNT (SURFACE_CACHE_SIZE / MIN_SURFACE_CACHE_LOD_SIZE)
#define MAX_SURFACE_CACHE_RASTERIZE_SIZE 256	        // 每帧最大光栅化buget，UE是512
#define MAX_SURFACE_CACHE_DIRECT_LIGHTING_SIZE 1024	    // 每帧最大直接光照buget
#define MAX_SURFACE_CACHE_INDIRECT_LIGHTING_SIZE 512    // 每帧最大间接光照buget
#define SURFACE_CACHE_DIRECT_LIGHTING_TILE_SIZE 8	    // 直接光照tile尺寸
#define SURFACE_CACHE_DIRECT_INLIGHTING_TILE_SIZE 4	    // 间接光照tile尺寸
#define SURFACE_CACHE_LOD_SIZE(x) (MAX_SURFACE_CACHE_LOD_SIZE >> (x))

///////////////////////////////////////////////////////////////////////////////

#define PER_FRAME_BINDING_GLOBAL_SETTING                0
#define PER_FRAME_BINDING_TLAS                          1
#define PER_FRAME_BINDING_CAMERA                        2
#define PER_FRAME_BINDING_OBJECT                        3
#define PER_FRAME_BINDING_MATERIAL                      4
#define PER_FRAME_BINDING_LIGHT                         5
#define PER_FRAME_BINDING_LIGHT_CLUSTER_GRID            6
#define PER_FRAME_BINDING_LIGHT_CLUSTER_INDEX           7
#define PER_FRAME_BINDING_DIRECTIONAL_SHADOW            8
#define PER_FRAME_BINDING_POINT_SHADOW                  9
#define PER_FRAME_BINDING_VOLUME_LIGHT_IRRADIANCE       10
#define PER_FRAME_BINDING_VOLUME_LIGHT_DEPTH            11
#define PER_FRAME_BINDING_SKYBOX_IBL                    12
#define PER_FRAME_BINDING_MESH_CLUSTER                  13
#define PER_FRAME_BINDING_MESH_CLUSTER_GROUP            14  
#define PER_FRAME_BINDING_MESH_CLUSTER_DRAW_INFO        15
#define PER_FRAME_BINDING_MESH_CARD                     16
#define PER_FRAME_BINDING_MESH_CARD_READBACK            17
#define PER_FRAME_BINDING_SURFACE_CACHE                 18
#define PER_FRAME_BINDING_DEPTH                         19
#define PER_FRAME_BINDING_DEPTH_PYRAMID                 20
#define PER_FRAME_BINDING_VELOCITY                      21
#define PER_FRAME_BINDING_OBJECT_ID                     22
#define PER_FRAME_BINDING_VERTEX                        23
#define PER_FRAME_BINDING_GIZMO                         24
#define PER_FRAME_BINDING_BINDLESS_POSITION             25
#define PER_FRAME_BINDING_BINDLESS_NORMAL               26
#define PER_FRAME_BINDING_BINDLESS_TANGENT              27
#define PER_FRAME_BINDING_BINDLESS_TEXCOORD             28
#define PER_FRAME_BINDING_BINDLESS_COLOR                29
#define PER_FRAME_BINDING_BINDLESS_BONE_INDEX           30
#define PER_FRAME_BINDING_BINDLESS_BONE_WEIGHT          31
#define PER_FRAME_BINDING_BINDLESS_ANIMATION            32
#define PER_FRAME_BINDING_BINDLESS_INDEX                33
#define PER_FRAME_BINDING_BINDLESS_SAMPLER              34
#define PER_FRAME_BINDING_BINDLESS_TEXTURE_1D           35
#define PER_FRAME_BINDING_BINDLESS_TEXTURE_1D_ARRAY     36
#define PER_FRAME_BINDING_BINDLESS_TEXTURE_2D           37
#define PER_FRAME_BINDING_BINDLESS_TEXTURE_2D_ARRAY     38
#define PER_FRAME_BINDING_BINDLESS_TEXTURE_CUBE         39
#define PER_FRAME_BINDING_BINDLESS_TEXTURE_3D           40

#if(ENABLE_RAY_TRACING != 0)
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
layout(set = 0, binding = PER_FRAME_BINDING_TLAS) uniform accelerationStructureEXT TLAS;
#endif

struct RHIIndexedIndirectCommand 
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct RHIIndirectCommand 
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

struct GlobalIconInfo           //图标的bindless索引
{
    // uint documentationIcon;
    // uint worldIcon;
    // uint entityIcon;
    uint cameraIcon;
    uint directionalLightIcon;
    uint pointLightIcon;
    // uint spotLightIcon;
    // uint skyLightIcon;
    // uint materialIcon;
    // uint textIcon;
    // uint decalIcon;
    // uint postProcessVolumeIcon;
    // uint exponentialHeightFogIcon;
    // uint volumetricCloudIcon;
    // uint atmosphericFogIcon;
    // uint skyAtmosphereIcon;

};

struct Object 
{
    mat4 model;
    mat4 prevModel;
    mat4 invModel;

    uint animationID;           //动画buffer索引
    uint materialID;            //材质buffer索引
    uint vertexID;
    uint indexID;  
    uint meshCardID;            //card索引起始值
    uint _padding[3];           

    BoundingSphere sphere;  
    BoundingBox box;
    vec4 localScale;            //缩放值，物体可能带有从局部坐标到local的变换
    vec4 modelScale;

    vec4 debugData;
};

struct VertexStream
{
    uint positionID;
    uint normalID;
    uint tangentID;
    uint texCoordID;
    uint colorID;
    uint boneIndexID;              
    uint boneWeightID;
    uint _padding;
};

struct Vertex
{
    vec4 pos;
    vec3 normal;
    vec4 tangent;
    vec3 color;
    vec2 texCoord;
};

struct Material 
{
    float roughness;
    float metallic;
    float alphaClip;
    uint useVertexColor;

    vec4 baseColor;
    vec4 emission;

    uint textureDiffuse;
    uint textureNormal;
    uint textureArm;        //AO/Roughness/Metallic
    uint textureSpecular;

    vec2 textureOffset;
    vec2 textureScale;

    int ints[8];       
    float floats[8];   

    vec4 colors[8];

    uint textureSlots2D[8]; 
    uint textureSlotsCube[4];
    uint textureSlots3D[4];  
};

struct LightClusterIndex
{
    uint lightID;             
};

struct DirectionalLight
{
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec3 pos;
    float _padding0;
    vec3 dir;
    float depth;              

    vec3 color;
    float intencity;            
    
    float fogScattering;   
    uint castShadow;        
    float _padding1[2];   

    Frustum frustum;            
    BoundingSphere sphere;
};

struct PointLight
{
    mat4 view[6];
    mat4 proj;
    mat4 viewProj[6];
    vec3 pos;
    vec3 color;
    float intencity;
    float near;
    float far;                             
    float bias;
    float _padding1;
     
    float c1;                          
    float c2;
    bool enable;
    uint shadowID;                    

    float fogScattering;                 
    float _padding2[3];

    Frustum frustum[6];  
    BoundingSphere sphere;                
};

struct DDGISetting
{
    vec3  gridStartPosition;
    vec3  gridStep;
    ivec3 probeCounts;
    float _padding2;

    float depthSharpness;
    float blendWeight;
    float normalBias;
    float energyPreservation;

    int irradianceTextureWidth;
    int irradianceTextureHeight;
    int depthTextureWidth;
    int depthTextureHeight;

    float maxProbeDistance;
    int   raysPerProbe;
    float _padding3[2];

    bool enable; 
    bool visibilityTest; 
    bool infiniteBounce;
    bool randomOrientation;

    BoundingBox boundingBox;   
};

struct VolumeLight
{
    DDGISetting setting;
};

struct LightSetting 
{
    uint directionalLightCnt;
    uint pointshadowedLightCnt;
    uint pointLightCnt;
    uint volumeLightCnt;

    uint globalIndexOffset;                          
    uint _padding[3];

    uint pointLightIDs[MAX_POINT_LIGHT_COUNT];
    uint pointShadowLightIDs[MAX_POINT_SHADOW_COUNT];  

    uint volumeLightIDs[MAX_VOLUME_LIGHT_COUNT];
};

struct GizmoBoxInfo 
{
    vec3 center;
    float _padding0;
    vec3 extent;
    float _padding1;
    vec4 color;
};

struct GizmoSphereInfo 
{
    vec3 center;
    float radious;
    vec4 color;
};

struct GizmoLineInfo 
{
    vec3 from;
    float _padding0;
    vec3 to;
    float _padding1;
    vec4 color;
};

struct GizmoBillboardInfo 
{
    vec3 center;
    uint textureID;
    vec2 extent;
    vec2 _padding;
    vec4 color;
};

struct MeshCluster
{
    uint vertexID;		      
    uint indexID;
    uint indexOffset;
    float lodError;

    BoundingSphere sphere;
};

struct MeshClusterGroup
{
    uint clusterID[CLUSTER_GROUP_SIZE]; 

    uint clusterSize;
    float parentLodError;  
    uint mipLevel;
    float _padding;

    BoundingSphere sphere;
};

struct MeshClusterDrawInfo
{
    uint objectID;      // 每个cluster对应的物体的实例索引
    uint clusterID;     // 对cluster的索引
};

struct MeshCardInfo     // 单个Card的全部信息
{
	vec3 viewPosition;
    float _padding0;
	vec3 viewExtent;
	float _padding1;
    vec3 scale;
    float _padding2;

	mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 invView;
    mat4 invProj;

	uvec2 atlasOffset;
	uvec2 atlasExtent;

    uvec2 sampleAtlasOffset;
    uvec2 sampleAtlasExtent;
};

layout(set = 0, binding = PER_FRAME_BINDING_GLOBAL_SETTING) buffer global_setting {
    
    uint skyboxMaterialID;
    uint clusterInspectMode;    
    uint totalTicks;
    float totalTickTime;
    float minFrameTime;
    float rayTracingOffset;

    GlobalIconInfo icons;

} GLOBAL_SETTING;

// layout(set = 0, binding = PER_FRAME_BINDING_TLAS) uniform accelerationStructureEXT TLAS;

layout(set = 0, binding = PER_FRAME_BINDING_CAMERA) uniform camera {

    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 prevView;
    mat4 prevProj;
    mat4 prevViewProj;
    mat4 invView;
    mat4 invProj;

    vec4 pos;  

    vec3 front;
    vec3 up;
    vec3 right;
    float _padding;

    float near;   
    float far;
    float fov;
    float aspect;       

    Frustum frustum;
            
} CAMERA;

layout(set = 0, binding = PER_FRAME_BINDING_OBJECT) readonly buffer objects {

    Object slot[MAX_PER_FRAME_OBJECT_SIZE];

} OBJECTS;

layout(set = 0, binding = PER_FRAME_BINDING_MATERIAL) readonly buffer materials { 

    Material slot[MAX_PER_FRAME_RESOURCE_SIZE];

} MATERIALS;

layout(set = 0, binding = PER_FRAME_BINDING_LIGHT) buffer lights {

    DirectionalLight directionalLights[DIRECTIONAL_SHADOW_CASCADE_LEVEL];
    PointLight pointLights[MAX_POINT_LIGHT_COUNT];
    VolumeLight volumeLights[MAX_VOLUME_LIGHT_COUNT];
    LightSetting lightSetting;

} LIGHTS;

layout(set = 0, binding = PER_FRAME_BINDING_LIGHT_CLUSTER_GRID, rg32ui) uniform uimage3D LIGHT_CLUSTER_GRID;

layout(set = 0, binding = PER_FRAME_BINDING_LIGHT_CLUSTER_INDEX) buffer light_cluster_index {

    LightClusterIndex slot[MAX_LIGHTS_PER_CLUSTER * LIGHT_CLUSTER_NUM];

} LIGHT_CLUSTER_INDEX;

layout(set = 0, binding = PER_FRAME_BINDING_DIRECTIONAL_SHADOW) uniform texture2D DIRECTIONAL_SHADOW[];

layout(set = 0, binding = PER_FRAME_BINDING_POINT_SHADOW) uniform textureCube POINT_SHADOW[];

layout(set = 0, binding = PER_FRAME_BINDING_VOLUME_LIGHT_IRRADIANCE) uniform texture2D VOLUME_LIGHT_IRRADIANCE[];

layout(set = 0, binding = PER_FRAME_BINDING_VOLUME_LIGHT_DEPTH) uniform texture2D VOLUME_LIGHT_DEPTH[];

layout(set = 0, binding = PER_FRAME_BINDING_SKYBOX_IBL) uniform textureCube SKYBOX_IBL[2];    // diffuse, specular

layout(set = 0, binding = PER_FRAME_BINDING_MESH_CLUSTER) readonly buffer mesh_clusters { 

    MeshCluster slot[MAX_PER_FRAME_CLUSTER_SIZE];

} MESH_CLUSTERS;

layout(set = 0, binding = PER_FRAME_BINDING_MESH_CLUSTER_GROUP) readonly buffer mesh_cluster_groups { 

    MeshClusterGroup slot[MAX_PER_FRAME_CLUSTER_SIZE];

} MESH_CLUSTER_GROUPS;

layout(set = 0, binding = PER_FRAME_BINDING_MESH_CLUSTER_DRAW_INFO) buffer mesh_cluster_draw_infos { 

    MeshClusterDrawInfo slot[MAX_PER_FRAME_CLUSTER_SIZE * MAX_SUPPORTED_MESH_PASS_COUNT];

} MESH_CLUSTER_DRAW_INFOS;

layout(set = 0, binding = PER_FRAME_BINDING_MESH_CARD) readonly buffer mesh_cards { 

    MeshCardInfo slot[MAX_PER_FRAME_OBJECT_SIZE * 6];

} MESH_CARDS;

layout(set = 0, binding = PER_FRAME_BINDING_MESH_CARD_READBACK) buffer mesh_card_readbacks { 

    int slot[MAX_PER_FRAME_OBJECT_SIZE * 6];

} MESH_CARD_READBACKS;

layout(set = 0, binding = PER_FRAME_BINDING_SURFACE_CACHE) uniform texture2D SURFACE_CACHE[5];  // diffuse normal emmision lighting depth  

layout(set = 0, binding = PER_FRAME_BINDING_DEPTH) uniform texture2D DEPTH[2];  // 本帧，前一帧

layout(set = 0, binding = PER_FRAME_BINDING_DEPTH_PYRAMID) uniform texture2D DEPTH_PYRAMID[2];  // MIN, MAX

layout(set = 0, binding = PER_FRAME_BINDING_VELOCITY) uniform texture2D VELOCITY;

layout(set = 0, binding = PER_FRAME_BINDING_OBJECT_ID) uniform utexture2D OBJECT_ID[2];

layout(set = 0, binding = PER_FRAME_BINDING_VERTEX) readonly buffer vertices { 

    VertexStream slot[MAX_PER_FRAME_RESOURCE_SIZE];

} VERTICES;

layout(set = 0, binding = PER_FRAME_BINDING_GIZMO) buffer gizmoDrawData 
{ 
    RHIIndexedIndirectCommand command[4];
    GizmoBoxInfo boxes[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoSphereInfo spheres[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoLineInfo lines[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoBillboardInfo worldBillboards[MAX_GIZMO_PRIMITIVE_COUNT];

} GIZMO_DRAW_DATA;

layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_POSITION) readonly buffer positions { 

    float position[];

} POSITIONS[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_NORMAL) readonly buffer normals { 

    float normal[];

} NORMALS[MAX_BINDLESS_RESOURCE_SIZE];


layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_TANGENT) readonly buffer tangents { 

    float tangent[];

} TANGENTS[MAX_BINDLESS_RESOURCE_SIZE];


layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_TEXCOORD) readonly buffer texCoords { 

    float texCoord[];

} TEXCOORDS[MAX_BINDLESS_RESOURCE_SIZE];


layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_COLOR) readonly buffer colors { 

    float color[];

} COLORS[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_BONE_INDEX) readonly buffer boneIndexs { 

    int boneIndex[];

} BONEINDEXS[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_BONE_WEIGHT) readonly buffer boneWeights { 

    float boneWeight[];

} BONEWEIGHTS[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_ANIMATION) readonly buffer animations { 

    mat4 matrix[];

} ANIMATIONS[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_INDEX) readonly buffer indices { 

    uint index[];

} INDICES[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_SAMPLER) uniform sampler SAMPLER[];
layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_TEXTURE_1D) uniform texture1D TEXTURES_1D[];
layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_TEXTURE_1D_ARRAY) uniform texture1DArray TEXTURES_1D_ARRAY[];
layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_TEXTURE_2D) uniform texture2D TEXTURES_2D[];
layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_TEXTURE_2D_ARRAY) uniform texture2DArray TEXTURES_2D_ARRAY[];
layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_TEXTURE_CUBE) uniform textureCube TEXTURES_CUBE[];
layout(set = 0, binding = PER_FRAME_BINDING_BINDLESS_TEXTURE_3D) uniform texture3D TEXTURES_3D[];

//顶点数据////////////////////////////////////////////////////////////////////////////

mat4 FetchModel(in uint objectID)
{
    return OBJECTS.slot[objectID].model;
}

mat4 FetchPrevModel(in uint objectID)
{
    return OBJECTS.slot[objectID].prevModel;
}

uint FetchIndex(in uint objectID, in uint offset)
{
    uint indexID = OBJECTS.slot[objectID].indexID;
    uint index = INDICES[indexID].index[offset];

    return index;
}

Vertex FetchVertex(in uint vertexID, in uint index)
{
    Vertex vert;
    vert.pos = vec4(0.0f);
    vert.normal = vec3(0.0f);
    vert.tangent = vec4(0.0f);
    vert.color = vec3(0.0f);
    vert.texCoord = vec2(0.0f);

    if(vertexID == 0) return vert;
    VertexStream vertexStream = VERTICES.slot[vertexID];

    if(vertexStream.positionID > 0)
        vert.pos = vec4(POSITIONS[vertexStream.positionID].position[3 * index], 
                        POSITIONS[vertexStream.positionID].position[3 * index + 1], 
                        POSITIONS[vertexStream.positionID].position[3 * index + 2],
                        1.0f);

    if(vertexStream.normalID > 0)
        vert.normal = vec3( NORMALS[vertexStream.normalID].normal[3 * index], 
                            NORMALS[vertexStream.normalID].normal[3 * index + 1], 
                            NORMALS[vertexStream.normalID].normal[3 * index + 2]);  

    if(vertexStream.tangentID > 0)
        vert.tangent = vec4(TANGENTS[vertexStream.tangentID].tangent[4 * index], 
                            TANGENTS[vertexStream.tangentID].tangent[4 * index + 1], 
                            TANGENTS[vertexStream.tangentID].tangent[4 * index + 2],
                            TANGENTS[vertexStream.tangentID].tangent[4 * index + 3]);   

    if(vertexStream.colorID > 0)
        vert.color = vec3(  COLORS[vertexStream.colorID].color[3 * index], 
                            COLORS[vertexStream.colorID].color[3 * index + 1],
                            COLORS[vertexStream.colorID].color[3 * index + 2]);

    if(vertexStream.texCoordID > 0)       
        vert.texCoord = vec2(   TEXCOORDS[vertexStream.texCoordID].texCoord[2 * index], 
                                TEXCOORDS[vertexStream.texCoordID].texCoord[2 * index + 1]);                 
    
    return vert;
}

vec4 FetchVertexPos(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec4(0.0f);

    uint positionID = VERTICES.slot[vertexID].positionID;
    if(positionID == 0) return vec4(0.0f);

    return vec4(POSITIONS[positionID].position[3 * index], 
                POSITIONS[positionID].position[3 * index + 1], 
                POSITIONS[positionID].position[3 * index + 2],
                1.0f);
}

vec3 FetchVertexNormal(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec3(0.0f, 0.0f, 1.0f);

    uint normalID = VERTICES.slot[vertexID].normalID;
    if(normalID == 0) return vec3(0.0f, 0.0f, 1.0f);

    return vec3(NORMALS[normalID].normal[3 * index], 
                NORMALS[normalID].normal[3 * index + 1], 
                NORMALS[normalID].normal[3 * index + 2]);   
}

vec4 FetchVertexTangent(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec4(0.0f);

    uint tangentID = VERTICES.slot[vertexID].tangentID;
    if(tangentID == 0) return vec4(0.0f);

    return vec4(TANGENTS[tangentID].tangent[4 * index], 
                TANGENTS[tangentID].tangent[4 * index + 1], 
                TANGENTS[tangentID].tangent[4 * index + 2],
                TANGENTS[tangentID].tangent[4 * index + 3]);   
}

vec2 FetchVertexTexCoord(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec2(0.0f);

    uint texCoordID = VERTICES.slot[vertexID].texCoordID;
    if(texCoordID == 0) return vec2(0.0f);

    return vec2(TEXCOORDS[texCoordID].texCoord[2 * index], 
                TEXCOORDS[texCoordID].texCoord[2 * index + 1]);
}

vec3 FetchVertexColor(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec3(1.0f);

    uint colorID = VERTICES.slot[vertexID].colorID;
    if(colorID == 0) return vec3(1.0f);

    return vec3(COLORS[colorID].color[3 * index], 
                COLORS[colorID].color[3 * index + 1],
                COLORS[colorID].color[3 * index + 2]);
}

uvec4 FetchVertexBoneIndex(in uint vertexID, in uint index)
{
    if(vertexID == 0) return uvec4(0);

    uint boneIndexID = VERTICES.slot[vertexID].boneIndexID;
    if(boneIndexID == 0) return uvec4(0);

    return uvec4(BONEINDEXS[boneIndexID].boneIndex[4 * index], 
                 BONEINDEXS[boneIndexID].boneIndex[4 * index + 1], 
                 BONEINDEXS[boneIndexID].boneIndex[4 * index + 2],
                 BONEINDEXS[boneIndexID].boneIndex[4 * index + 3]);   
}

vec4 FetchVertexBoneWeight(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec4(0);

    uint boneWeightID = VERTICES.slot[vertexID].boneWeightID;
    if(boneWeightID == 0) return vec4(0);

    return vec4(BONEWEIGHTS[boneWeightID].boneWeight[4 * index], 
                BONEWEIGHTS[boneWeightID].boneWeight[4 * index + 1], 
                BONEWEIGHTS[boneWeightID].boneWeight[4 * index + 2],
                BONEWEIGHTS[boneWeightID].boneWeight[4 * index + 3]);   
}

vec4 FetchPos(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexPos(vertexID, index);
}

vec3 FetchNormal(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexNormal(vertexID, index);
}

vec4 FetchTangent(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexTangent(vertexID, index);
}

vec3 FetchWorldNormal(in vec3 normal, in mat4 model)
{
    mat3 tbnModel = mat3(model);
    return normalize(tbnModel * normal);
}

vec4 FetchWorldTangent(in vec4 tangent, in mat4 model)
{
    mat3 tbnModel = mat3(model);
    return vec4(normalize(tbnModel * tangent.xyz), tangent.w);
}

vec2 FetchTexCoord(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexTexCoord(vertexID, index);
}

vec3 FetchColor(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexColor(vertexID, index);
}

uvec4 FetchBoneIndex(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexBoneIndex(vertexID, index);  
}

vec4 FetchBoneWeight(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexBoneWeight(vertexID, index); 
}

//光追顶点数据////////////////////////////////////////////////////////////////////////////

uvec3 FetchTriangleIndex(in uint objectID, in uint triangleID, in vec3 barycentrics)
{
	uint indexID = OBJECTS.slot[objectID].indexID;

	return uvec3(	INDICES[indexID].index[triangleID * 3],
					INDICES[indexID].index[triangleID * 3 + 1],
					INDICES[indexID].index[triangleID * 3 + 2]);
}

vec4 FetchTrianglePos(in uint objectID, in uvec3 triangleIndex, in vec3 barycentrics)
{
    return BarycentricsInterpolation(
		FetchPos(objectID, triangleIndex[0]),
		FetchPos(objectID, triangleIndex[1]),
		FetchPos(objectID, triangleIndex[2]),
		barycentrics);
}

vec3 FetchTriangleNormal(in uint objectID, in uvec3 triangleIndex, in vec3 barycentrics)
{
    return normalize(BarycentricsInterpolation(
		FetchNormal(objectID, triangleIndex[0]),
		FetchNormal(objectID, triangleIndex[1]),
		FetchNormal(objectID, triangleIndex[2]),
		barycentrics));
}

vec4 FetchTriangleTangent(in uint objectID, in uvec3 triangleIndex, in vec3 barycentrics)
{
    return BarycentricsInterpolation(
		FetchTangent(objectID, triangleIndex[0]),
		FetchTangent(objectID, triangleIndex[1]),
		FetchTangent(objectID, triangleIndex[2]),
		barycentrics);
}

vec2 FetchTriangleTexCoord(in uint objectID, in uvec3 triangleIndex, in vec3 barycentrics)
{
    return BarycentricsInterpolation(
		FetchTexCoord(objectID, triangleIndex[0]),
		FetchTexCoord(objectID, triangleIndex[1]),
		FetchTexCoord(objectID, triangleIndex[2]),
		barycentrics);
}

vec3 FetchTriangleColor(in uint objectID, in uvec3 triangleIndex, in vec3 barycentrics)
{
    return BarycentricsInterpolation(
		FetchColor(objectID, triangleIndex[0]),
		FetchColor(objectID, triangleIndex[1]),
		FetchColor(objectID, triangleIndex[2]),
		barycentrics);
}

//texture////////////////////////////////////////////////////////////////////////////

vec4 FetchTex2D(in uint slot, in vec2 coord, in vec2 offset, in vec2 scale) {
	return texture(sampler2D(TEXTURES_2D[slot], SAMPLER[1]), offset + coord * scale);   
}

vec4 FetchTex2D(in uint slot, in vec2 coord, in vec2 offset, in vec2 scale, in float lod) {
	return textureLod(sampler2D(TEXTURES_2D[slot], SAMPLER[1]), offset + coord * scale, lod);   
}

vec4 FetchTex2D(in uint slot, in vec2 coord) {
	return texture(sampler2D(TEXTURES_2D[slot], SAMPLER[1]), coord);   
}

vec4 FetchTex2D(in uint slot, in vec2 coord, in float lod) {
	return textureLod(sampler2D(TEXTURES_2D[slot], SAMPLER[1]), coord, lod);   
}

vec4 FetchTexCube(in uint slot, in vec3 vector) {
	return texture(samplerCube(TEXTURES_CUBE[slot], SAMPLER[1]), vector);   
}

vec4 FetchTexCube(in uint slot, in vec3 vector, in float lod) {
	return textureLod(samplerCube(TEXTURES_CUBE[slot], SAMPLER[1]), vector, lod);   
}

vec4 FetchTex3D(in uint slot, in vec3 vector) {
	return texture(sampler3D(TEXTURES_3D[slot], SAMPLER[1]), vector);   
}

vec4 FetchTex3D(in uint slot, in vec3 vector, in float lod) {
	return textureLod(sampler3D(TEXTURES_3D[slot], SAMPLER[1]), vector, lod);   
}

//material////////////////////////////////////////////////////////////////////////////

Material FetchMaterial(in uint objectID) {
	return MATERIALS.slot[OBJECTS.slot[objectID].materialID]; 
}

int FetchMaterialInt(in Material material, in int index) {
	return material.ints[index]; 
}

float FetchMaterialFloat(in Material material, in int index) {
	return material.floats[index]; 
}

vec4 FetchMaterialColor(in Material material, in int index) {
	return material.colors[index]; 
}

vec4 FetchMaterialTex2D(in Material material, in int index, in vec2 coord) {
	return FetchTex2D(material.textureSlots2D[index], coord, material.textureOffset, material.textureScale);
}

vec4 FetchMaterialTex2D(in Material material, in int index, in vec2 coord, in float lod) {
	return FetchTex2D(material.textureSlots2D[index], coord, material.textureOffset, material.textureScale, lod);
}

vec4 FetchMaterialTexCube(in Material material, in int index, in vec3 vector) {
	return FetchTexCube(material.textureSlotsCube[index], vector);
}

vec4 FetchMaterialTexCube(in Material material, in int index, in vec3 vector, in float lod) {
	return FetchTexCube(material.textureSlotsCube[index], vector, lod);
}

vec4 FetchMaterialTex3D(in Material material, in int index, in vec3 vector) {
	return FetchTex3D(material.textureSlots3D[index], vector);
}

vec4 FetchMaterialTex3D(in Material material, in int index, in vec3 vector, in float lod) {
	return FetchTex3D(material.textureSlots3D[index], vector, lod);
}

vec4 FetchBaseColor(in Material material){
    return material.baseColor;  
}

float FetchRoughness(in Material material, in vec2 coord){
    if(material.textureArm > 0)        
    {
        vec3 arm = FetchTex2D(material.textureArm, coord, material.textureOffset, material.textureScale).xyz;
        arm = pow(arm, vec3(1.0/2.2));          //gamma矫正

        return arm.y;
    }
    else return clamp(material.roughness, 0.00001, 0.99999); 
}

float FetchMetallic(in Material material, in vec2 coord){
    if(material.textureArm > 0)        
    {
        vec3 arm = FetchTex2D(material.textureArm, coord, material.textureOffset, material.textureScale).xyz;
        arm = pow(arm, vec3(1.0/2.2));          //gamma矫正

        return arm.z;
    }
    else return clamp(material.metallic, 0.00001, 0.99999);   
}

float FetchAO(in Material material, in vec2 coord){
    if(material.textureArm > 0)        
    {
        vec3 arm = FetchTex2D(material.textureArm, coord, material.textureOffset, material.textureScale).xyz;
        arm = pow(arm, vec3(1.0/2.2));          //gamma矫正

        return arm.x;
    }
    else return 1.0f;   
}

vec3 FetchEmission(in Material material){
    return material.emission.xyz * material.emission.w; 
}

float FetchEmissionIntencity(in Material material){
    return material.emission.w; 
}

vec4 FetchDiffuse(in Material material, in vec2 coord) {
    if(material.textureDiffuse > 0)    
    {
        vec4 diffuse = FetchTex2D(material.textureDiffuse, coord, material.textureOffset, material.textureScale);
        diffuse = pow(diffuse, vec4(1.0/2.2));          //gamma矫正
        diffuse = FetchBaseColor(material) * diffuse;         

        return diffuse;
    }
    else return FetchBaseColor(material);
}

vec3 FetchNormal(in Material material, in vec2 coord, in vec3 normal, in vec4 tangent) {
	if(material.textureNormal > 0)     
    {
        //计算每像素的tbn矩阵可以避免在vert shader输出上的额外两个vec3的插值，其实还会更快！
        float fSign = tangent.w < 0 ? -1 : 1;        
        highp vec3 n = normalize(normal);
        highp vec3 t = normalize(tangent.xyz);       
        highp vec3 b = -fSign * normalize(cross(n, t)); //排列组合试出来的 nm的 为什么
        t = fSign * normalize(cross(n, t));

        highp mat3 TBN = mat3(t, b, n);

        vec3 texNormal = FetchTex2D(material.textureNormal, coord, material.textureOffset, material.textureScale).xyz;
        vec3 outNormal = normalize(texNormal * 2.0 - 1.0);  
        outNormal = normalize(TBN * outNormal);

        return outNormal;
    }

    else return normal;
}

vec4 FetchArm(in Material material, in vec2 coord) {
	if(material.textureArm > 0)        return FetchTex2D(material.textureArm, coord, material.textureOffset, material.textureScale);
    else return vec4(1.0f, 1.0f, 0.0f, 0.0f);
}

vec4 FetchSpecular(in Material material, in vec2 coord) {
	if(material.textureSpecular > 0)        return FetchTex2D(material.textureSpecular, coord, material.textureOffset, material.textureScale);
    else return vec4(0.0f, 0.0f, 0.0f, 0.0f);
}

//默认纹理////////////////////////////////////////////////////////////////////////////

int SkyboxTexureSize() {
    Material material = MATERIALS.slot[GLOBAL_SETTING.skyboxMaterialID]; 

    return textureSize(TEXTURES_CUBE[material.textureSlotsCube[0]], 0).s;
}

vec3 FetchSkyLight(vec3 sampleVec, float lod) { 
    Material material = MATERIALS.slot[GLOBAL_SETTING.skyboxMaterialID]; 

    vec3 sky        = FetchMaterialTexCube(material, 0, sampleVec, lod).xyz;
    float intencity = FetchMaterialFloat(material, 0);
    // float intencity = 1.0f;

    vec3 skyLight   = pow(sky, vec3(1.0/2.2)) * intencity;
    return skyLight;
}

vec3 FetchSkyLight(vec3 sampleVec) { 
    return FetchSkyLight(sampleVec, 0.0f);
}

// vec2 FetchNoise2D(ivec2 texPos) {
//     return texelFetch(NOISE_SAMPLERS[0], texPos % 512, 0).rg;
// }

// vec3 FetchNoise3D(ivec2 texPos) {
//     return texelFetch(NOISE_SAMPLERS[1], texPos % 512, 0).xyz;
// }

float FetchDepth(in vec2 coord) {
	return texture(sampler2D(DEPTH[0], SAMPLER[1]), coord).r;
}

float FetchDepthTex(in ivec2 pixel) {
	return texelFetch(DEPTH[0], pixel, 0).r;
}

float FetchPrevDepth(in vec2 coord) {
	return texture(sampler2D(DEPTH[1], SAMPLER[1]), coord).r;
}

float FetchPrevDepthTex(in ivec2 pixel) {
	return texelFetch(DEPTH[1], pixel, 0).r;
}

vec2 FetchVelocity(in vec2 coord) {
	return texture(sampler2D(VELOCITY, SAMPLER[1]), coord).rg;
}

vec2 FetchVelocityTex(in ivec2 pixel) {
	return texelFetch(VELOCITY, pixel, 0).rg;
}

uint FetchObjectID(in vec2 coord) {
	return texture(usampler2D(OBJECT_ID[0], SAMPLER[1]), coord).r;
}

uint FetchObjectIDTex(in ivec2 pixel) {
	return texelFetch(OBJECT_ID[0], pixel, 0).r;
}

// vec3 FetchNormal(vec2 coord) {
// 	return texture(NORMAL_SAMPLER, coord).xyz;
// }

// float FetchRoughness(vec2 coord) {
// 	return texture(VELOCITY_SAMPLER, coord).a;  //目前粗糙度放在速度的A通道
// }

// float FetchMetallic(vec2 coord) {
// 	return texture(NORMAL_SAMPLER, coord).a;    //目前金属度放在法线的A通道
// }

// vec4 FetchDiffuse(vec2 coord) {
//     return texture(COLOR_DEFERRED_SAMPLER, coord).xyza;    
// }

#include "gizmo.glsl"
#include "ray_query.glsl"
#include "ddgi.glsl"
#include "coordinate.glsl"
#include "screen.glsl"
#include "brdf.glsl"
#include "light.glsl"
#include "reprojection.glsl"
#include "surface_cache.glsl"

#endif