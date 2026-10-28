#ifndef SET2_DEFINES_GLSL
#define SET2_DEFINES_GLSL

// ===== 共享域 0-13（跨pass语义槽）=====
#define SET2_BINDING_LIGHT_CLUSTER_GRID                 0
#define SET2_BINDING_LIGHT_CLUSTER_INDEX                1
#define SET2_BINDING_HISTORY_DEPTH                      2
#define SET2_BINDING_HISTORY_DEPTH_PYRAMID              3
#define SET2_BINDING_HISTORY_VELOCITY                   4
#define SET2_BINDING_HISTORY_OBJECT_ID                  5
#define SET2_BINDING_DIRECTIONAL_SHADOW                 6
#define SET2_BINDING_POINT_SHADOW                       7
#define SET2_BINDING_SURFACE_CACHE                      8
#define SET2_BINDING_VOLUME_LIGHT_IRRADIANCE            9
#define SET2_BINDING_VOLUME_LIGHT_DEPTH                 10
#define SET2_BINDING_MESH_CARD_READBACK                 11
#define SET2_BINDING_GIZMO_DRAW_DATA                    12
#define SET2_BINDING_CLUSTER_DRAW_INFOS_READ            13

// ===== mesh剔除域 14-19（GPUCulling写侧，x1024）=====
#define SET2_BINDING_CLUSTER_DRAW_INFOS_WRITE           14
#define SET2_BINDING_INDIRECT_MESH_DRAW_DATAS           15
#define SET2_BINDING_INDIRECT_MESH_DRAW_COMMANDS        16
#define SET2_BINDING_INDIRECT_CLUSTER_DRAW_DATAS        17
#define SET2_BINDING_INDIRECT_CLUSTER_DRAW_COMMANDS     18
#define SET2_BINDING_INDIRECT_CLUSTER_GROUP_DATAS       19

// ===== G-Buffer域 20-30（DI侧读法随7b-3统一texelFetch）=====
#define SET2_BINDING_GBUFFER_DIFFUSE_METALLIC           20
#define SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS           21
#define SET2_BINDING_GBUFFER_EMISSION                   22
#define SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS      23
#define SET2_BINDING_GBUFFER_GEOMETRIC_NORMAL_AO        24
#define SET2_BINDING_GBUFFER_IOR                        25
// 26 已退役（DirectionalDepthHistory特性2026-10-22删除，槽号保留防错位）
#define SET2_BINDING_REPROJECTION_RESULT                27
#define SET2_BINDING_FINAL_COLOR                        28     // "Noisy HDR Color"四号归一
#define SET2_BINDING_DI_DIFFUSE_COLOR                   29
#define SET2_BINDING_DI_SPECULAR_COLOR                  30

// ===== DDGI域 31-37 =====
#define SET2_BINDING_DDGI_TRACE_ALBEDO_METALLIC         31
#define SET2_BINDING_DDGI_TRACE_NORMAL_ROUGHNESS        32
#define SET2_BINDING_DDGI_TRACE_EMISSION                33
#define SET2_BINDING_DDGI_TRACE_POSITION                34
#define SET2_BINDING_DDGI_RADIANCE                      35
#define SET2_BINDING_DDGI_IRRADIANCE                    36
#define SET2_BINDING_DDGI_DEPTH                         37

// ===== 后处理通用域 38-44（通用IO=角色槽：槽共享、资源按边逐pass换绑）=====
#define SET2_BINDING_IN_COLOR                           38
#define SET2_BINDING_OUT_COLOR                          39     // rgba16f契约（HDR中间色）
#define SET2_BINDING_POSTPROCESS_OUT                    40     // v3拆1：post_process的LDR输出(rgba8)
#define SET2_BINDING_EXPOSURE_DATA                      41
#define SET2_BINDING_IN_TEX_ARRAY                       42
#define SET2_BINDING_OUT_TEX_ARRAY                      43
#define SET2_BINDING_BRDF_LUT                           44

// ===== 金字塔/深度域 45-46 =====
#define SET2_BINDING_DEPTH_PYRAMID_LAST_MIP             45
#define SET2_BINDING_DEPTH_PYRAMID_CURRENT_MIP          46

// ===== ReSTIR DI域 47-50（v3拆2：block=DIReservoir/RestirDISetting）=====
#define SET2_BINDING_RESTIR_DI_SETTING                  47
#define SET2_BINDING_RESTIR_DI_RESERVOIRS               48
#define SET2_BINDING_RESTIR_DI_PREV_RESERVOIRS          49
#define SET2_BINDING_RESTIR_DI_RESULT_RESERVOIRS        50

// ===== ReSTIR GI域 51-56（v3拆2：block=GIReservoir/RestirGISetting）=====
#define SET2_BINDING_RESTIR_GI_SETTING                  51
#define SET2_BINDING_RESTIR_GI_RESERVOIRS               52
#define SET2_BINDING_RESTIR_GI_PREV_RESERVOIRS          53
#define SET2_BINDING_RESTIR_GI_RESULT_RESERVOIRS        54
#define SET2_BINDING_GI_DIFFUSE_COLOR                   55
#define SET2_BINDING_GI_SPECULAR_COLOR                  56

// ===== SSSR域 57-63 =====
#define SET2_BINDING_SSSR_HIT_RESULT                    57
#define SET2_BINDING_SSSR_HIT_COLOR                     58
#define SET2_BINDING_SSSR_PDF                           59
#define SET2_BINDING_SSSR_RESOLVE                       60
#define SET2_BINDING_SSSR_HISTORY                       61
#define SET2_BINDING_SSSR_IN_COLOR_PYRAMID              62
#define SET2_BINDING_SSSR_REPROJECTION_RESULT           63

// ===== SVGF域 64-68（IN/OUT并入通用38/39，G-Buffer读并入20/21）=====
#define SET2_BINDING_SVGF_HISTORY_GBUFFER_NORMAL        64
#define SET2_BINDING_SVGF_HISTORY_MOMENTS               65
#define SET2_BINDING_SVGF_CURRENT_MOMENTS               66
#define SET2_BINDING_SVGF_HISTORY_COLOR_VARIANCE        67
#define SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE        68

// ===== SurfaceCache域 69-74 =====
#define SET2_BINDING_SC_CACHE_DIFFUSE                   69
#define SET2_BINDING_SC_CACHE_NORMAL                    70
#define SET2_BINDING_SC_CACHE_EMISSION                  71
#define SET2_BINDING_SC_CACHE_DEPTH                     72
#define SET2_BINDING_SC_CACHE_LIGHTING                  73
#define SET2_BINDING_SC_DIRECT_LIGHTING_DISPATCH        74

// ===== NRD wrapper域 75-78（引擎侧wrapper；NRD SDK内部布局不在此列）=====
#define SET2_BINDING_NRD_VIEW_Z                         75
#define SET2_BINDING_NRD_NORMAL                         76
#define SET2_BINDING_NRD_OUT_DIFFUSE                    77
#define SET2_BINDING_NRD_OUT_SPECULAR                   78

// ===== pass私有 79-97 =====
#define SET2_BINDING_TAA_CURRENT_COLOR                  79
#define SET2_BINDING_TAA_HISTORY_COLOR                  80
#define SET2_BINDING_TAA_REPROJECTION_RESULT            81
#define SET2_BINDING_TAA_OUT_COLOR                      82
#define SET2_BINDING_TAA_HISTORY_OUT                    83
#define SET2_BINDING_REPROJECTION_CURRENT_NORMAL        84
#define SET2_BINDING_REPROJECTION_HISTORY_NORMAL        85
#define SET2_BINDING_REPROJECTION_RESULT_OUT            86
#define SET2_BINDING_FOG_VOXEL_CURRENT                  87
#define SET2_BINDING_FOG_VOXEL_HISTORY                  88
#define SET2_BINDING_FOG_VOXEL_INTEGRATED               89
#define SET2_BINDING_FOG_VOXEL_OUT                      90
#define SET2_BINDING_CLIPMAP_A                          91
#define SET2_BINDING_CLIPMAP_B                          92
#define SET2_BINDING_CLIPMAP_C                          93
#define SET2_BINDING_RTB_A                              94
#define SET2_BINDING_PT_A                               95
#define SET2_BINDING_PT_B                               96
#define SET2_BINDING_IBL_A                              97

// ===== ReSTIR DI G-Buffer RW读槽 98-101（v4拆分裁决：DI的storage读法保留，不开TEXTURE融合）=====
#define SET2_BINDING_RESTIR_DI_GBUFFER_DIFFUSE        98
#define SET2_BINDING_RESTIR_DI_GBUFFER_NORMAL          99
#define SET2_BINDING_RESTIR_DI_GBUFFER_EMISSION        100
#define SET2_BINDING_RESTIR_DI_GBUFFER_SPECF0          101

#endif
