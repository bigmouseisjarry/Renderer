#pragma once

#include "Core/Math/BoundingBox.h"
#include "Core/Math/Math.h"
#include "Function/Global/Definations.h"
#include "Function/Render/RHI/RHIStructs.h"

#include <array>
#include <cstdint>
#include <memory>

// 声明各类在GPU和CPU间传递信息的数据结构
// 注意buffer的字节对齐问题

typedef struct GlobalIconInfo           //图标的bindless索引
{
    // uint32_t documentationIcon;
    // uint32_t worldIcon;
    // uint32_t entityIcon;
    uint32_t cameraIcon;
    uint32_t directionalLightIcon;
    uint32_t pointLightIcon;
    // uint32_t spotLightIcon;
    // uint32_t skyLightIcon;
    // uint32_t materialIcon;
    // uint32_t textIcon;
    // uint32_t decalIcon;
    // uint32_t postProcessVolumeIcon;
    // uint32_t exponentialHeightFogIcon;
    // uint32_t volumetricCloudIcon;
    // uint32_t atmosphericFogIcon;
    // uint32_t skyAtmosphereIcon;

} GlobalIconInfo;

typedef struct RenderGlobalSetting
{
    uint32_t skyboxMaterialID = 0;      //天空盒的材质ID
    uint32_t clusterInspectMode = 0;    //cluster渲染使用的模式
    uint32_t totalTicks = 0;            //运行帧数
    float totalTickTime = 0;            //运行时间
    float minFrameTime = 0;             //最小帧用时，毫秒单位
    float rayTracingOffset = 0.05f;     //由于虚拟几何体gbuffer和光追不一致导致需要添加偏移

    GlobalIconInfo icons;
    
} RenderGlobalSetting;

typedef struct CameraInfo 
{
    Mat4 view;
    Mat4 proj;
    Mat4 viewProj;
    Mat4 prevView;
    Mat4 prevProj;
    Mat4 prevViewProj;
    Mat4 invView;
    Mat4 invProj;

    Vec3 pos;           //世界坐标
    float _padding0;

    Vec3 front;
    float _padding1;
    Vec3 up;
    float _padding2;
    Vec3 right;
    float _padding3;

    float near;
    float far;
    float fov;
    float aspect;

    Frustum frustum;    //视锥，用于计算剔除

} CameraInfo;

typedef struct ObjectInfo
{
    Mat4 model;
    Mat4 prevModel;                 //前一帧的model矩阵
    Mat4 invModel;

    uint32_t animationID;           //动画索引
    uint32_t materialID;            //材质索引
    uint32_t vertexID;
    uint32_t indexID;  
    uint32_t meshCardID;            //card索引起始值
    uint32_t _padding[3];           
    
    BoundingSphere sphere;          //包围球，本地空间
    BoundingBox box;                //包围盒，本地空间
    Vec4 localScale;                //缩放值，物体可能带有从局部坐标到local的变换
    Vec4 modelScale;

    Vec4 debugData;                 //仅debug使用

} ObjectInfo;

typedef struct VertexInfo
{
    uint32_t positionID;
    uint32_t normalID;
    uint32_t tangentID;
    uint32_t texCoordID;
    uint32_t colorID;
    uint32_t boneIndexID;              
    uint32_t boneWeightID;
    uint32_t _padding;
    
} VertexInfo;

typedef struct MaterialInfo 
{
    //固定槽位///////////////////////////////
    float roughness;
    float metallic;
    float alphaClip;
    uint32_t useVertexColor;    //是否启用顶点颜色

    Vec4 diffuse;
    Vec4 emission;

    uint32_t textureDiffuse;
    uint32_t textureNormal;
    uint32_t textureArm;        //AO/Roughness/Metallic
    uint32_t textureSpecular;

    Vec2 textureOffset;
    Vec2 textureScale;

    //预留的通用槽位///////////////////////////////
    std::array<int32_t, 8> ints;
    std::array<float, 8> floats;
    std::array<Vec4, 8> colors;

    std::array<uint32_t, 8> texture2D;      //额外的纹理使用的下标，一共支持每材质8个2d纹理，
    std::array<uint32_t, 4> textureCube;    //4个cube纹理
    std::array<uint32_t, 4> texture3D;      //4个3d纹理

} MaterialInfo;

typedef struct LightClusterIndex
{
    uint32_t lightID;  
               
} LightClusterIndex;

typedef struct DirectionalLightInfo 
{
    Mat4 view;
    Mat4 proj;
    Mat4 viewProj;
    Vec3 pos;
    float _padding0;
    Vec3 dir;
    float depth;                //平行光源级联的划分深度，对齐Vec4

    Vec3 color;
    float intencity;            //前三维为颜色，最后一维强度
    
    float fogScattering;        //体积雾散射系数
    uint32_t castShadow;        //是否投射阴影，非零值有效
    float _padding1[2];

    Frustum frustum;            //视锥，用于计算剔除      
    BoundingSphere sphere;

} DirectionalLightInfo;

typedef struct PointLightInfo
{
    Mat4 view[6];
    Mat4 proj;
    Mat4 viewProj[6];
    Vec3 pos;
    float _padding0;
    Vec3 color;
    float intencity;
    float near;
    float far;                             
    float bias;
    float _padding1;
     
    float c1;                               //EVSM计算时使用的指数系数
    float c2;
    union { uint32_t _p0; bool enable; };   //是否启用
    uint32_t  shadowID;                     //索引点光源阴影的槽位，小于POINT_SHADOW_MAX_COUNT有效，每帧更新

    float fogScattering;                    //体积雾散射系数
    float _padding2[3];

    Frustum frustum[6];                     //视锥，用于计算剔除
    BoundingSphere sphere;                  //世界空间包围球，用于计算剔除

} PointLightInfo;

typedef struct DDGISetting
{
    Vec3  gridStartPosition;
    float _padding0;
    Vec3  gridStep;
    float _padding1;
    IVec3 probeCounts;
    float _padding2;

    float depthSharpness;
    float blendWeight;
    float normalBias;
    float energyPreservation;

    uint32_t irradianceTextureWidth;
    uint32_t irradianceTextureHeight;
    uint32_t depthTextureWidth;
    uint32_t depthTextureHeight;

    float maxProbeDistance;
    int   raysPerProbe;
    float _padding3[2];

    union { uint32_t _p0; bool enable; };   //cpp是一字节，glsl是四字节，手动对齐
    union { uint32_t _p1; bool visibilityTest; };
    union { uint32_t _p2; bool infiniteBounce; };
    union { uint32_t _p3; bool randomOrientation; };

    BoundingBox boundingBox;    //包围盒，世界空间

} DDGISetting;

typedef struct VolumeLightInfo
{
    DDGISetting setting;

} VolumeLightInfo;

typedef struct LightSetting 
{
    uint32_t directionalLightCnt = 0;
    uint32_t pointshadowedLightCnt = 0;
    uint32_t pointLightCnt = 0;
    uint32_t volumeLightCnt = 0;

    uint32_t globalIndexOffset = 0;                                     // 创建cluster light时使用，标记全局index缓冲的已分配偏移
    uint32_t _padding[3];

    uint32_t pointLightIDs[MAX_POINT_LIGHT_COUNT] = { 0 };          // 等待分簇光照处理的当前帧的全部点光源索引
    uint32_t pointShadowLightIDs[MAX_POINT_SHADOW_COUNT] = { 0 };   // 标记阴影点光源的lightID，方便剔除时索引
    
    uint32_t volumeLightIDs[MAX_VOLUME_LIGHT_COUNT] = { 0 };

} LightSetting;

#define DIR_LIGHT_OFFSET        (0)
#define POINT_LIGHT_OFFSET      (DIR_LIGHT_OFFSET + DIRECTIONAL_SHADOW_CASCADE_LEVEL * sizeof(DirectionalLightInfo))
#define VOLUME_LIGHT_OFFSET     (POINT_LIGHT_OFFSET + MAX_POINT_LIGHT_COUNT * sizeof(PointLightInfo))
#define LIGHT_SETTING_OFFSET    (VOLUME_LIGHT_OFFSET + MAX_VOLUME_LIGHT_COUNT * sizeof(VolumeLightInfo))
#define LIGHT_OFFSET_MAX        (LIGHT_SETTING_OFFSET + sizeof(LightSetting))

typedef struct LightInfo
{
    DirectionalLightInfo directionalLights[DIRECTIONAL_SHADOW_CASCADE_LEVEL];
    PointLightInfo pointLights[MAX_POINT_LIGHT_COUNT];
    VolumeLightInfo volumeLights[MAX_VOLUME_LIGHT_COUNT];

    LightSetting lightSetting;
} LightInfo;

typedef struct LightIndex
{
    uint32_t index;             //光源索引信息，索引LightInfo内的实际光源信息  

} LightIndex;

typedef struct GizmoBoxInfo 
{
    Vec3 center;
    float _padding0;
    Vec3 extent;
    float _padding1;
    Vec4 color;

} GizmoBoxInfo;

typedef struct GizmoSphereInfo 
{
    Vec3 center;
    float radious;
    Vec4 color;

} GizmoSphereInfo;

typedef struct GizmoLineInfo 
{
    Vec3 from;
    float _padding0;
    Vec3 to;
    float _padding1;
    Vec4 color;
} GizmoLineInfo;

struct GizmoBillboardInfo 
{
    Vec3 center;
    uint32_t textureID;
    Vec2 extent;
    Vec2 _padding;
    Vec4 color;
};

typedef struct GizmoDrawData    // 间接绘制Gizmo信息
{ 
    RHIIndexedIndirectCommand command[4];
    GizmoBoxInfo boxes[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoSphereInfo spheres[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoLineInfo lines[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoBillboardInfo worldBillboards[MAX_GIZMO_PRIMITIVE_COUNT];

} GizmoDrawData;

typedef struct MeshClusterInfo
{
    uint32_t vertexID;		// 到实际的顶点buffer索引     
    uint32_t indexID;
    uint32_t indexOffset;   // 在对应indexBuffer里的起始偏移（每个cluster的index数目保持一致）
    float lodError;

    BoundingSphere sphere;

} MeshClusterInfo;

typedef struct MeshClusterGroupInfo
{
    uint32_t clusterID[CLUSTER_GROUP_SIZE]; 

    uint32_t clusterSize;
    float parentLodError;  
    uint32_t mipLevel;
    float _padding;

    BoundingSphere sphere;
    
} MeshClusterGroupInfo;

typedef struct MeshCardInfo     // 单个Card的全部信息
{
	Vec3 viewPosition;          // 物体空间相机位置
	float _padding0;
	Vec3 viewExtent;            // 相机空间的包围盒尺寸
	float _padding1;
    Vec3 scale;                 // 物体空间缩放
    float _padding2;

	Mat4 view;
    Mat4 proj;
    Mat4 viewProj;
    Mat4 invView;
    Mat4 invProj;

	UVec2 atlasOffset = UVec2::Zero();      // 在surface cache中实际用到的尺寸范围，也是光栅化的范围
	UVec2 atlasExtent = UVec2::Zero();      // 内部不再有padding，尺寸也不对齐tile

    UVec2 sampleAtlasOffset = UVec2::Zero();    // 当存在尺寸变换时，光栅化和光照计算的任务会在新的范围计算
	UVec2 sampleAtlasExtent = UVec2::Zero();    // 但采样数据还是先保持旧范围（避免更新延迟导致短暂变黑），直到光照计算一次后再更新

} MeshCardInfo;

// 存储每个card最后在GPU端被使用的帧数，每帧回读来决定本帧的更新优先级
typedef std::array<int32_t, MAX_PER_FRAME_OBJECT_SIZE * 6> MeshCardReadBack;

// typedef struct AnimatedTransformInfo 
// {
//     Mat4 transform[200];

// } AnimatedTransformInfo;


// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct IndirectSetting
{
    uint32_t processSize = 0;               // 本轮需要处理的全部batch/cluster/cluster group数目
    uint32_t pipelineStateSize = 0;         // 本轮处理的不同管线状态的数目
    uint32_t _padding0[2];

    uint32_t drawSize = 0;                  // 通过culling实际需要绘制的数目，由GPU端计算写入
    uint32_t frustumCull = 0;               // 视锥剔除数目，由GPU端计算写入
    uint32_t occlusionCull = 0;             // 遮蔽剔除数目，由GPU端计算写入
    uint32_t _padding1;

} IndirectSetting;

// 下面的全部间接绘制都包括两个缓冲：
// 1. 存储提交给GPU剔除的，待绘制的全部几何的索引信息，以及一些统计信息
// 2. 存储经过GPU剔除处理后的，所有的间接绘制指令（以及cluster的对应索引）
// 此外object，cluster和cluster group各自还有独立的缓冲以存储信息；经剔除后最终需要绘制的cluster所需的索引信息也有一个全局缓冲

//Indirect Mesh Draw///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct IndirectMeshDrawInfo
{
    uint32_t objectID = 0;			        // 物体的实例索引
    uint32_t commandID = 0;				    // 使用的间接绘制指令的下标

} IndirectMeshDrawInfo;

typedef struct IndirectMeshDrawDatas        // 提交给GPU的待剔除信息
{
    IndirectSetting setting;

    IndirectMeshDrawInfo draws[MAX_PER_FRAME_OBJECT_SIZE];	    

} DrawClusterGroupDatas;

typedef struct IndirectMeshDrawCommands     // 提交给GPU的间接绘制指令信息，被剔除的资源会置instanceCount为零；
{                                           // 整个buffer再给mesh pass调用绘制
    RHIIndirectCommand commands[MAX_PER_FRAME_OBJECT_SIZE];

} IndirectMeshDrawCommands; 

//Indirect Cluster Draw///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct IndirectClusterDrawInfo       // 此处是提交给GPU进行剔除的数据，在cluster的剔除完成后还会重新构建一个全局缓冲GlobalClusterDrawInfos存实际绘制的信息
{
    uint32_t objectID = 0;                  // 每个cluster对应的物体的实例索引
    uint32_t clusterID = 0;                 // 对cluster的索引
    uint32_t commandID = 0;				    // 使用的间接绘制指令的下标，其实也是管线状态下标
    uint32_t _padding;

} IndirectClusterDrawInfo;

typedef struct MeshClusterDrawInfo
{
    uint32_t objectID = 0;                  // 每个cluster对应的物体的实例索引
    uint32_t clusterID = 0;                 // 对cluster的索引

} MeshClusterDrawInfo;

typedef struct IndirectClusterDrawDatas
{
    IndirectSetting setting;

    IndirectClusterDrawInfo draws[MAX_PER_FRAME_CLUSTER_SIZE];	    // 待剔除的全部cluster信息，可能来自于CPU端初始化，也可能来自于group剔除时在GPU端添加
                                                      
} IndirectClusterDrawDatas;

// typedef struct GlobalClusterDrawInfos
// {
//     MeshClusterDrawInfo clusters[MAX_PER_FRAME_CLUSTER_SIZE * MAX_SUPPORTED_MESH_PASS_COUNT];   // 剔除之后剩余的实际绘制使用的所有cluster，汇集到一个全局buffer里

// } GlobalClusterDrawInfos;

typedef struct IndirectClusterDrawCommands			           
{
    RHIIndirectCommand command[MAX_PER_PASS_PIPELINE_STATE_COUNT];	// 间接绘制指令，每个不同的管线状态都需要一个命令
    //uint32_t    vertexCount;		= CLUSTER_TRIANGLE_SIZE * 3
    //uint32_t    instanceCount;	= 0，经GPU计算后更新为实际绘制数目
    //uint32_t    firstVertex;		= 0
    //uint32_t    firstInstance;	= 该管线状态下的全局起始cluster信息索引（上面的GlobalClusterDrawInfos），已考虑到最大cluster数目来计算偏移
                                                        
} IndirectClusterDrawCommands;

//Indirect Cluster Group Draw///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct IndirectClusterGroupDrawInfo 
{
    uint32_t objectID = 0;                  // 每个cluster group对应的物体的实例索引
    uint32_t clusterGroupID = 0;            // 对cluster group的索引
    uint32_t commandID = 0;				    // 使用的间接绘制指令的下标，其实也是管线状态下标
    uint32_t _padding;

} IndirectClusterGroupDrawInfo;

typedef struct IndirectClusterGroupDrawDatas
{
    IndirectSetting setting;

    IndirectClusterGroupDrawInfo draws[MAX_PER_FRAME_CLUSTER_GROUP_SIZE];	    

} IndirectClusterGroupDrawDatas;

// cluster group经过剔除处理后得到的是cluster的绘制信息，直接填在上面的DrawClusterBuffer里










