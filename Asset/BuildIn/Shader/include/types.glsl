#ifndef TYPES_GLSL
#define TYPES_GLSL
#include "defines.glsl"
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
    // 有些东西他不止一个
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

    float ior;                // 折射率（薄透明层），G-Buffer IOR纹理(R8, (η-1)/2) 中转给透射；虹彩时兼作薄膜η2
    uint iridescence;         // 虹彩标志，G-Buffer 法线纹理.a 中转为 specF（光照端用烘焙F0跳过Schlick）
    float index2;             // 虹彩基底折射率η3（G-Buffer写入端烘焙时消费，不进G-Buffer）
    float dinc;               // 虹彩膜厚标度（OPD=dinc·cosθ2，2η2已折叠，Range约0-6，非物理nm）
};

struct LightClusterIndex
{
    uint lightID;             
};

struct LightStructGPU
{
    vec4 pos_range;          // .xyz=位置(方向光段=预留) .w=range(衰减远端)
    vec4 color_intencity;    // .rgb=辐射度颜色 .w=强度
    vec4 dir_tan;            // .xyz=主方向(方向光=太阳向;聚光=锥轴) .w=tanHalfAngle(聚光,余预留)
    vec4 flags_fog;          // .x=fogScattering .y=floatBitsToUint位域{bit0-7类型,bit8=castShadow} .zw=预留(NRD/面光逃生门)
};

#define LIGHT_TYPE_DIRECTIONAL 0u
#define LIGHT_TYPE_POINT      1u
#define LIGHT_TYPE_SPOT       2u

uint LightType(LightStructGPU l)       { return floatBitsToUint(l.flags_fog.y) & 0xFFu; }
bool LightCastShadow(LightStructGPU l) { return (floatBitsToUint(l.flags_fog.y) & 0x100u) != 0u; }

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
    uint pointLightCnt;

    uint globalIndexOffset;                          
    uint _padding[3];

    uint pointLightIDs[MAX_POINT_LIGHT_COUNT];

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

struct SurfaceCacheData     // SC fetch的数据载荷（FetchSurfaceCacheData出参，不进任何block）
{
    vec3 albedo;      // 原始反照率（cache存raw，未预乘metallic）
    vec3 normal;
    vec4 emission;
    float roughness;
    float metallic;
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

struct IndirectSetting
{
    uint processSize;
    uint pipelineStateSize;
    uint _padding0[2];

    uint drawSize;
    uint frustumCull;
    uint occlusionCull;
    uint _padding1;
};

struct IndirectMeshDrawInfo
{
    uint objectID;
    uint commandID;
};

struct IndirectClusterDrawInfo
{
    uint objectID;
    uint clusterID;
    uint commandID;
    uint _padding;
};

struct IndirectClusterGroupDrawInfo
{
    uint objectID;
    uint clusterGroupID;
    uint commandID;
    uint _padding;
};

struct ClipmapLevel
{
    vec3 center;
    float _padding1;

    int level;
    int extent;
    float voxelSize;
    float _padding2;
};

struct ClipmapInfo
{
    int totalMipLevel;
    int _padding[3];

    ClipmapLevel levels[32];    // 32级再怎么也应该够了
};

struct ClipmapRegion
{
    ivec3 min;
    int _padding1;
    ivec3 extent;
    int _padding2;

    int mipLevel;
};

struct ExposureSetting
{
    float minLog2Luminance;             //最小亮度，对数
    float inverseLuminanceRange;        //亮度范围，对数倒数
    float luminanceRange;               //亮度范围，对数
    float numPixels;                    //总像素数
    float timeCoeff;                    //时间加权
    float _padding[3];
};


// ReSTIR DI 被采样光源信息（https://zhuanlan.zhihu.com/p/703950102）
struct LightSample
{
    uint lightID;               // 光源索引
};

struct DIReservoir
{
    LightSample sampl;

    float pHat;                 // 当前光源的重要性采样权重（targetPdf）
    float sumWeights;           // 已处理的(pHat / proposalPdf)权重和
    float w;                    // SIR PDF的倒数
    uint numStreamSamples;      // 已处理的采样总数 M
};

// ReSTIR GI 命中样本
struct HitSample
{
    vec3 hitPos;
    float _padding0;
    vec3 hitNormal;
    float _padding1;
    vec3 outRadiance;
    float _padding2;
};

struct GIReservoir
{
    HitSample sampl;

    float pHat;                 // 当前命中点的重要性采样权重（outRadiance的luminance）
    float sumWeights;           // 已处理的(pHat / proposalPdf)权重和
    float w;                    // SIR PDF的倒数
    uint numStreamSamples;      // 已处理的采样总数 M
};

struct RestirDISetting
{
    int initialSampleCount;

    int temporalSampleCountMultiplier;
    float temporalPosThreshold;

    float spatialPosThreshold;
    float spatialNormalThreshold;
    float spatialRadius;

    uint combineMode;
    uint visibilityReuse;
    uint temporalReuse;
    uint spatialReuse;
    uint unbias;
    uint bruteLighting;
    uint showWeight;
    uint showLightID;
};

struct RestirGISetting
{
    int initialSampleCount;

    int temporalSampleCountMultiplier;
    float temporalPosThreshold;

    float spatialPosThreshold;
    float spatialNormalThreshold;
    float spatialRadius;

    float surfaceCacheFactor;

    uint combineMode;
    uint visibilityReuse;
    uint temporalReuse;
    uint spatialReuse;
    uint unbias;
    uint enableSkybox;
    uint showRadiance;
};

struct SurfaceCacheLightingDispatch
{
    uint meshCardID;
    uint objectID;
    uvec2 tileOffset;
    uvec2 tileIndex;
};
#endif
