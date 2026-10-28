#include "RenderResourceManager.h"
#include "Core/SurfaceCache/SurfaceCache.h"
#include "Core/Util/IndexAlloctor.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/RenderStructs.h"
#include "Function/Render/RenderResource/Texture.h"
#include "Sampler.h"

#include <cstdint>
#include <memory>

void RenderResourceManager::Init()
{
    for(auto& alloctor : bindlessIDAlloctor) alloctor = IndexAlloctor(MAX_BINDLESS_RESOURCE_SIZE);

    InitGlobalResources();
    LoadIcons();
}  

void RenderResourceManager::Destroy()
{
    
}

uint32_t RenderResourceManager::AllocateBindlessID(const BindlessResourceInfo& resoruceInfo, BindlessSlot slot)
{
    // 全局Bindless分配id
    uint32_t index = bindlessIDAlloctor[slot].Allocate();

    // 更新所有帧（e.g.当前的全部两个帧）的对应描述符绑定信息（刀2b：bindless池在set1池集）
    for(auto& resource : perFrameResources)
    {
        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = BindlessSlotToPerFrameBinding(slot),
            .index = index,
            .resourceType = resoruceInfo.resourceType,
            .buffer = resoruceInfo.buffer,
            .textureView = resoruceInfo.textureView,
            .sampler = resoruceInfo.sampler,
            .bufferOffset = resoruceInfo.bufferOffset,
            .bufferRange = resoruceInfo.bufferRange});
    }

    return index;
}

void RenderResourceManager::ReleaseBindlessID(uint32_t id, BindlessSlot slot)
{
    bindlessIDAlloctor[slot].Release(id);
}

void RenderResourceManager::SetRenderGlobalSetting(const RenderGlobalSetting& globalSetting)
{
    RenderGlobalSetting setting = globalSetting;
    setting.icons = iconInfo;
    multiFrameResource.globalSettingBuffer.SetData(setting);
}

void RenderResourceManager::SetTLAS(const RHITopLevelAccelerationStructureRef& tlas)
{
    for(auto& resource : perFrameResources)
    {
        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = SET1_BINDING_TLAS,
            .index = 0,
            .resourceType = RESOURCE_TYPE_RAY_TRACING,
            .tlas = tlas});
    }
}

void RenderResourceManager::SetCameraInfo(const CameraInfo& cameraInfo)
{
    perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].cameraBuffer.SetData(cameraInfo);
}

void RenderResourceManager::SetObjectInfo(const ObjectInfo& objectInfo, uint32_t objectID)
{
    perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].objectBuffer.SetData(objectInfo, objectID);
}

void RenderResourceManager::SetObjectInfos(const std::vector<ObjectInfo>& objectInfos, uint32_t baseObjectID)
{
    perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].objectBuffer.SetData(objectInfos, baseObjectID);
}

void RenderResourceManager::SetDirectionalLightInfo(const DirectionalLightInfo& directionalLightInfo, uint32_t cascade)
{
    perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].lightBuffer.SetData(
        &directionalLightInfo, 
        sizeof(DirectionalLightInfo), 
        DIR_LIGHT_OFFSET + cascade * sizeof(DirectionalLightInfo));
}

void RenderResourceManager::SetPointLightInfo(const PointLightInfo& pointLightInfo, uint32_t pointLightID)
{
    perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].lightBuffer.SetData(
        &pointLightInfo, 
        sizeof(PointLightInfo), 
        POINT_LIGHT_OFFSET + pointLightID * sizeof(PointLightInfo));
}

void RenderResourceManager::SetVolumeLightInfo(const VolumeLightInfo& volumeLightInfo, uint32_t volumeLightID)
{
    perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].lightBuffer.SetData(
        &volumeLightInfo, 
        sizeof(VolumeLightInfo), 
        VOLUME_LIGHT_OFFSET + volumeLightID * sizeof(VolumeLightInfo));    
}

void RenderResourceManager::SetVolumeLightTextures(const VolumeLightTextures& volumeLightTextures, uint32_t volumeLightID)
{
    // 刀5：烘焙纹理改经RDG入图（DDGI以"Volume Light Irradiance/Depth [id]"导入，消费者走set2 b25/b26 typed边）——set0描述符写入退役
}

void RenderResourceManager::SetLightSetting(const LightSetting& lightSetting)
{
    perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].lightBuffer.SetData(
        &lightSetting, 
        sizeof(LightSetting), 
        LIGHT_SETTING_OFFSET);
}

void RenderResourceManager::SetMaterialInfo(const MaterialInfo& materialInfo, uint32_t materialID)
{
    multiFrameResource.materialBuffer.SetData(materialInfo, materialID);
}

void RenderResourceManager::SetMeshClusterInfo(const std::vector<MeshClusterInfo>& meshClusterInfo, uint32_t baseMeshClusterID)
{
    multiFrameResource.meshClusterBuffer.SetData(meshClusterInfo, baseMeshClusterID);
}

void RenderResourceManager::SetMeshClusterGroupInfo(const std::vector<MeshClusterGroupInfo>& meshClusterGroupInfo, uint32_t baseMeshClusterGroupID)
{
    multiFrameResource.meshClusterGroupBuffer.SetData(meshClusterGroupInfo, baseMeshClusterGroupID);
}

void RenderResourceManager::SetMeshCardInfos(const std::vector<MeshCardInfo>& cards, uint32_t size)
{
    perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].meshCardBuffer.SetData(cards.data(), 0, size);
}

void RenderResourceManager::SetVertexInfo(const VertexInfo& vertexInfo, uint32_t vertexID)
{
    multiFrameResource.vertexBuffer.SetData(vertexInfo, vertexID);
}

void RenderResourceManager::SetGizmoDataCommand(void* data, int size)
{
    perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].gizmoBuffer.SetData(
        data,
        size * sizeof(RHIIndexedIndirectCommand),
        0);    
}

void RenderResourceManager::GetMeshCardReadback(MeshCardReadBack& readback)
{
    multiFrameResource.cardReadbackBuffer.GetData(&readback);
}

RHIShaderRef RenderResourceManager::GetOrCreateRHIShader(const std::string& path, ShaderFrequency frequency, const std::string& entry)
{
    auto iter = shaderMap.find(path);
    if(iter != shaderMap.end()) return iter->second;
    
    std::vector<uint8_t> code;
    EngineContext::File()->LoadBinary(path, code);

    // 去掉.spv扩展、保留一级父目录（如"restir_gi/lighting"）
    std::string debugName = path.substr(0, path.rfind('.'));
    size_t slash = debugName.rfind('/');
    if (slash != std::string::npos)
    {
        size_t slash2 = debugName.rfind('/', slash - 1);
        debugName = debugName.substr(slash2 + 1);    
    }

    RHIShaderInfo shaderInfo = {
        .entry = entry,
        .frequency = frequency,
        .code = code,
        .debugName = debugName
    };
    RHIShaderRef shader = EngineContext::RHI()->CreateShader(shaderInfo);

    shaderMap.emplace(path, shader);
    return shader;
}

RHIBufferRef RenderResourceManager::GetLightClusterIndexBuffer()
{ 
    return perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].lightClusterIndexBuffer.buffer;
} 

RHIBufferRef RenderResourceManager::GetGizmoDataBuffer()
{
    return perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].gizmoBuffer.buffer;
}

RHIDescriptorSetRef RenderResourceManager::GetPerFrameDescriptorSet()
{
    return perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].descriptorSet;
}

RHIDescriptorSetRef RenderResourceManager::GetPoolDescriptorSet()
{
    return perFrameResources[EngineContext::ThreadPool()->ThreadFrameIndex()].poolDescriptorSet;
}

RHITextureRef RenderResourceManager::GetStableHDRColorTexture(uint32_t slot)
{
    // 语义槽(0=本帧,1=上一帧) → 物理槽：本帧TAA直写physical[parity]，上帧残留physical[parity^1]
    return multiFrameResource.stableHDRColorTexture[EngineContext::CurrentFrameIndex() ^ slot]->texture;
}

void RenderResourceManager::InitGlobalResources()
{
    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

    // 刀2b：set0瘦身——数据表/bindless池/TLAS迁往set1资源池集（BindlessRS），set0只留视图常量+反馈+阴影+历史帧
    RHIRootSignatureInfo info = {};
    info.AddEntry({0, SET0_BINDING_GLOBAL_SETTING, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
        //.AddEntry({0, SET1_BINDING_TLAS, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RAY_TRACING})
        .AddEntry({0, SET0_BINDING_CAMERA, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_UNIFORM_BUFFER});     // 刀5：set0定型=视图集双槽（GLOBAL_SETTING+CAMERA）；volume light→set2 b25/26，readback→SC域b27


    perFrameRootSignature = EngineContext::RHI()->CreateRootSignature(info);
    for(auto& resource : perFrameResources) resource.descriptorSet = perFrameRootSignature->CreateDescriptorSet(0);

    // 全局采样器上提创建：bindless RS的静态采样器声明需要句柄（set1创建期播种，见下）；
    // 原建于multiFrameResource块内、晚于RS构建，声明即绑定后须先于此处存在
    multiFrameResource.samplers.push_back(std::make_shared<Sampler>(
        ADDRESS_MODE_CLAMP_TO_EDGE,
        FILTER_TYPE_LINEAR,
        MIPMAP_MODE_LINEAR,
        0.0f));

    multiFrameResource.samplers.push_back(std::make_shared<Sampler>(
        ADDRESS_MODE_REPEAT,
        FILTER_TYPE_LINEAR,
        MIPMAP_MODE_LINEAR,
        0.0f));

    multiFrameResource.samplers.push_back(std::make_shared<Sampler>(
        ADDRESS_MODE_CLAMP_TO_EDGE,
        FILTER_TYPE_LINEAR,
        MIPMAP_MODE_NEAREST,
        0.0f,
        SAMPLER_REDUCTION_MODE_MIN));

    multiFrameResource.samplers.push_back(std::make_shared<Sampler>(
        ADDRESS_MODE_CLAMP_TO_EDGE,
        FILTER_TYPE_LINEAR,
        MIPMAP_MODE_NEAREST,
        0.0f,
        SAMPLER_REDUCTION_MODE_MAX));

    multiFrameResource.samplers.push_back(std::make_shared<Sampler>(
        ADDRESS_MODE_REPEAT,
        FILTER_TYPE_NEAREST,
        MIPMAP_MODE_NEAREST,
        0.0f));

    multiFrameResource.samplers.push_back(std::make_shared<Sampler>(
        ADDRESS_MODE_CLAMP_TO_EDGE,
        FILTER_TYPE_NEAREST,
        MIPMAP_MODE_NEAREST,
        0.0f));

    // 刀2b：set1资源池集——数据表+顶点/纹理bindless池+SAMPLER+TLAS（槽位号沿用旧set0编号，仅set号0→1）
    RHIRootSignatureInfo bindlessInfo = {};
    bindlessInfo.AddEntry({1, SET1_BINDING_OBJECT, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_MESH_CLUSTER, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_MESH_CLUSTER_GROUP, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_SKYBOX_IBL, 2, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE_CUBE})
                .AddEntry({1, SET1_BINDING_STBN, 64, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
                .AddEntry({1, SET1_BINDING_MATERIAL, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_LIGHT, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_MESH_CARD, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_VERTEX, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_BINDLESS_POSITION, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_BINDLESS_NORMAL, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_BINDLESS_TANGENT, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_BINDLESS_TEXCOORD, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_BINDLESS_COLOR, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_BINDLESS_BONE_INDEX, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_BINDLESS_BONE_WEIGHT, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_BINDLESS_ANIMATION, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_BINDLESS_INDEX, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
                .AddEntry({1, SET1_BINDING_BINDLESS_SAMPLER, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_SAMPLER})
                .AddEntry({1, SET1_BINDING_BINDLESS_TEXTURE_1D, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
                .AddEntry({1, SET1_BINDING_BINDLESS_TEXTURE_1D_ARRAY, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
                .AddEntry({1, SET1_BINDING_BINDLESS_TEXTURE_2D, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
                .AddEntry({1, SET1_BINDING_BINDLESS_TEXTURE_2D_ARRAY, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
                .AddEntry({1, SET1_BINDING_BINDLESS_TEXTURE_CUBE, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
                .AddEntry({1, SET1_BINDING_BINDLESS_TEXTURE_3D, MAX_BINDLESS_RESOURCE_SIZE, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE});

    // 静态采样器声明即绑定：六个全局采样器在set1创建期自动播种（每帧槽的poolDescriptorSet同享），
    // 原UpdatePoolDescriptorSet里的手动sampler写入循环已删
    for (uint32_t i = 0; i < multiFrameResource.samplers.size(); i++)
        bindlessInfo.AddStaticSampler({1, SET1_BINDING_BINDLESS_SAMPLER, i, multiFrameResource.samplers[i]->sampler});

#if ENABLE_RAY_TRACING
    bindlessInfo.AddEntry({1, SET1_BINDING_TLAS, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RAY_TRACING});
#endif

    bindlessRootSignature = EngineContext::RHI()->CreateRootSignature(bindlessInfo);
    for(auto& resource : perFrameResources) resource.poolDescriptorSet = bindlessRootSignature->CreateDescriptorSet(1);

    // 刀7b-2：set2全局统一RS——pass IO集全量槽位（终态v2表0-92共93槽，规划见Asset/BuildIn/Shader/include/set2/PLANNING.md）。
    // 7b-3起各pass的Init收敛为三行拼装：AddEntry(set0)+AddEntry(set1)+AddEntry(set2本RS)，
    // RDG按边逐pass填槽（虚拟边不填槽铁律），槽号一律用Set2BindingID宏。
    // 本RS不常驻CreateDescriptorSet——set2描述符集由RDG描述符池按{RS info, set}逐pass池化分配
    RHIRootSignatureInfo set2Info = {};
    set2Info
        // 共享域 0-13（跨pass语义槽）
        .AddEntry({2, SET2_BINDING_LIGHT_CLUSTER_GRID,          1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})     // 原b10
        .AddEntry({2, SET2_BINDING_LIGHT_CLUSTER_INDEX,         1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})      // 原b11
        .AddEntry({2, SET2_BINDING_HISTORY_DEPTH,               2,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b12
        .AddEntry({2, SET2_BINDING_HISTORY_DEPTH_PYRAMID,       2,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b13
        .AddEntry({2, SET2_BINDING_HISTORY_VELOCITY,            1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b14
        .AddEntry({2, SET2_BINDING_HISTORY_OBJECT_ID,           2,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b15
        .AddEntry({2, SET2_BINDING_DIRECTIONAL_SHADOW,          4,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b22（=DIRECTIONAL_SHADOW_CASCADE_LEVEL）
        .AddEntry({2, SET2_BINDING_POINT_SHADOW,                8,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE_CUBE})  // 原b23（filtered0-3+depth4-7）
        .AddEntry({2, SET2_BINDING_SURFACE_CACHE,               5,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b24
        .AddEntry({2, SET2_BINDING_VOLUME_LIGHT_IRRADIANCE,     MAX_VOLUME_LIGHT_COUNT, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})   // 原b25
        .AddEntry({2, SET2_BINDING_VOLUME_LIGHT_DEPTH,          MAX_VOLUME_LIGHT_COUNT, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})   // 原b26
        .AddEntry({2, SET2_BINDING_MESH_CARD_READBACK,          1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原b27
        .AddEntry({2, SET2_BINDING_GIZMO_DRAW_DATA,             1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原gizmo b32
        .AddEntry({2, SET2_BINDING_CLUSTER_DRAW_INFOS_READ,     6,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_BUFFER})        // 原b21读侧（PointShadow一面一槽）
        // mesh剔除域 14-19（GPUCulling写侧，原MESH_PASS_BINDING 16-21写侧）
        .AddEntry({2, SET2_BINDING_CLUSTER_DRAW_INFOS_WRITE,    1024,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
        .AddEntry({2, SET2_BINDING_INDIRECT_MESH_DRAW_DATAS,    1024,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
        .AddEntry({2, SET2_BINDING_INDIRECT_MESH_DRAW_COMMANDS, 1024,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
        .AddEntry({2, SET2_BINDING_INDIRECT_CLUSTER_DRAW_DATAS, 1024,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
        .AddEntry({2, SET2_BINDING_INDIRECT_CLUSTER_DRAW_COMMANDS, 1024, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
        .AddEntry({2, SET2_BINDING_INDIRECT_CLUSTER_GROUP_DATAS, 1024, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
        // G-Buffer域 20-30（DI侧RW声明随7b-3统一TEXTURE+texelFetch）
        .AddEntry({2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC,      1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b0(ReSTIR)/b32(DL,NRD)
        .AddEntry({2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS,      1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b1/b33/b32(SVGF)
        .AddEntry({2, SET2_BINDING_GBUFFER_EMISSION,              1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b2/b34(DL)
        .AddEntry({2, SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS, 1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b7/b38(DL)/b39(NRD)/b3(SSSR)
        .AddEntry({2, SET2_BINDING_GBUFFER_GEOMETRIC_NORMAL_AO,   1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原DL b37
        .AddEntry({2, SET2_BINDING_GBUFFER_IOR,                   1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原DL b39
        .AddEntry({2, SET2_BINDING_REPROJECTION_RESULT,           1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原ReSTIR b3
        .AddEntry({2, SET2_BINDING_FINAL_COLOR,                   1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原b4（ReSTIR DI/GI共享）
        .AddEntry({2, SET2_BINDING_DI_DIFFUSE_COLOR,              1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原b5(DI)/b35(NRD viewZ)
        .AddEntry({2, SET2_BINDING_DI_SPECULAR_COLOR,             1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原b6(DI)/b36(NRD viewZ)/b33(NRD copySssr)
        // DDGI域 31-37（原DDGI b32-38）
        .AddEntry({2, SET2_BINDING_DDGI_TRACE_ALBEDO_METALLIC,    1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_DDGI_TRACE_NORMAL_ROUGHNESS,   1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_DDGI_TRACE_EMISSION,           1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_DDGI_TRACE_POSITION,           1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_DDGI_RADIANCE,                 1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_DDGI_IRRADIANCE,               1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_DDGI_DEPTH,                    1,   SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        // 后处理通用域 38-44（通用IO=角色槽：槽共享、资源按边逐pass换绑）
        .AddEntry({2, SET2_BINDING_IN_COLOR,                 2,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原b32（bloom双输入取最大；count=1用户取[0]）
        .AddEntry({2, SET2_BINDING_OUT_COLOR,                1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原b33（bloom/fxaa/SVGF/SSSR等HDR中间色，rgba16f契约）
        .AddEntry({2, SET2_BINDING_POSTPROCESS_OUT,          1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // v3拆1：post_process的LDR输出(rgba8)
        .AddEntry({2, SET2_BINDING_EXPOSURE_DATA,            1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原exposure b33/postprocess b34（三处同构）
        .AddEntry({2, SET2_BINDING_IN_TEX_ARRAY,             6,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原PointShadow b32
        .AddEntry({2, SET2_BINDING_OUT_TEX_ARRAY,            6,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原PointShadow b33（rg32f）
        .AddEntry({2, SET2_BINDING_BRDF_LUT,                 1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原SSSR b39
        // 金字塔/深度域 45-46
        .AddEntry({2, SET2_BINDING_DEPTH_PYRAMID_LAST_MIP,   1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原DepthPyramid b32
        .AddEntry({2, SET2_BINDING_DEPTH_PYRAMID_CURRENT_MIP, 1,    SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原DepthPyramid b33（r32f）
        // ReSTIR DI域 47-50（v3拆2：block=DIReservoir/RestirDISetting，物理buffer与GI分离）
        .AddEntry({2, SET2_BINDING_RESTIR_DI_SETTING,        1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原DI b32
        .AddEntry({2, SET2_BINDING_RESTIR_DI_RESERVOIRS,     1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原DI b33
        .AddEntry({2, SET2_BINDING_RESTIR_DI_PREV_RESERVOIRS, 1,    SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原DI b34
        .AddEntry({2, SET2_BINDING_RESTIR_DI_RESULT_RESERVOIRS, 1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原DI b35
        // ReSTIR GI域 51-56（v3拆2：block=GIReservoir/RestirGISetting）
        .AddEntry({2, SET2_BINDING_RESTIR_GI_SETTING,        1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原GI b32
        .AddEntry({2, SET2_BINDING_RESTIR_GI_RESERVOIRS,     1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原GI b33
        .AddEntry({2, SET2_BINDING_RESTIR_GI_PREV_RESERVOIRS, 1,    SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原GI b34
        .AddEntry({2, SET2_BINDING_RESTIR_GI_RESULT_RESERVOIRS, 1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原GI b35
        .AddEntry({2, SET2_BINDING_GI_DIFFUSE_COLOR,         1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原GI b5
        .AddEntry({2, SET2_BINDING_GI_SPECULAR_COLOR,        1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原GI b6
        // SSSR域 57-63（原SSSR b32-40）
        .AddEntry({2, SET2_BINDING_SSSR_HIT_RESULT,          1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_SSSR_HIT_COLOR,           1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_SSSR_PDF,                 1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_SSSR_RESOLVE,             1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_SSSR_HISTORY,             1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // v2修正：RW非TEXTURE
        .AddEntry({2, SET2_BINDING_SSSR_IN_COLOR_PYRAMID,    1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        .AddEntry({2, SET2_BINDING_SSSR_REPROJECTION_RESULT, 1,     SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        // SVGF域 64-68（IN/OUT并入通用38/39，G-Buffer读并入20/21）
        .AddEntry({2, SET2_BINDING_SVGF_HISTORY_GBUFFER_NORMAL,  1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        .AddEntry({2, SET2_BINDING_SVGF_HISTORY_MOMENTS,         1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        .AddEntry({2, SET2_BINDING_SVGF_CURRENT_MOMENTS,         1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_SVGF_HISTORY_COLOR_VARIANCE,  1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        .AddEntry({2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE,  2,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        // SurfaceCache域 69-74（原SurfaceCachePass b32-37）
        .AddEntry({2, SET2_BINDING_SC_CACHE_DIFFUSE,             1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        .AddEntry({2, SET2_BINDING_SC_CACHE_NORMAL,              1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        .AddEntry({2, SET2_BINDING_SC_CACHE_EMISSION,            1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        .AddEntry({2, SET2_BINDING_SC_CACHE_DEPTH,               1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        .AddEntry({2, SET2_BINDING_SC_CACHE_LIGHTING,            1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_SC_DIRECT_LIGHTING_DISPATCH,  1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})
        // NRD wrapper域 75-78（引擎侧wrapper；NRD SDK内部走NRDIntegration私有布局不在此列）
        .AddEntry({2, SET2_BINDING_NRD_VIEW_Z,                    1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_NRD_NORMAL,                    1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_NRD_OUT_DIFFUSE,               1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        .AddEntry({2, SET2_BINDING_NRD_OUT_SPECULAR,              1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})
        // pass私有 79-97
        .AddEntry({2, SET2_BINDING_TAA_CURRENT_COLOR,             1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原taa b32
        .AddEntry({2, SET2_BINDING_TAA_HISTORY_COLOR,             1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原taa b33
        .AddEntry({2, SET2_BINDING_TAA_REPROJECTION_RESULT,       1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原taa b34
        .AddEntry({2, SET2_BINDING_TAA_OUT_COLOR,                 1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原taa b35
        .AddEntry({2, SET2_BINDING_TAA_HISTORY_OUT,               1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原taa b36（kernel直写下帧history）
        .AddEntry({2, SET2_BINDING_REPROJECTION_CURRENT_NORMAL,   1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原reproj b32
        .AddEntry({2, SET2_BINDING_REPROJECTION_HISTORY_NORMAL,   1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})       // 原reproj b33
        .AddEntry({2, SET2_BINDING_REPROJECTION_RESULT_OUT,       1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原reproj b34
        .AddEntry({2, SET2_BINDING_FOG_VOXEL_CURRENT,             1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原fog b32（未积分体素,3D）
        .AddEntry({2, SET2_BINDING_FOG_VOXEL_HISTORY,             1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原fog b33（3D）
        .AddEntry({2, SET2_BINDING_FOG_VOXEL_INTEGRATED,          1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原fog b34（3D）
        .AddEntry({2, SET2_BINDING_FOG_VOXEL_OUT,                 1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原fog b35（2D）
        .AddEntry({2, SET2_BINDING_CLIPMAP_A,                     1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原clipmap b32（3D）
        .AddEntry({2, SET2_BINDING_CLIPMAP_B,                     1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})     // 原clipmap b33
        .AddEntry({2, SET2_BINDING_CLIPMAP_C,                     1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原clipmap visualize b34（2D）
        .AddEntry({2, SET2_BINDING_RTB_A,                         1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原RTBase b32（rgba8）
        .AddEntry({2, SET2_BINDING_PT_A,                          1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原PathTracing b32
        .AddEntry({2, SET2_BINDING_PT_B,                          1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})    // 原PathTracing b33（rgba32f）
        .AddEntry({2, SET2_BINDING_IBL_A,                         1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        // ReSTIR DI G-Buffer RW读槽 98-101（v4：storage读法保留，不融合TEXTURE域）
        .AddEntry({2, SET2_BINDING_RESTIR_DI_GBUFFER_DIFFUSE,   1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_RESTIR_DI_GBUFFER_NORMAL,     1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_RESTIR_DI_GBUFFER_EMISSION,   1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
        .AddEntry({2, SET2_BINDING_RESTIR_DI_GBUFFER_SPECF0,     1,  SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE});  // 原ReSTIR b0-2/b7

    set2RootSignature = EngineContext::RHI()->CreateRootSignature(set2Info);

    // 初始化全局资源，部分直接在声明时就默认创建了
    // multiFrameResource
    {
        // 六个全局采样器已上提至bindless RS构建前（静态采样器声明需要句柄）

        multiFrameResource.lightClusterGridTexture = std::make_shared<Texture>(
            TEXTURE_TYPE_3D,
            FORMAT_R32G32_UINT,
            Extent3D(LIGHT_CLUSTER_WIDTH, LIGHT_CLUSTER_HEIGHT, LIGHT_CLUSTER_DEPTH),
            1, 1);

        for(auto& texture : multiFrameResource.dirShadowTextures)
        {
            texture = std::make_shared<Texture>( 
                TEXTURE_TYPE_2D, 
                FORMAT_D32_SFLOAT,
                Extent3D(DIRECTIONAL_SHADOW_SIZE, DIRECTIONAL_SHADOW_SIZE, 1),
                1, 1);
        } 

        for(auto& texture : multiFrameResource.pointShadowTextures)
        {
            texture = std::make_shared<Texture>( 
                TEXTURE_TYPE_CUBE, 
                FORMAT_R32G32_SFLOAT,
                Extent3D(POINT_SHADOW_SIZE, POINT_SHADOW_SIZE, 1),
                6, 1);
        }

        for(auto& texture : multiFrameResource.pointShadowDepthTextures)
        {
            texture = std::make_shared<Texture>( 
                TEXTURE_TYPE_CUBE, 
                FORMAT_D32_SFLOAT,
                Extent3D(POINT_SHADOW_SIZE, POINT_SHADOW_SIZE, 1),
                6, 1);
        }

        multiFrameResource.skyboxIBLTexuture[0] = std::make_shared<Texture>( 
            TEXTURE_TYPE_CUBE, 
            EngineContext::Render()->GetHdrColorFormat(),
            Extent3D(DIFFUSE_IBL_SIZE, DIFFUSE_IBL_SIZE, 1),
            6, 1);

        multiFrameResource.skyboxIBLTexuture[1] = std::make_shared<Texture>( 
            TEXTURE_TYPE_CUBE, 
            EngineContext::Render()->GetHdrColorFormat(),
            Extent3D(SPECULAR_IBL_SIZE, SPECULAR_IBL_SIZE, 1),
            6, 0);  // mip为0自动生成全mip

        multiFrameResource.depthTexture[0] = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            EngineContext::Render()->GetDepthFormat(),
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 1);

        multiFrameResource.depthTexture[1] = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            EngineContext::Render()->GetDepthFormat(),
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 1);

        multiFrameResource.depthPyramidTexture[0] = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R32_SFLOAT,     
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 0);  // mip为0自动生成全mip

        multiFrameResource.depthPyramidTexture[1] = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R32_SFLOAT,
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 0);  // mip为0自动生成全mip

        multiFrameResource.velocityTexture = std::make_shared<Texture>(
            TEXTURE_TYPE_2D,
            FORMAT_R32G32_SFLOAT,
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 1, std::initializer_list<QueueType>{ QUEUE_TYPE_GRAPHICS, QUEUE_TYPE_COMPUTE });

        for (uint32_t i = 0; i < 64; i++)
        {
            multiFrameResource.stbnTexture[i] = std::make_shared<Texture>(
                "Asset/BuildIn/Texture/stbn/stbn_scalar_2Dx1Dx1D_128x128x64x1_" + std::to_string(i) + ".png");
        }

        multiFrameResource.objectIDTexture[0] = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R32_UINT,
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 1);

        multiFrameResource.objectIDTexture[1] = std::make_shared<Texture>(
            TEXTURE_TYPE_2D,
            FORMAT_R32_UINT,
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 1);

        multiFrameResource.gbufferNormalTexture[0] = std::make_shared<Texture>(
            TEXTURE_TYPE_2D,
            FORMAT_R8G8B8A8_UNORM,     // 拼积木布局：八面体2×12位拼进RGB（位模式，非符号量），A=specF
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 1);

        multiFrameResource.gbufferNormalTexture[1] = std::make_shared<Texture>(
            TEXTURE_TYPE_2D,
            FORMAT_R8G8B8A8_UNORM,     // 历史对同格式（copy pass两端必须一致）
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 1);

        multiFrameResource.gbufferIORTexture = std::make_shared<Texture>(
            TEXTURE_TYPE_2D,
            FORMAT_R8_UNORM,
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 1);

        multiFrameResource.stableHDRColorTexture[0] = std::make_shared<Texture>(      
            TEXTURE_TYPE_2D,
            EngineContext::Render()->GetHdrColorFormat(),
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 1);

        multiFrameResource.stableHDRColorTexture[1] = std::make_shared<Texture>(
            TEXTURE_TYPE_2D,
            EngineContext::Render()->GetHdrColorFormat(),
            Extent3D(windowExtent.width, windowExtent.height, 1),
            1, 1);

        multiFrameResource.surfaceCacheTexture[0] = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R8G8B8A8_UNORM,
            Extent3D(SURFACE_CACHE_SIZE, SURFACE_CACHE_SIZE, 1),
            1, 1);

        multiFrameResource.surfaceCacheTexture[1] = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R8G8B8A8_SNORM,
            Extent3D(SURFACE_CACHE_SIZE, SURFACE_CACHE_SIZE, 1),
            1, 1);

        multiFrameResource.surfaceCacheTexture[2] = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R16G16B16A16_SFLOAT,
            Extent3D(SURFACE_CACHE_SIZE, SURFACE_CACHE_SIZE, 1),
            1, 1);

        multiFrameResource.surfaceCacheTexture[3] = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R16G16B16A16_SFLOAT,
            Extent3D(SURFACE_CACHE_SIZE, SURFACE_CACHE_SIZE, 1),
            1, 1);

        multiFrameResource.surfaceCacheTexture[4] = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            EngineContext::Render()->GetDepthFormat(),
            Extent3D(SURFACE_CACHE_SIZE, SURFACE_CACHE_SIZE, 1),
            1, 1);
    }

    // 刀2b：SamplerRS已删除（双sampler池合一）。内置sampler对池集SAMPLER槽的写入在下方
    // init循环内（原set0 b34写入随bindless槽一并迁池）

    for(auto& resource : perFrameResources)
    {
        resource.descriptorSet->UpdateDescriptor({
            .binding = SET0_BINDING_GLOBAL_SETTING,
            .index = 0,
            .resourceType = RESOURCE_TYPE_RW_BUFFER,
            .buffer = multiFrameResource.globalSettingBuffer.buffer});

        resource.descriptorSet->UpdateDescriptor({
            .binding = SET0_BINDING_CAMERA,
            .index = 0,
            .resourceType = RESOURCE_TYPE_UNIFORM_BUFFER,
            .buffer = resource.cameraBuffer.buffer});

        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = SET1_BINDING_OBJECT,
            .index = 0,
            .resourceType = RESOURCE_TYPE_RW_BUFFER,
            .buffer = resource.objectBuffer.buffer});

        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = SET1_BINDING_MESH_CARD,
            .index = 0,
            .resourceType = RESOURCE_TYPE_RW_BUFFER,
            .buffer = resource.meshCardBuffer.buffer});

        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = SET1_BINDING_MATERIAL,
            .index = 0,
            .resourceType = RESOURCE_TYPE_RW_BUFFER,
            .buffer = multiFrameResource.materialBuffer.buffer});

        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = SET1_BINDING_LIGHT,
            .index = 0,
            .resourceType = RESOURCE_TYPE_RW_BUFFER,
            .buffer = resource.lightBuffer.buffer});




        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = SET1_BINDING_SKYBOX_IBL,
            .index = 0,
            .resourceType = RESOURCE_TYPE_TEXTURE,
            .textureView = multiFrameResource.skyboxIBLTexuture[0]->textureView});

        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = SET1_BINDING_SKYBOX_IBL,
            .index = 1,
            .resourceType = RESOURCE_TYPE_TEXTURE,
            .textureView = multiFrameResource.skyboxIBLTexuture[1]->textureView});

        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = SET1_BINDING_MESH_CLUSTER,
            .index = 0,
            .resourceType = RESOURCE_TYPE_RW_BUFFER,
            .buffer = multiFrameResource.meshClusterBuffer.buffer});

        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = SET1_BINDING_MESH_CLUSTER_GROUP,
            .index = 0,
            .resourceType = RESOURCE_TYPE_RW_BUFFER,
            .buffer = multiFrameResource.meshClusterGroupBuffer.buffer});











        for (uint32_t i = 0; i < 64; i++)
        {
            resource.poolDescriptorSet->UpdateDescriptor({
                .binding = SET1_BINDING_STBN,
                .index = i,
                .resourceType = RESOURCE_TYPE_TEXTURE,
                .textureView = multiFrameResource.stbnTexture[i]->textureView });
        }


              
        resource.poolDescriptorSet->UpdateDescriptor({
            .binding = SET1_BINDING_VERTEX,
            .index = 0,
            .resourceType = RESOURCE_TYPE_RW_BUFFER,
            .buffer = multiFrameResource.vertexBuffer.buffer});

        // 刀3：方向/点光源阴影描述符写入已删——阴影纹理经RDG边绑至消费者set2阴影域（shadows.glsl b0/b1）
        // 全局采样器：已迁bindless RS静态采样器声明（set1创建期播种），手动写入循环已删
    }
}

void RenderResourceManager::LoadIcons()
{
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/documentation.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/world.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/entity.png"));
    icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/camera.png"));
    icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/directionalLight.png"));
    icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/pointLight.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/spotLight.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/skyLight.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/material.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/text.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/decal.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/postProcessVolume.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/exponentialHeightFog.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/volumetricCloud.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/atmosphericFog.png"));
    // icons.push_back(std::make_shared<Texture>("Asset/BuildIn/Texture/icon/skyAtmosphere.png"));

    // iconInfo.documentationIcon          = icons[0]->textureID;
    // iconInfo.worldIcon                  = icons[1]->textureID;
    // iconInfo.entityIcon                 = icons[2]->textureID;
    iconInfo.cameraIcon                 = icons[0]->textureID;
    iconInfo.directionalLightIcon       = icons[1]->textureID;
    iconInfo.pointLightIcon             = icons[2]->textureID;
    // iconInfo.spotLightIcon              = icons[6]->textureID;
    // iconInfo.skyLightIcon               = icons[7]->textureID;
    // iconInfo.materialIcon               = icons[8]->textureID;
    // iconInfo.textIcon                   = icons[9]->textureID;
    // iconInfo.decalIcon                  = icons[10]->textureID;
    // iconInfo.postProcessVolumeIcon      = icons[11]->textureID;
    // iconInfo.exponentialHeightFogIcon   = icons[12]->textureID;
    // iconInfo.volumetricCloudIcon        = icons[13]->textureID;
    // iconInfo.atmosphericFogIcon         = icons[14]->textureID;
    // iconInfo.skyAtmosphereIcon          = icons[15]->textureID;
}