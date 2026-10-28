#ifndef SET2_GLSL
#define SET2_GLSL

// 刀7b-4（2026-10-20）：set2胖头=pass IO集全量声明（98槽v3表）+域访问器。
// 契约：槽号宏见defines.glsl（单源）；RW槽镜像格式/block结构=PLANNING.md契约表（拆分为主裁决版）。
// 基石（探针实证）：spirv-reflect accessed=main可达调用图内指令引用，纯声明/死访问器均不计
// ——本文件可被任意shader随意include，未引用的槽零成本（无需描述符、不进校验）。
// 命名过渡债（7b-3迁移时shader侧同步改名，见PLANNING待定项）：
//   读侧MESH_CLUSTER_DRAW_INFOS_READ/写侧MESH_CLUSTER_DRAW_INFOS、RESTIR_DI/GI_RESERVOIRS族、
//   TAA_REPROJECTION_RESULT/TAA_OUT_COLOR、REPROJECTION_RESULT_OUT、CLIPMAP_VISUALIZE_OUT、
//   RTB_OUT/PT_OUT/PT_HISTORY/IBL_OUT、DI_DIFFUSE_COLOR/DI_SPECULAR_COLOR
// include set1（SAMPLER[]供访问器采样；set1自带defines/types/math/intersection链）——
// 层级单向：set0 → set1 → set2，set2不自引用set0
#include "defines.glsl"
#include "../set1/set1.glsl"

// ===== 共享域 0-13（跨pass语义槽）=================================================================

// 照明域：cluster剔除结构（uimage3D/imageLoad消费=STORAGE描述符，消费者用ReadWrite边）
layout(set = 2, binding = SET2_BINDING_LIGHT_CLUSTER_GRID, rg32ui) uniform uimage3D LIGHT_CLUSTER_GRID;
layout(set = 2, binding = SET2_BINDING_LIGHT_CLUSTER_INDEX) buffer light_cluster_index {

    LightClusterIndex slot[MAX_LIGHTS_PER_CLUSTER * LIGHT_CLUSTER_NUM];

} LIGHT_CLUSTER_INDEX;

// 历史域：屏幕空间历史/跨帧纹理。DEPTH[0]="Depth" DEPTH[1]="Previous Depth"
// PYRAMID[0/1]="Depth Min"/"Depth Max" VELOCITY="G-Buffer Velocity"
// OBJECT_ID[0/1]="G-Buffer Object ID"/"History G-Buffer Object ID"
layout(set = 2, binding = SET2_BINDING_HISTORY_DEPTH)          uniform texture2D DEPTH[2];
layout(set = 2, binding = SET2_BINDING_HISTORY_DEPTH_PYRAMID)  uniform texture2D DEPTH_PYRAMID[2];
layout(set = 2, binding = SET2_BINDING_HISTORY_VELOCITY)       uniform texture2D VELOCITY;
layout(set = 2, binding = SET2_BINDING_HISTORY_OBJECT_ID)      uniform utexture2D OBJECT_ID[2];

// 阴影域：POINT_SHADOW[8]=filtered0-3+depth4-7
layout(set = 2, binding = SET2_BINDING_DIRECTIONAL_SHADOW)     uniform texture2D DIRECTIONAL_SHADOW[4];
layout(set = 2, binding = SET2_BINDING_POINT_SHADOW)           uniform textureCube POINT_SHADOW[8];

// SC域：atlas
layout(set = 2, binding = SET2_BINDING_SURFACE_CACHE)          uniform texture2D SURFACE_CACHE[5];

// 照明域：volume light烘焙纹理（DDGI按"Volume Light Irradiance/Depth [id]"入图，动态索引）
layout(set = 2, binding = SET2_BINDING_VOLUME_LIGHT_IRRADIANCE) uniform texture2D VOLUME_LIGHT_IRRADIANCE[];
layout(set = 2, binding = SET2_BINDING_VOLUME_LIGHT_DEPTH)      uniform texture2D VOLUME_LIGHT_DEPTH[];

// SC域：mesh card优先级readback（FetchMeshCard*读写，PostProcessing copy链回读）
layout(set = 2, binding = SET2_BINDING_MESH_CARD_READBACK) buffer mesh_card_readbacks {

    int slot[MAX_PER_FRAME_OBJECT_SIZE * 6];

} MESH_CARD_READBACKS;

// gizmo私有（写入方：gizmo-init+culling三子pass的AddGizmoBox调试盒）
layout(set = 2, binding = SET2_BINDING_GIZMO_DRAW_DATA) buffer gizmoDrawData {

    RHIIndexedIndirectCommand command[4];
    GizmoBoxInfo boxes[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoSphereInfo spheres[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoLineInfo lines[MAX_GIZMO_PRIMITIVE_COUNT];
    GizmoBillboardInfo worldBillboards[MAX_GIZMO_PRIMITIVE_COUNT];

} GIZMO_DRAW_DATA;

// mesh剔除域读侧：各draw pass消费（PointShadow一面一槽x6；slot下标=firstInstance+instance序号）
layout(set = 2, binding = SET2_BINDING_CLUSTER_DRAW_INFOS_READ) readonly buffer mesh_cluster_draw_infos_read {

    MeshClusterDrawInfo slot[];

} MESH_CLUSTER_DRAW_INFOS_READ[6];

// ===== mesh剔除域 14-19（GPUCulling写侧，数组下标=mesh pass第j个子遍）=============================

layout(set = 2, binding = SET2_BINDING_CLUSTER_DRAW_INFOS_WRITE) buffer mesh_cluster_draw_infos {

    MeshClusterDrawInfo slot[];     // 无界SSBO，运行期大小=MAX_PER_FRAME_CLUSTER_SIZE

} MESH_CLUSTER_DRAW_INFOS[];

layout(set = 2, binding = SET2_BINDING_INDIRECT_MESH_DRAW_DATAS) buffer IndirectMeshDrawDatas {

    IndirectSetting setting;
    IndirectMeshDrawInfo draws[MAX_PER_FRAME_OBJECT_SIZE];

} MESH_DRAW_DATAS[];

layout(set = 2, binding = SET2_BINDING_INDIRECT_MESH_DRAW_COMMANDS) buffer IndirectMeshDrawCommands {

    RHIIndirectCommand commands[MAX_PER_FRAME_OBJECT_SIZE];

} MESH_DRAW_COMMANDS[];

layout(set = 2, binding = SET2_BINDING_INDIRECT_CLUSTER_DRAW_DATAS) buffer IndirectClusterDrawDatas {

    IndirectSetting setting;
    IndirectClusterDrawInfo draws[MAX_PER_FRAME_CLUSTER_SIZE];

} CLUSTER_DRAW_DATAS[];

layout(set = 2, binding = SET2_BINDING_INDIRECT_CLUSTER_DRAW_COMMANDS) buffer IndirectClusterDrawCommands {

    RHIIndirectCommand command[MAX_PER_PASS_PIPELINE_STATE_COUNT];

} CLUSTER_DRAW_COMMANDS[];

layout(set = 2, binding = SET2_BINDING_INDIRECT_CLUSTER_GROUP_DATAS) buffer IndirectClusterGroupDrawDatas {

    IndirectSetting setting;
    IndirectClusterGroupDrawInfo draws[MAX_PER_FRAME_CLUSTER_GROUP_SIZE];

} CLUSTER_GROUP_DRAW_DATAS[];

// ===== G-Buffer域 20-30（DI侧旧RW声明随迁移统一texelFetch读本形态）================================

layout(set = 2, binding = SET2_BINDING_GBUFFER_DIFFUSE_METALLIC)      uniform texture2D G_BUFFER_DIFFUSE_TRANSPARENT;     // rgb=原始反照率(预乘透明)，a=metallic
layout(set = 2, binding = SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS)     uniform texture2D G_BUFFER_NORMAL_SPECF;           // 八面体2x12位拼，A=specF
layout(set = 2, binding = SET2_BINDING_GBUFFER_EMISSION)              uniform texture2D G_BUFFER_EMISSION;               // RGBM编码
layout(set = 2, binding = SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS) uniform texture2D G_BUFFER_SPECULAR_F0_ROUGHNESS;
layout(set = 2, binding = SET2_BINDING_GBUFFER_GEOMETRIC_NORMAL_AO)   uniform texture2D G_BUFFER_GEOMETRIC_NORMAL_AO;
layout(set = 2, binding = SET2_BINDING_GBUFFER_IOR)                   uniform texture2D G_BUFFER_IOR;                    // (η-1)/2，薄透明pass写
layout(set = 2, binding = SET2_BINDING_REPROJECTION_RESULT)           uniform texture2D REPROJECTION_RESULT;

layout(set = 2, binding = SET2_BINDING_FINAL_COLOR, rgba16f)      uniform image2D FINAL_COLOR;        // "Noisy HDR Color"（四号归一）
layout(set = 2, binding = SET2_BINDING_DI_DIFFUSE_COLOR, rgba16f) uniform image2D DI_DIFFUSE_COLOR;
layout(set = 2, binding = SET2_BINDING_DI_SPECULAR_COLOR, rgba16f) uniform image2D DI_SPECULAR_COLOR;

// ===== DDGI域 31-37 ================================================================================

layout(set = 2, binding = SET2_BINDING_DDGI_TRACE_ALBEDO_METALLIC, rgba8)       uniform image2D TRACE_ALBEDO_METALLIC;      // rgb=原始反照率，a=metallic
layout(set = 2, binding = SET2_BINDING_DDGI_TRACE_NORMAL_ROUGHNESS, rgba8_snorm) uniform image2D TRACE_NORMAL_ROUGHNESS;    // rgb=世界法线，a=roughness
layout(set = 2, binding = SET2_BINDING_DDGI_TRACE_EMISSION, rgba16f)           uniform image2D TRACE_EMISSION;            // 线性自发光
layout(set = 2, binding = SET2_BINDING_DDGI_TRACE_POSITION, rgba16f)           uniform image2D TRACE_DIRECTION_DIST;      // rgb=光线方向，a=命中距离
layout(set = 2, binding = SET2_BINDING_DDGI_RADIANCE, rgba16f)                 uniform image2D DDGI_RADIANCE;
layout(set = 2, binding = SET2_BINDING_DDGI_IRRADIANCE, rgba16f)               uniform image2D DDGI_IRRADIANCE;
layout(set = 2, binding = SET2_BINDING_DDGI_DEPTH, rg16f)                      uniform image2D DDGI_DEPTH;

// ===== 后处理通用域 38-44（通用IO=角色槽：槽共享、资源按边逐pass换绑）=============================

layout(set = 2, binding = SET2_BINDING_IN_COLOR)         uniform texture2D IN_COLOR[];        // x2数组槽（bloom双输入），单输入用户取[0]
layout(set = 2, binding = SET2_BINDING_OUT_COLOR, rgba16f) uniform image2D OUT_COLOR;        // HDR中间色契约
layout(set = 2, binding = SET2_BINDING_POSTPROCESS_OUT, rgba8) uniform image2D POSTPROCESS_OUT;  // v3拆1：post_process的LDR输出
layout(set = 2, binding = SET2_BINDING_EXPOSURE_DATA) buffer exposure_data
{
    ExposureSetting setting;
    float luminance;                     // 计算得到的平均亮度
    float adaptedLuminance;              // 多帧渐进的亮度
    float _padding[2];
    uint histogramBuffer[256];           // 直方图数组
    uint readBackHistogramBuffer[256];   // 回读用的数组
} EXPOSURE_DATA;
layout(set = 2, binding = SET2_BINDING_IN_TEX_ARRAY)  uniform texture2D IN_TEX_ARRAY[6];     // PointShadow输入（6面）
layout(set = 2, binding = SET2_BINDING_OUT_TEX_ARRAY, rg32f) uniform image2D OUT_TEX_ARRAY[6];  // PointShadow输出
layout(set = 2, binding = SET2_BINDING_BRDF_LUT)       uniform texture2D BRDF_LUT;

// ===== 金字塔/深度域 45-46 ==========================================================================

layout(set = 2, binding = SET2_BINDING_DEPTH_PYRAMID_LAST_MIP)   uniform texture2D DEPTH_PYRAMID_LAST_MIP;
layout(set = 2, binding = SET2_BINDING_DEPTH_PYRAMID_CURRENT_MIP, r32f) uniform image2D DEPTH_PYRAMID_CURRENT_MIP;

// ===== ReSTIR DI域 47-50（v3拆2：与GI的reservoir block异构）=======================================

layout(set = 2, binding = SET2_BINDING_RESTIR_DI_SETTING) buffer restir_di_setting {
    RestirDISetting RESTIR_DI_SETTING;
};
layout(set = 2, binding = SET2_BINDING_RESTIR_DI_RESERVOIRS) buffer restir_di_reservoirs {
    DIReservoir RESTIR_DI_RESERVOIRS[];                 // 本帧新生成的Reservoir(以及时域合并的结果)
};
layout(set = 2, binding = SET2_BINDING_RESTIR_DI_PREV_RESERVOIRS) buffer restir_di_prev_reservoirs {
    DIReservoir RESTIR_DI_PREV_RESERVOIRS[];
};
layout(set = 2, binding = SET2_BINDING_RESTIR_DI_RESULT_RESERVOIRS) buffer restir_di_result_reservoirs {
    DIReservoir RESTIR_DI_RESULT_RESERVOIRS[];
};

// ===== ReSTIR GI域 51-56 ===========================================================================

layout(set = 2, binding = SET2_BINDING_RESTIR_GI_SETTING) buffer restir_gi_setting {
    RestirGISetting RESTIR_GI_SETTING;
};
layout(set = 2, binding = SET2_BINDING_RESTIR_GI_RESERVOIRS) buffer restir_gi_reservoirs {
    GIReservoir RESTIR_GI_RESERVOIRS[];
};
layout(set = 2, binding = SET2_BINDING_RESTIR_GI_PREV_RESERVOIRS) buffer restir_gi_prev_reservoirs {
    GIReservoir RESTIR_GI_PREV_RESERVOIRS[];
};
layout(set = 2, binding = SET2_BINDING_RESTIR_GI_RESULT_RESERVOIRS) buffer restir_gi_result_reservoirs {
    GIReservoir RESTIR_GI_RESULT_RESERVOIRS[];
};
layout(set = 2, binding = SET2_BINDING_GI_DIFFUSE_COLOR, rgba16f)  uniform image2D GI_DIFFUSE_LIGHT;
layout(set = 2, binding = SET2_BINDING_GI_SPECULAR_COLOR, rgba16f) uniform image2D GI_SPECULAR_LIGHT;

// ===== SSSR域 57-63 ================================================================================

layout(set = 2, binding = SET2_BINDING_SSSR_HIT_RESULT, rgba16f)  uniform image2D SSSR_HIT_RESULT;     // trace到的命中信息
layout(set = 2, binding = SET2_BINDING_SSSR_HIT_COLOR, rgba16f)   uniform image2D SSSR_HIT_COLOR;      // trace到的radiance
layout(set = 2, binding = SET2_BINDING_SSSR_PDF, rgba16f)         uniform image2D SSSR_PDF;            // trace到的pdf
layout(set = 2, binding = SET2_BINDING_SSSR_RESOLVE, rgba16f)     uniform image2D SSSR_RESOLVE;        // resolve结果
layout(set = 2, binding = SET2_BINDING_SSSR_HISTORY, rgba16f)     uniform image2D SSSR_HISTORY;        // 历史帧
layout(set = 2, binding = SET2_BINDING_SSSR_IN_COLOR_PYRAMID)     uniform texture2D IN_COLOR_PYRAMID;
layout(set = 2, binding = SET2_BINDING_SSSR_REPROJECTION_RESULT)  uniform texture2D SSSR_REPROJECTION_RESULT;

// ===== SVGF域 64-68（IN/OUT并入通用38/39，G-Buffer读并入20/21）====================================

layout(set = 2, binding = SET2_BINDING_SVGF_HISTORY_GBUFFER_NORMAL)   uniform texture2D HISTORY_G_BUFFER_NORMAL_ROUGHNESS;
layout(set = 2, binding = SET2_BINDING_SVGF_HISTORY_MOMENTS)          uniform texture2D HISTORY_MOMENTS;           // 前两维moments，第三维history length，第四维预滤波variance
layout(set = 2, binding = SET2_BINDING_SVGF_CURRENT_MOMENTS, rgba32f) uniform image2D CURRENT_MOMENTS;
layout(set = 2, binding = SET2_BINDING_SVGF_HISTORY_COLOR_VARIANCE)   uniform texture2D HISTORY_COLOR_VARIANCE;
layout(set = 2, binding = SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, rgba16f) uniform image2D CURRENT_COLOR_VARIANCE[2];    // 需要pingpong

// ===== SurfaceCache域 69-74 ========================================================================

layout(set = 2, binding = SET2_BINDING_SC_CACHE_DIFFUSE)   uniform texture2D CACHE_DIFFUSE_METALLIC;
layout(set = 2, binding = SET2_BINDING_SC_CACHE_NORMAL)    uniform texture2D CACHE_NORMAL_ROUGHNESS;
layout(set = 2, binding = SET2_BINDING_SC_CACHE_EMISSION)  uniform texture2D CACHE_EMISSION;
layout(set = 2, binding = SET2_BINDING_SC_CACHE_DEPTH)     uniform texture2D CACHE_DEPTH;
layout(set = 2, binding = SET2_BINDING_SC_CACHE_LIGHTING, rgba16f) uniform image2D CACHE_LIGHTING;
layout(set = 2, binding = SET2_BINDING_SC_DIRECT_LIGHTING_DISPATCH) buffer direct_lighting_dispatch {
    SurfaceCacheLightingDispatch slot[];
} CACHE_DIRECT_LIGHTING_DISPATCH;

// ===== NRD wrapper域 75-78（引擎侧wrapper；NRD SDK内部走NRDIntegration私有布局）==================

layout(set = 2, binding = SET2_BINDING_NRD_VIEW_Z, r32f)           uniform image2D NRD_VIEW_Z;
layout(set = 2, binding = SET2_BINDING_NRD_NORMAL, rgba8_snorm)    uniform image2D NRD_NORMAL;
layout(set = 2, binding = SET2_BINDING_NRD_OUT_DIFFUSE)            uniform texture2D NRD_OUT_DIFFUSE;
layout(set = 2, binding = SET2_BINDING_NRD_OUT_SPECULAR)           uniform texture2D NRD_OUT_SPECULAR;

// ===== pass私有 79-97 ==============================================================================

layout(set = 2, binding = SET2_BINDING_TAA_CURRENT_COLOR)       uniform texture2D CURRENT_COLOR;
layout(set = 2, binding = SET2_BINDING_TAA_HISTORY_COLOR)       uniform texture2D HISTORY_COLOR;
layout(set = 2, binding = SET2_BINDING_TAA_REPROJECTION_RESULT) uniform texture2D TAA_REPROJECTION_RESULT;
layout(set = 2, binding = SET2_BINDING_TAA_OUT_COLOR, rgba16f)  uniform image2D TAA_OUT_COLOR;
layout(set = 2, binding = SET2_BINDING_TAA_HISTORY_OUT, rgba16f) uniform image2D HISTORY_OUT;   // kernel直写下帧history

layout(set = 2, binding = SET2_BINDING_REPROJECTION_CURRENT_NORMAL)  uniform texture2D CURRENT_NORMAL;
layout(set = 2, binding = SET2_BINDING_REPROJECTION_HISTORY_NORMAL)  uniform texture2D HISTORY_NORMAL;
layout(set = 2, binding = SET2_BINDING_REPROJECTION_RESULT_OUT, rgba32f) uniform image2D REPROJECTION_RESULT_OUT;

layout(set = 2, binding = SET2_BINDING_FOG_VOXEL_CURRENT, rgba16f)    uniform image3D VOLUMETRIC_FOG_RAW;            // 未计算积分时的体素纹理
layout(set = 2, binding = SET2_BINDING_FOG_VOXEL_HISTORY, rgba16f)    uniform image3D VOLUMETRIC_FOG_RAW_HISTORY;    // 未计算积分时的历史帧
layout(set = 2, binding = SET2_BINDING_FOG_VOXEL_INTEGRATED, rgba16f) uniform image3D VOLUMETRIC_FOG_INTEGRAL;      // 积分体素纹理
layout(set = 2, binding = SET2_BINDING_FOG_VOXEL_OUT, rgba16f)        uniform image2D VOLUMETRIC_FOG_IMG;            // 输出纹理

layout(set = 2, binding = SET2_BINDING_CLIPMAP_A, rgba16f)   uniform image3D VXGI_CLIPMAP;
layout(set = 2, binding = SET2_BINDING_CLIPMAP_B) buffer vxgi_clipmap_buffer {
    ClipmapInfo clipmapInfo;
} VXGI_CLIPMAP_BUFFER;
layout(set = 2, binding = SET2_BINDING_CLIPMAP_C, rgba16f)   uniform image2D CLIPMAP_VISUALIZE_OUT;

layout(set = 2, binding = SET2_BINDING_RTB_A, rgba8)         uniform image2D RTB_OUT;
layout(set = 2, binding = SET2_BINDING_PT_A, rgba16f)        uniform image2D PT_OUT;
layout(set = 2, binding = SET2_BINDING_PT_B, rgba32f)        uniform image2D PT_HISTORY;
layout(set = 2, binding = SET2_BINDING_IBL_A, rgba16f)       uniform image2D IBL_OUT;

// ===== ReSTIR DI G-Buffer RW读槽 98-101（v4拆分：DI的storage读法专属，与G-Buffer域TEXTURE槽分立）=====
layout(set = 2, binding = SET2_BINDING_RESTIR_DI_GBUFFER_DIFFUSE, rgba8)  uniform image2D RESTIR_DI_GBUFFER_DIFFUSE;
layout(set = 2, binding = SET2_BINDING_RESTIR_DI_GBUFFER_NORMAL, rgba8)    uniform image2D RESTIR_DI_GBUFFER_NORMAL;
layout(set = 2, binding = SET2_BINDING_RESTIR_DI_GBUFFER_EMISSION, rgba8)  uniform image2D RESTIR_DI_GBUFFER_EMISSION;
layout(set = 2, binding = SET2_BINDING_RESTIR_DI_GBUFFER_SPECF0, rgba8)   uniform image2D RESTIR_DI_GBUFFER_SPECF0;

// ===== 访问器（死访问器不计accessed——探针实证；引用set1的SAMPLER[]常驻全背书）====================

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

#endif
