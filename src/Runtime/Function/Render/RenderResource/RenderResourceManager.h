#pragma once

#include "Core/Math/Math.h"
#include "Core/Util/IndexAlloctor.h"
#include "Function/Global/Definations.h"
#include "Function/Render/RHI/RHIResource.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/RenderStructs.h"
#include "Buffer.h"
#include "Function/Render/RenderResource/Sampler.h"
#include "Function/Render/RenderResource/Texture.h"
#include "RenderStructs.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct BindlessResourceInfo
{
    ResourceType resourceType = RESOURCE_TYPE_NONE;

	RHIBufferRef buffer;
	RHITextureViewRef textureView;
	RHISamplerRef sampler;
	//TODO 光追加速结构

	uint64_t bufferOffset = 0;	// 仅buffer使用
	uint64_t bufferRange = 0;
};

struct VolumeLightTextures
{
    TextureRef diffuseTex;       // 需要缓存G-Buffer信息才能做烘焙？
    TextureRef normalTex;
    TextureRef emissionTex;
    TextureRef positionTex;
    
    TextureRef radianceTex;
    TextureRef irradianceTex;
    TextureRef depthTex;
};

enum BindlessSlot
{
    BINDLESS_SLOT_POSITION = 0,
    BINDLESS_SLOT_NORMAL,
    BINDLESS_SLOT_TANGENT,
    BINDLESS_SLOT_TEXCOORD,
    BINDLESS_SLOT_COLOR,
    BINDLESS_SLOT_BONE_INDEX,
    BINDLESS_SLOT_BONE_WEIGHT,
    BINDLESS_SLOT_ANIMATION,
    BINDLESS_SLOT_INDEX,

    BINDLESS_SLOT_SAMPLER,
    BINDLESS_SLOT_TEXTURE_1D,
    BINDLESS_SLOT_TEXTURE_1D_ARRAY,
    BINDLESS_SLOT_TEXTURE_2D,
    BINDLESS_SLOT_TEXTURE_2D_ARRAY,
    BINDLESS_SLOT_TEXTURE_CUBE,
    BINDLESS_SLOT_TEXTURE_3D,

    BINDLESS_SLOT_MAX_ENUM,     //
};

enum PerFrameBindingID
{
    // 刀7a（2026-10-20）：set0/set1重排连续化——shader侧镜像见 Asset/BuildIn/Shader/include/set0|set1/defines.glsl。
    // 死宏（LIGHT_CLUSTER_GRID/INDEX、DIRECTIONAL/POINT_SHADOW、VOLUME_LIGHT×2、MESH_CARD_READBACK、
    // SURFACE_CACHE、DEPTH、DEPTH_PYRAMID、VELOCITY、OBJECT_ID、GIZMO——已随刀3-5下放set2或废弃）本轮退役。
    // set0 = 视图集（原GLOBAL_SETTING=0/CAMERA=2，TLAS迁set1后b1空位，本轮CAMERA收紧为1）
    SET0_BINDING_GLOBAL_SETTING = 0,
    SET0_BINDING_CAMERA,

    // set1 = bindless资源池集（原沿用set0旧号稀疏1,3,4,5,12-15,21,23,25-40，本轮重排连续0-25）
    // 分组：数据表(0-7) / 静态资源(8-9) / 顶点流bindless池(10-18) / 采样器与纹理池(19-25)
    SET1_BINDING_TLAS = 0,
    SET1_BINDING_OBJECT,
    SET1_BINDING_MATERIAL,
    SET1_BINDING_LIGHT,
    SET1_BINDING_VERTEX,
    SET1_BINDING_MESH_CARD,
    SET1_BINDING_MESH_CLUSTER,
    SET1_BINDING_MESH_CLUSTER_GROUP,
    SET1_BINDING_SKYBOX_IBL,
    SET1_BINDING_STBN,
    SET1_BINDING_BINDLESS_POSITION,
    SET1_BINDING_BINDLESS_NORMAL,
    SET1_BINDING_BINDLESS_TANGENT,
    SET1_BINDING_BINDLESS_TEXCOORD,
    SET1_BINDING_BINDLESS_COLOR,
    SET1_BINDING_BINDLESS_BONE_INDEX,
    SET1_BINDING_BINDLESS_BONE_WEIGHT,
    SET1_BINDING_BINDLESS_ANIMATION,
    SET1_BINDING_BINDLESS_INDEX,
    SET1_BINDING_BINDLESS_SAMPLER,
    SET1_BINDING_BINDLESS_TEXTURE_1D,
    SET1_BINDING_BINDLESS_TEXTURE_1D_ARRAY,
    SET1_BINDING_BINDLESS_TEXTURE_2D,
    SET1_BINDING_BINDLESS_TEXTURE_2D_ARRAY,
    SET1_BINDING_BINDLESS_TEXTURE_CUBE,
    SET1_BINDING_BINDLESS_TEXTURE_3D,

    SET1_BINDING_MAX_ENUM,//
};

// 刀7b-1（2026-10-20）：set2=pass IO集全局统一槽位表（v3契约对账版，0-97共98槽）。
// shader侧镜像=Asset/BuildIn/Shader/include/set2/defines.glsl；施工图/契约表=同目录PLANNING.md。
// v3变更：拆POSTPROCESS_OUT(40,rgba8)、拆ReSTIR为DI/GI两域(47-50/51-54,block异构)、
// FINAL_COLOR(28)="Noisy HDR Color"四号归一。
// 数组槽count：2=HISTORY_DEPTH/PYRAMID/OBJECT_ID、4=DIRECTIONAL_SHADOW、5=SURFACE_CACHE、
// 6=CLUSTER_DRAW_INFOS_READ/IN_TEX_ARRAY/OUT_TEX_ARRAY、8=POINT_SHADOW、100=VOLUME_LIGHT×2、
// 1024=剔除域写侧、2=IN_COLOR/SVGF_CURRENT_COLOR_VARIANCE，其余=1
enum Set2BindingID
{
    // 共享域 0-13（跨pass语义槽）
    SET2_BINDING_LIGHT_CLUSTER_GRID = 0,
    SET2_BINDING_LIGHT_CLUSTER_INDEX,               // =1
    SET2_BINDING_HISTORY_DEPTH,                      // =2 x2
    SET2_BINDING_HISTORY_DEPTH_PYRAMID,              // =3 x2
    SET2_BINDING_HISTORY_VELOCITY,                   // =4
    SET2_BINDING_HISTORY_OBJECT_ID,                  // =5 x2
    SET2_BINDING_DIRECTIONAL_SHADOW,                 // =6 x4
    SET2_BINDING_POINT_SHADOW,                       // =7 x8 TEXTURE_CUBE
    SET2_BINDING_SURFACE_CACHE,                      // =8 x5
    SET2_BINDING_VOLUME_LIGHT_IRRADIANCE,            // =9 xMAX_VOLUME_LIGHT_COUNT
    SET2_BINDING_VOLUME_LIGHT_DEPTH,                 // =10 xMAX_VOLUME_LIGHT_COUNT
    SET2_BINDING_MESH_CARD_READBACK,                 // =11 RW_BUFFER
    SET2_BINDING_GIZMO_DRAW_DATA,                    // =12 RW_BUFFER
    SET2_BINDING_CLUSTER_DRAW_INFOS_READ,            // =13 x6 读侧

    // mesh剔除域 14-19（GPUCulling写侧，x1024）
    SET2_BINDING_CLUSTER_DRAW_INFOS_WRITE,           // =14
    SET2_BINDING_INDIRECT_MESH_DRAW_DATAS,           // =15
    SET2_BINDING_INDIRECT_MESH_DRAW_COMMANDS,        // =16
    SET2_BINDING_INDIRECT_CLUSTER_DRAW_DATAS,        // =17
    SET2_BINDING_INDIRECT_CLUSTER_DRAW_COMMANDS,     // =18
    SET2_BINDING_INDIRECT_CLUSTER_GROUP_DATAS,       // =19

    // G-Buffer域 20-30（DI侧读法随7b-3统一texelFetch）
    SET2_BINDING_GBUFFER_DIFFUSE_METALLIC,           // =20
    SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS,           // =21
    SET2_BINDING_GBUFFER_EMISSION,                   // =22
    SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS,      // =23
    SET2_BINDING_GBUFFER_GEOMETRIC_NORMAL_AO,        // =24
    SET2_BINDING_GBUFFER_IOR,                        // =25
    SET2_BINDING_DIRECTIONAL_DEPTH_HISTORY,          // =26 已退役（DDH特性2026-10-22删除，槽号保留防错位，勿删勿用）
    SET2_BINDING_REPROJECTION_RESULT,                // =27
    SET2_BINDING_FINAL_COLOR,                        // =28 RW rgba16f（"Noisy HDR Color"归一）
    SET2_BINDING_DI_DIFFUSE_COLOR,                   // =29 RW
    SET2_BINDING_DI_SPECULAR_COLOR,                  // =30 RW

    // DDGI域 31-37（全RW_TEXTURE）
    SET2_BINDING_DDGI_TRACE_ALBEDO_METALLIC,         // =31 rgba8
    SET2_BINDING_DDGI_TRACE_NORMAL_ROUGHNESS,        // =32 rgba8_snorm
    SET2_BINDING_DDGI_TRACE_EMISSION,                // =33 rgba16f
    SET2_BINDING_DDGI_TRACE_POSITION,                // =34 rgba16f
    SET2_BINDING_DDGI_RADIANCE,                      // =35 rgba16f
    SET2_BINDING_DDGI_IRRADIANCE,                    // =36 rgba16f
    SET2_BINDING_DDGI_DEPTH,                         // =37 rg16f

    // 后处理通用域 38-44（通用IO=角色槽：槽共享、资源按边逐pass换绑）
    SET2_BINDING_IN_COLOR,                           // =38 x2（count=1用户声明数组取[0]）
    SET2_BINDING_OUT_COLOR,                          // =39 RW rgba16f
    SET2_BINDING_POSTPROCESS_OUT,                    // =40 RW rgba8（v3拆1：LDR输出）
    SET2_BINDING_EXPOSURE_DATA,                      // =41 RW_BUFFER
    SET2_BINDING_IN_TEX_ARRAY,                       // =42 x6
    SET2_BINDING_OUT_TEX_ARRAY,                      // =43 x6 RW rg32f
    SET2_BINDING_BRDF_LUT,                           // =44

    // 金字塔/深度域 45-46
    SET2_BINDING_DEPTH_PYRAMID_LAST_MIP,             // =45
    SET2_BINDING_DEPTH_PYRAMID_CURRENT_MIP,          // =46 RW r32f

    // ReSTIR DI域 47-50（v3拆2：block=DIReservoir/RestirDISetting）
    SET2_BINDING_RESTIR_DI_SETTING,                  // =47 RW_BUFFER
    SET2_BINDING_RESTIR_DI_RESERVOIRS,               // =48 RW_BUFFER
    SET2_BINDING_RESTIR_DI_PREV_RESERVOIRS,          // =49 RW_BUFFER
    SET2_BINDING_RESTIR_DI_RESULT_RESERVOIRS,        // =50 RW_BUFFER

    // ReSTIR GI域 51-56（v3拆2：block=GIReservoir/RestirGISetting）
    SET2_BINDING_RESTIR_GI_SETTING,                  // =51 RW_BUFFER
    SET2_BINDING_RESTIR_GI_RESERVOIRS,               // =52 RW_BUFFER
    SET2_BINDING_RESTIR_GI_PREV_RESERVOIRS,          // =53 RW_BUFFER
    SET2_BINDING_RESTIR_GI_RESULT_RESERVOIRS,        // =54 RW_BUFFER
    SET2_BINDING_GI_DIFFUSE_COLOR,                   // =55 RW
    SET2_BINDING_GI_SPECULAR_COLOR,                  // =56 RW

    // SSSR域 57-63
    SET2_BINDING_SSSR_HIT_RESULT,                    // =57 RW
    SET2_BINDING_SSSR_HIT_COLOR,                     // =58 RW
    SET2_BINDING_SSSR_PDF,                           // =59 RW
    SET2_BINDING_SSSR_RESOLVE,                       // =60 RW
    SET2_BINDING_SSSR_HISTORY,                       // =61 RW
    SET2_BINDING_SSSR_IN_COLOR_PYRAMID,              // =62
    SET2_BINDING_SSSR_REPROJECTION_RESULT,           // =63

    // SVGF域 64-68（IN/OUT并入通用38/39，G-Buffer读并入20/21）
    SET2_BINDING_SVGF_HISTORY_GBUFFER_NORMAL,        // =64
    SET2_BINDING_SVGF_HISTORY_MOMENTS,               // =65
    SET2_BINDING_SVGF_CURRENT_MOMENTS,               // =66 RW rgba32f
    SET2_BINDING_SVGF_HISTORY_COLOR_VARIANCE,        // =67
    SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE,        // =68 x2 RW rgba16f

    // SurfaceCache域 69-74
    SET2_BINDING_SC_CACHE_DIFFUSE,                   // =69
    SET2_BINDING_SC_CACHE_NORMAL,                    // =70
    SET2_BINDING_SC_CACHE_EMISSION,                  // =71
    SET2_BINDING_SC_CACHE_DEPTH,                     // =72
    SET2_BINDING_SC_CACHE_LIGHTING,                  // =73 RW rgba16f
    SET2_BINDING_SC_DIRECT_LIGHTING_DISPATCH,        // =74 RW_BUFFER

    // NRD wrapper域 75-78（引擎侧wrapper；NRD SDK内部布局不在此列）
    SET2_BINDING_NRD_VIEW_Z,                         // =75 RW r32f
    SET2_BINDING_NRD_NORMAL,                         // =76 RW rgba8_snorm
    SET2_BINDING_NRD_OUT_DIFFUSE,                    // =77
    SET2_BINDING_NRD_OUT_SPECULAR,                   // =78

    // pass私有 79-97
    SET2_BINDING_TAA_CURRENT_COLOR,                  // =79
    SET2_BINDING_TAA_HISTORY_COLOR,                  // =80
    SET2_BINDING_TAA_REPROJECTION_RESULT,            // =81
    SET2_BINDING_TAA_OUT_COLOR,                      // =82 RW rgba16f
    SET2_BINDING_TAA_HISTORY_OUT,                    // =83 RW rgba16f
    SET2_BINDING_REPROJECTION_CURRENT_NORMAL,        // =84
    SET2_BINDING_REPROJECTION_HISTORY_NORMAL,        // =85
    SET2_BINDING_REPROJECTION_RESULT_OUT,            // =86 RW rgba32f
    SET2_BINDING_FOG_VOXEL_CURRENT,                  // =87 RW rgba16f 3D
    SET2_BINDING_FOG_VOXEL_HISTORY,                  // =88 RW rgba16f 3D
    SET2_BINDING_FOG_VOXEL_INTEGRATED,               // =89 RW rgba16f 3D
    SET2_BINDING_FOG_VOXEL_OUT,                      // =90 RW rgba16f 2D
    SET2_BINDING_CLIPMAP_A,                          // =91 RW rgba16f 3D
    SET2_BINDING_CLIPMAP_B,                          // =92 RW_BUFFER
    SET2_BINDING_CLIPMAP_C,                          // =93 RW rgba16f 2D
    SET2_BINDING_RTB_A,                              // =94 RW rgba8
    SET2_BINDING_PT_A,                               // =95 RW rgba16f
    SET2_BINDING_PT_B,                               // =96 RW rgba32f
    SET2_BINDING_IBL_A,                              // =97 RW rgba16f

    // ReSTIR DI G-Buffer RW读槽 98-101（v4拆分裁决：storage读法保留）
    SET2_BINDING_RESTIR_DI_GBUFFER_DIFFUSE,        // =98 RW rgba8
    SET2_BINDING_RESTIR_DI_GBUFFER_NORMAL,          // =99 RW rgba8
    SET2_BINDING_RESTIR_DI_GBUFFER_EMISSION,        // =100 RW rgba8
    SET2_BINDING_RESTIR_DI_GBUFFER_SPECF0,          // =101 RW rgba8

    SET2_BINDING_MAX_ENUM,//
};

static uint32_t BindlessSlotToPerFrameBinding(BindlessSlot slot) { return slot + (uint32_t)SET1_BINDING_BINDLESS_POSITION; }


// FRenderResource是对Render线程的资源抽象，对应的FRHIResource是对RHI线程的资源抽象，有一个封装的关系
// 有相当多的子类继承
// FRenderResource也规定了一组简单的初始化和销毁用的RHI接口，对于RHI资源的操作会入队到对应线程执行（异步）
// 简单起见就不做了，只对RHI资源做必要的封装，RenderResource文件夹下的各个类作为上层渲染资源的抽象

// 主要用于bindless绑定分配等全局的资源分配和管理
class RenderResourceManager
{
public:
    RenderResourceManager() = default;
    ~RenderResourceManager() {};

    void Init();    


    void Destroy();

    uint32_t AllocateObjectID()                             { return perFrameResources[0].objectBuffer.Allocate(); }
    void ReleaseObjectID(uint32_t id)                       { perFrameResources[0].objectBuffer.Release(id); }

    uint32_t AllocateMaterialID()                           { return multiFrameResource.materialBuffer.Allocate(); }      
    void ReleaseMaterialID(uint32_t id)                     { multiFrameResource.materialBuffer.Release(id); }  
    
    uint32_t AllocatePointLightID()                         { return pointLightIDAlloctor.Allocate(); }      
    void ReleasePointLightID(uint32_t id)                   { pointLightIDAlloctor.Release(id); }  

    uint32_t AllocateVolumeLightID()                        { return volumeLightIDAlloctor.Allocate(); }      
    void ReleaseVolumeLightID(uint32_t id)                  { volumeLightIDAlloctor.Release(id); } 

    IndexRange AllocateMeshClusterID(uint32_t size)         { return multiFrameResource.meshClusterBuffer.Allocate(size); }      
    void ReleaseMeshClusterID(IndexRange range)             { multiFrameResource.meshClusterBuffer.Release(range); }  

    uint32_t AllocateMeshCardID()                           { return perFrameResources[0].meshCardBuffer.Allocate(6).begin; }      
    void ReleaseMeshCardID(uint32_t id)                     { perFrameResources[0].meshCardBuffer.Release({id, 6}); }  

    IndexRange AllocateMeshClusterGroupID(uint32_t size)    { return multiFrameResource.meshClusterGroupBuffer.Allocate(size); }      
    void ReleaseMeshClusterGroupID(IndexRange range)        { multiFrameResource.meshClusterGroupBuffer.Release(range); }  

    uint32_t AllocateVertexID()                             { return multiFrameResource.vertexBuffer.Allocate(); }      
    void ReleaseVertexID(uint32_t id)                       { multiFrameResource.vertexBuffer.Release(id); }  

    uint32_t AllocateBindlessID(const BindlessResourceInfo& resoruceInfo, BindlessSlot slot);
    void ReleaseBindlessID(uint32_t id, BindlessSlot slot);   
  
    RHITextureRef GetLightClusterGridTexture()              { return multiFrameResource.lightClusterGridTexture->texture; }
    RHITextureRef GetDirectionalShadowTexture(uint32_t id)  { return multiFrameResource.dirShadowTextures[id]->texture; }
    RHITextureRef GetPointShadowTexture(uint32_t id)        { return multiFrameResource.pointShadowTextures[id]->texture; }
    RHITextureRef GetPointShadowDepthTexture(uint32_t id)   { return multiFrameResource.pointShadowDepthTextures[id]->texture; }
    RHITextureRef GetIBLTexture(uint32_t id)                { return multiFrameResource.skyboxIBLTexuture[id]->texture; }
    RHITextureRef GetDepthTexture()                         { return multiFrameResource.depthTexture[0]->texture; }
    RHITextureRef GetPrevDepthTexture()                     { return multiFrameResource.depthTexture[1]->texture; }
    RHITextureRef GetDepthPyramidTexture(uint32_t id)       { return multiFrameResource.depthPyramidTexture[id]->texture; }
    RHITextureRef GetVelocityTexture()                      { return multiFrameResource.velocityTexture->texture; }
    RHITextureRef GetObjectIDTexture()                      { return multiFrameResource.objectIDTexture[0]->texture; }
    RHITextureRef GetPrevObjectIDTexture()                  { return multiFrameResource.objectIDTexture[1]->texture; }
    RHITextureRef GetNormalTexture()                        { return multiFrameResource.gbufferNormalTexture[0]->texture; }
    RHITextureRef GetPrevNormalTexture()                    { return multiFrameResource.gbufferNormalTexture[1]->texture; }
    RHITextureRef GetIORTexture()                           { return multiFrameResource.gbufferIORTexture->texture; }    // (η-1)/2，薄透明pass写
    RHITextureRef GetStableHDRColorTexture(uint32_t slot);
    TextureRef GetSurfaceCacheTexture(uint32_t id)          { return multiFrameResource.surfaceCacheTexture[id]; }

    void SetRenderGlobalSetting(const RenderGlobalSetting& globalSetting);
    void SetTLAS(const RHITopLevelAccelerationStructureRef& tlas);
    void SetCameraInfo(const CameraInfo& cameraInfo);
    void SetObjectInfo(const ObjectInfo& objectInfo, uint32_t objectID);
    void SetObjectInfos(const std::vector<ObjectInfo>& objectInfos, uint32_t baseObjectID);
    void SetDirectionalLightInfo(const DirectionalLightInfo& directionalLightInfo, uint32_t cascade);
    void SetPointLightInfo(const PointLightInfo& pointLightInfo, uint32_t pointLightID);
    void SetVolumeLightInfo(const VolumeLightInfo& volumeLightInfo, uint32_t volumeLightID);
    void SetVolumeLightTextures(const VolumeLightTextures& volumeLightTextures, uint32_t volumeLightID);
    void SetLightSetting(const LightSetting& lightSetting);
    void SetMaterialInfo(const MaterialInfo& materialInfo, uint32_t materialID);
    void SetMeshClusterInfo(const std::vector<MeshClusterInfo>& meshClusterInfo, uint32_t baseMeshClusterID);
    void SetMeshClusterGroupInfo(const std::vector<MeshClusterGroupInfo>& meshClusterGroupInfo, uint32_t baseMeshClusterGroupID);
    void SetMeshCardInfos(const std::vector<MeshCardInfo>& cards, uint32_t size);
    void SetVertexInfo(const VertexInfo& vertexInfo, uint32_t vertexID);
    void SetGizmoDataCommand(void* data, int size);
    void GetMeshCardReadback(MeshCardReadBack& readback);

    RHIShaderRef GetOrCreateRHIShader(const std::string& path, ShaderFrequency frequency, const std::string& entry = "main");
    RHIBufferRef GetLightClusterIndexBuffer();
    RHIBufferRef GetGizmoDataBuffer();
    RHIBufferRef GetCardUpdateBuffer()              { return multiFrameResource.cardUpdateBuffer.buffer; }
    RHIBufferRef GetCardReadbackBuffer()            { return multiFrameResource.cardReadbackBuffer.buffer; }
    RHIBufferRef GetSurfaceTileBuffer();

    RHIRootSignatureRef GetPerFrameRootSignature()  { return perFrameRootSignature; }
    RHIDescriptorSetRef GetPerFrameDescriptorSet();
    RHIRootSignatureRef GetBindlessRootSignature()   { return bindlessRootSignature; }      // 刀2b：set1资源池集签名
    RHIDescriptorSetRef GetPoolDescriptorSet();       
    RHIRootSignatureRef GetSet2RootSignature()       { return set2RootSignature; }         // 刀7b-2：set2全局统一RS（pass IO集全量93槽）

private:
    std::unordered_map<std::string, RHIShaderRef> shaderMap;    // 全局统一的着色器创建，假定文件路径唯一索引一个着色器

private:
    // 每帧都单独需要的资源（持续更新）
    struct PerFrameResource
    {
        RHIDescriptorSetRef descriptorSet;
        RHIDescriptorSetRef poolDescriptorSet;        // 刀2b：set1资源池集（数据表/bindless池/TLAS）

        // 每帧都完全重构的buffer
        // 上一帧还在渲染时下一帧也可能在录制了，因此对每一帧有单独的buffer

        ArrayBuffer<LightClusterIndex, LIGHT_CLUSTER_NUM * MAX_LIGHTS_PER_CLUSTER> lightClusterIndexBuffer;                     // ClusterLightingPass (Compute写入)
        // clusterDrawInfoBuffer已下放至MeshPassIndirectBuffers（per mesh pass自持，GPUCullingPass入图import）
    
        // 有固定槽位分配的buffer，且更新频率高
        Buffer<CameraInfo> cameraBuffer;                                    
        ArrayBuffer<ObjectInfo, MAX_PER_FRAME_OBJECT_SIZE> objectBuffer;
        ArrayBuffer<MeshCardInfo, MAX_PER_FRAME_OBJECT_SIZE * 6> meshCardBuffer;
        Buffer<LightInfo> lightBuffer;
        Buffer<GizmoDrawData> gizmoBuffer = Buffer<GizmoDrawData>(RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_INDIRECT_BUFFER);     // GizmoPass
    };
    std::array<PerFrameResource, FRAMES_IN_FLIGHT> perFrameResources;

    // TODO 哪些资源需要创建多份，做cpu数据传输和gpu计算的并行处理？
    struct MultiFrameResource
    {

        // 有固定槽位分配的buffer，且更新频率低
        Buffer<RenderGlobalSetting> globalSettingBuffer;                    
        ArrayBuffer<MeshClusterInfo, MAX_PER_FRAME_CLUSTER_SIZE> meshClusterBuffer;
        ArrayBuffer<MeshClusterGroupInfo, MAX_PER_FRAME_CLUSTER_SIZE> meshClusterGroupBuffer;
        ArrayBuffer<MaterialInfo, MAX_PER_FRAME_RESOURCE_SIZE> materialBuffer;
        ArrayBuffer<VertexInfo, MAX_PER_FRAME_RESOURCE_SIZE> vertexBuffer;

        Buffer<MeshCardReadBack> cardUpdateBuffer = Buffer<MeshCardReadBack>(RESOURCE_TYPE_RW_BUFFER); // 需要每帧更新，但是都用同一个
        Buffer<MeshCardReadBack> cardReadbackBuffer = Buffer<MeshCardReadBack>(RESOURCE_TYPE_RW_BUFFER, MEMORY_USAGE_GPU_TO_CPU); 
 
        TextureRef lightClusterGridTexture;                                 // 纹理不会有冲突
        std::array<TextureRef, DIRECTIONAL_SHADOW_CASCADE_LEVEL> dirShadowTextures;        
        std::array<TextureRef, MAX_POINT_SHADOW_COUNT> pointShadowTextures;   
        std::array<TextureRef, MAX_POINT_SHADOW_COUNT> pointShadowDepthTextures;   
        std::array<TextureRef, 2> skyboxIBLTexuture;    // diffuse, specular
        std::array<TextureRef, 2> depthTexture;         // 本帧，上一帧
        std::array<TextureRef, 2> depthPyramidTexture;  // MIN, MAX 
        TextureRef velocityTexture;
        std::array<TextureRef, 64> stbnTexture;
        std::array<TextureRef, 2> objectIDTexture;      // 本帧，上一帧
        std::array<TextureRef, 2> gbufferNormalTexture; // 本帧，上一帧：G-Buffer Normal的历史对
        TextureRef gbufferIORTexture;                   // G-Buffer IOR(R8)：仅薄透明pass写，每帧CLEAR兜0=IOR 1
        std::array<TextureRef, 2> stableHDRColorTexture;    // 最终颜色乒乓：TAA直写physical[parity]（无Copy），经语义访问器(0=本帧,1=上一帧)访问
        std::array<TextureRef, 5> surfaceCacheTexture;
        std::vector<SamplerRef> samplers; 
    };
    MultiFrameResource multiFrameResource;

    // 全部的资源索引分配器
    std::array<IndexAlloctor, BINDLESS_SLOT_MAX_ENUM> bindlessIDAlloctor;
    IndexAlloctor pointLightIDAlloctor;
    IndexAlloctor volumeLightIDAlloctor;

    // 逐帧资源的根签名
    RHIRootSignatureRef perFrameRootSignature;
    RHIRootSignatureRef bindlessRootSignature;       // 刀2b：set1资源池集（数据表+bindless池+SAMPLER+TLAS）
    RHIRootSignatureRef set2RootSignature;           // 刀7b-2：set2全局统一RS（pass IO集，7b-3起各pass拼装进自己的RS）

    // 图标纹理
    std::vector<TextureRef> icons;
    GlobalIconInfo iconInfo;

    void InitGlobalResources(); // 负责创建全局（帧间）的渲染资源
    void LoadIcons();           // 加载图标资源
};

void SetMeshClusterGroupInfo(const std::vector<MeshClusterGroupInfo>& meshClusterGroupInfo, uint32_t baseMeshClusterGroupID);  