# 刀7b-1：set2 全局槽位规划表（施工图 v3 终态）

生成于 2026-10-20；v3 = 契约对账版（格式/block 契约逐槽核定，拆分为主融合为次）。
基础=v2 的全 pass 声明核对 + 61 条活 image 声明 + 全部 buffer block 对账。
目标：统一 set2 RS 要求全局唯一槽语义——同语义共享槽（per-pass 按边填槽，跨 pass 共享无冲突），
同槽异类/异语义/异契约错开。

## 冲突解决策略

1. **同槽异类型**（b32 的 RW_BUFFER/RW_TEXTURE/TEXTURE 三家）：按语义拆独立槽
2. **同槽同类型异语义**（b35 的 SSSR_RESOLVE vs IN_COLOR vs CACHE_LIGHTING）：拆独立槽
3. **读/写侧型冲突**（CLUSTER_DRAW_INFOS 的 BUFFER vs RW_BUFFER）：读写分槽
4. **DI/GI 声明风格差异**（G-Buffer b0-2/b7：DI 用 RW_TEXTURE、GI 用 TEXTURE）：
   统一为 TEXTURE，DI 侧 shader 读法在 7b-3 改 texelFetch
5. **数组 count 取最大使用者**；数组槽小用户按索引访问（[0]/[1]）合法
6. **同资源双号现象**（DeferredLighting G-Buffer 读在 b32-34/b38、NRD 在 b32-34/b39、
   ReSTIR b4/DL b36/NRD b37 同写 "Noisy HDR Color"）：统一映射单套号消灭
7. **契约冲突拆分为先**（用户裁决 2026-10-20）：格式契约（image format）或 block 契约（SSBO 结构）
   同槽不一致时优先拆独立槽，不强融结构

## v4 裁决（2026-10-21，用户总宗旨：拆分为主，槽位不够就开新槽——binding上限远超需求）

- **ReSTIR DI G-Buffer RW读槽 98-101**：DI的storage读法（imageLoad）保留不改造（推翻v1裁决1的
  texelFetch统一方向），为DI开专属RW槽RESTIR_DI_GBUFFER_{DIFFUSE/NORMAL/EMISSION/SPECF0}；
  GI维持TEXTURE读法用G-Buffer域20-23。表总量=102槽（0-101）。
- 胖头reservoir八block匿名化（成员直访式，与ReSTIR shader用法对齐）。

## 契约对账记录（v3，2026-10-20 开工前核定）

- **拆1**：OUT_COLOR 钉 rgba16f；post_process 的 LDR 输出（rgba8）拆新槽 POSTPROCESS_OUT
- **拆2**：ReSTIR DI/GI 的 reservoirs block 不同构（DIReservoir/RestarDISetting vs
  GIReservoir/RestirGISetting）且各用各的物理buffer（"ReSTIR DI Reservoirs [i]" vs
  "Reservoir Buffer 0-2"）→ 拆 DI 四槽+GI 四槽
- **归一**：FINAL_COLOR = "Noisy HDR Color" 资源（ReSTIR DI/GI b4、DL b36、NRD combine b37 四号归一）
- **融合无碍**：EXPOSURE_DATA（histogram/exposure/post_process 三处同构）、
  CLUSTER_DRAW_INFOS（draw_read/culling_write/point_shadow 三处同构 MeshClusterDrawInfo slot[]）、
  VXGI_CLIPMAP_BUFFER（四处同构 ClipmapInfo）
- **维度契约**：CLIPMAP_A 与 FOG_VOXEL_{CURRENT/HISTORY/INTEGRATED} 为 image3D，
  FOG_VOXEL_OUT 为 image2D；其余 RW 槽均 image2D

## 终态设计（2026-10-20 用户定稿：三胖头+三全量RS+边校验）

**基石（探针实证）**：spirv-reflect 的 accessed=「main 可达调用图内指令引用」，纯声明/死函数引用均不计
⇒ set2 胖头零运行时成本，shader 只 include 胖头、零手写 layout。

- **Shader端**：`set2/set2.glsl` 胖头（98槽全量声明+访问器随迁+契约表）；L3域头（history/shadows/
  surface_cache_lighting）声明并入后退役；L4算法头保留；刀6收伞后每shader=胖头按需+算法头按需
- **校验器转型**：pipeline点基础校验保留（抓宏号/format错）；新增RDG边校验层=accessed⊆edges⊆RS
  （accessed注册表：Create*Pipeline时按rootSignature登记；ReadIfValid条件槽留痕放行+计数；
  violations收集统一抛）——悬空槽根治+迁移漏项实时检出
- **set1自包含**：补L1 defines include；sky.glsl拆分保留

## 槽位分配 v3（0-97 共98槽；F列=RW槽契约格式/维度，B列=buffer槽契约block）

### 共享域 0-13（跨pass语义槽）
| # | 宏 | 类型 | count | F/B契约 | 原号 | 使用者 |
|---|----|----|-------|---------|------|--------|
| 0 | SET2_BINDING_LIGHT_CLUSTER_GRID | RW_TEXTURE | 1 | 3D | b10 | Cluster/DDGI/DeferredLighting/VolumetricFog |
| 1 | SET2_BINDING_LIGHT_CLUSTER_INDEX | RW_BUFFER | 1 | light_cluster_index{} | b11 | 同上 |
| 2 | SET2_BINDING_HISTORY_DEPTH | TEXTURE | 2 | — | b12 | 8 pass（NRD经索引0读本帧深度） |
| 3 | SET2_BINDING_HISTORY_DEPTH_PYRAMID | TEXTURE | 2 | — | b13 | GPUCulling/SSSR |
| 4 | SET2_BINDING_HISTORY_VELOCITY | TEXTURE | 1 | — | b14 | Reprojection/SVGF/TAA |
| 5 | SET2_BINDING_HISTORY_OBJECT_ID | TEXTURE | 2 | — | b15 | Reprojection/SVGF |
| 6 | SET2_BINDING_DIRECTIONAL_SHADOW | TEXTURE | 4 | — | b22 | 7 pass |
| 7 | SET2_BINDING_POINT_SHADOW | TEXTURE_CUBE | 8 | — | b23 | 7 pass（filtered0-3+depth4-7） |
| 8 | SET2_BINDING_SURFACE_CACHE | TEXTURE | 5 | — | b24 | Clipmap/RTBase/ReSTIRGI/SSSR |
| 9 | SET2_BINDING_VOLUME_LIGHT_IRRADIANCE | TEXTURE | MAX_VOLUME_LIGHT_COUNT | — | b25 | 4 pass |
| 10 | SET2_BINDING_VOLUME_LIGHT_DEPTH | TEXTURE | MAX_VOLUME_LIGHT_COUNT | — | b26 | 4 pass |
| 11 | SET2_BINDING_MESH_CARD_READBACK | RW_BUFFER | 1 | mesh_card_readbacks{} | b27 | 4 pass |
| 12 | SET2_BINDING_GIZMO_DRAW_DATA | RW_BUFFER | 1 | gizmoDrawData{} | b32(gizmo) | Gizmo/GPUCulling |
| 13 | SET2_BINDING_CLUSTER_DRAW_INFOS_READ | BUFFER | 6 | MeshClusterDrawInfo slot[] | b21读侧 | 6个draw pass |

### mesh 剔除域 14-19（GPUCulling 写侧）
| 14 | SET2_BINDING_CLUSTER_DRAW_INFOS_WRITE | RW_BUFFER | 1024 | MeshClusterDrawInfo slot[] | b21写侧 |
| 15 | SET2_BINDING_INDIRECT_MESH_DRAW_DATAS | RW_BUFFER | 1024 | IndirectMeshDrawDatas{} | b16写侧 |
| 16 | SET2_BINDING_INDIRECT_MESH_DRAW_COMMANDS | RW_BUFFER | 1024 | IndirectMeshDrawCommands{} | b17 |
| 17 | SET2_BINDING_INDIRECT_CLUSTER_DRAW_DATAS | RW_BUFFER | 1024 | IndirectClusterDrawDatas{} | b18 |
| 18 | SET2_BINDING_INDIRECT_CLUSTER_DRAW_COMMANDS | RW_BUFFER | 1024 | IndirectClusterDrawCommands{} | b19 |
| 19 | SET2_BINDING_INDIRECT_CLUSTER_GROUP_DATAS | RW_BUFFER | 1024 | IndirectClusterGroupDrawDatas{} | b20 |

### G-Buffer 域 20-30
| 20 | SET2_BINDING_GBUFFER_DIFFUSE_METALLIC | TEXTURE | 1 | — | b0(ReSTIR)/b32(DL,NRD) |
| 21 | SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS | TEXTURE | 1 | — | b1/b33(DL,NRD)/b32(SVGF) |
| 22 | SET2_BINDING_GBUFFER_EMISSION | TEXTURE | 1 | — | b2/b34(DL) |
| 23 | SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS | TEXTURE | 1 | — | b7/b38(DL)/b39(NRD)/b3(SSSR) |
| 24 | SET2_BINDING_GBUFFER_GEOMETRIC_NORMAL_AO | TEXTURE | 1 | — | b37(DL) |
| 25 | SET2_BINDING_GBUFFER_IOR | TEXTURE | 1 | — | b39(DL) |
| 26 | SET2_BINDING_DIRECTIONAL_DEPTH_HISTORY | TEXTURE | 1 | — | b35(DL) |
| 27 | SET2_BINDING_REPROJECTION_RESULT | TEXTURE | 1 | — | b3(ReSTIR DI/GI) |
| 28 | SET2_BINDING_FINAL_COLOR | RW_TEXTURE | 1 | rgba16f | b4/b36(DL)/b37(NRD combine)，"Noisy HDR Color"四号归一 |
| 29 | SET2_BINDING_DI_DIFFUSE_COLOR | RW_TEXTURE | 1 | rgba16f | b5(DI)/b35(NRD viewZ) |
| 30 | SET2_BINDING_DI_SPECULAR_COLOR | RW_TEXTURE | 1 | rgba16f | b6(DI)/b36(NRD viewZ)/b33(NRD copySssr) |

### DDGI 域 31-37（原DDGI b32-38）
| 31 | SET2_BINDING_DDGI_TRACE_ALBEDO_METALLIC | RW_TEXTURE | 1 | rgba8 |
| 32 | SET2_BINDING_DDGI_TRACE_NORMAL_ROUGHNESS | RW_TEXTURE | 1 | rgba8_snorm |
| 33 | SET2_BINDING_DDGI_TRACE_EMISSION | RW_TEXTURE | 1 | rgba16f |
| 34 | SET2_BINDING_DDGI_TRACE_POSITION | RW_TEXTURE | 1 | rgba16f（方向+距离） |
| 35 | SET2_BINDING_DDGI_RADIANCE | RW_TEXTURE | 1 | rgba16f |
| 36 | SET2_BINDING_DDGI_IRRADIANCE | RW_TEXTURE | 1 | rgba16f |
| 37 | SET2_BINDING_DDGI_DEPTH | RW_TEXTURE | 1 | rg16f |

### 后处理通用域 38-44（通用IO=角色槽：槽共享、资源按边逐pass换绑）
| 38 | SET2_BINDING_IN_COLOR | TEXTURE | 2 | — | bloom双输入取最大；count=1用户取[0] |
| 39 | SET2_BINDING_OUT_COLOR | RW_TEXTURE | 1 | rgba16f | bloom/fxaa/SVGF/SSSR等HDR中间色 |
| 40 | SET2_BINDING_POSTPROCESS_OUT | RW_TEXTURE | 1 | rgba8 | v3拆1：post_process的LDR输出 |
| 41 | SET2_BINDING_EXPOSURE_DATA | RW_BUFFER | 1 | EXPOSURE_DATA{}（三处同构） | exposure/postprocess |
| 42 | SET2_BINDING_IN_TEX_ARRAY | TEXTURE | 6 | — | PointShadow输入 |
| 43 | SET2_BINDING_OUT_TEX_ARRAY | RW_TEXTURE | 6 | rg32f | PointShadow输出 |
| 44 | SET2_BINDING_BRDF_LUT | TEXTURE | 1 | — | SSSR |

### 金字塔/深度域 45-46
| 45 | SET2_BINDING_DEPTH_PYRAMID_LAST_MIP | TEXTURE | 1 | — |
| 46 | SET2_BINDING_DEPTH_PYRAMID_CURRENT_MIP | RW_TEXTURE | 1 | r32f |

### ReSTIR DI 域 47-50（v3拆2：block=DIReservoir/RestirDISetting）
| 47 | SET2_BINDING_RESTIR_DI_SETTING | RW_BUFFER | 1 | RestarDISetting |
| 48 | SET2_BINDING_RESTIR_DI_RESERVOIRS | RW_BUFFER | 1 | DIReservoir[] |
| 49 | SET2_BINDING_RESTIR_DI_PREV_RESERVOIRS | RW_BUFFER | 1 | DIReservoir[] |
| 50 | SET2_BINDING_RESTIR_DI_RESULT_RESERVOIRS | RW_BUFFER | 1 | DIReservoir[] |

### ReSTIR GI 域 51-56（v3拆2：block=GIReservoir/RestirGISetting）
| 51 | SET2_BINDING_RESTIR_GI_SETTING | RW_BUFFER | 1 | RestirGISetting |
| 52 | SET2_BINDING_RESTIR_GI_RESERVOIRS | RW_BUFFER | 1 | GIReservoir[] |
| 53 | SET2_BINDING_RESTIR_GI_PREV_RESERVOIRS | RW_BUFFER | 1 | GIReservoir[] |
| 54 | SET2_BINDING_RESTIR_GI_RESULT_RESERVOIRS | RW_BUFFER | 1 | GIReservoir[] |
| 55 | SET2_BINDING_GI_DIFFUSE_COLOR | RW_TEXTURE | 1 | rgba16f | 原b5(GI) |
| 56 | SET2_BINDING_GI_SPECULAR_COLOR | RW_TEXTURE | 1 | rgba16f | 原b6(GI) |

### SSSR 域 57-63
| 57 | SET2_BINDING_SSSR_HIT_RESULT | RW_TEXTURE | 1 | rgba16f |
| 58 | SET2_BINDING_SSSR_HIT_COLOR | RW_TEXTURE | 1 | rgba16f |
| 59 | SET2_BINDING_SSSR_PDF | RW_TEXTURE | 1 | rgba16f |
| 60 | SET2_BINDING_SSSR_RESOLVE | RW_TEXTURE | 1 | rgba16f |
| 61 | SET2_BINDING_SSSR_HISTORY | RW_TEXTURE | 1 | rgba16f |
| 62 | SET2_BINDING_SSSR_IN_COLOR_PYRAMID | TEXTURE | 1 | — |
| 63 | SET2_BINDING_SSSR_REPROJECTION_RESULT | TEXTURE | 1 | — |

### SVGF 域 64-68（IN/OUT并入通用38/39，G-Buffer读并入20/21）
| 64 | SET2_BINDING_SVGF_HISTORY_GBUFFER_NORMAL | TEXTURE | 1 | — |
| 65 | SET2_BINDING_SVGF_HISTORY_MOMENTS | TEXTURE | 1 | — |
| 66 | SET2_BINDING_SVGF_CURRENT_MOMENTS | RW_TEXTURE | 1 | rgba32f |
| 67 | SET2_BINDING_SVGF_HISTORY_COLOR_VARIANCE | TEXTURE | 1 | — |
| 68 | SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE | RW_TEXTURE | 2 | rgba16f |

### SurfaceCache 域 69-74
| 69 | SET2_BINDING_SC_CACHE_DIFFUSE | TEXTURE | 1 | — |
| 70 | SET2_BINDING_SC_CACHE_NORMAL | TEXTURE | 1 | — |
| 71 | SET2_BINDING_SC_CACHE_EMISSION | TEXTURE | 1 | — |
| 72 | SET2_BINDING_SC_CACHE_DEPTH | TEXTURE | 1 | — |
| 73 | SET2_BINDING_SC_CACHE_LIGHTING | RW_TEXTURE | 1 | rgba16f |
| 74 | SET2_BINDING_SC_DIRECT_LIGHTING_DISPATCH | RW_BUFFER | 1 | direct_lighting_dispatch{} |

### NRD wrapper 域 75-78（引擎侧wrapper；NRD SDK内部走NRDIntegration私有布局）
| 75 | SET2_BINDING_NRD_VIEW_Z | RW_TEXTURE | 1 | r32f |
| 76 | SET2_BINDING_NRD_NORMAL | RW_TEXTURE | 1 | rgba8_snorm |
| 77 | SET2_BINDING_NRD_OUT_DIFFUSE | TEXTURE | 1 | — |
| 78 | SET2_BINDING_NRD_OUT_SPECULAR | TEXTURE | 1 | — |

### ReSTIR DI G-Buffer RW读槽 98-101（v4拆分：DI的storage读法专属，与G-Buffer域TEXTURE槽分立）
| 98 | SET2_BINDING_RESTIR_DI_GBUFFER_DIFFUSE | RW_TEXTURE | 1 | rgba8 | 原ReSTIR b0 |
| 99 | SET2_BINDING_RESTIR_DI_GBUFFER_NORMAL | RW_TEXTURE | 1 | rgba8 | 原b1 |
| 100 | SET2_BINDING_RESTIR_DI_GBUFFER_EMISSION | RW_TEXTURE | 1 | rgba8 | 原b2 |
| 101 | SET2_BINDING_RESTIR_DI_GBUFFER_SPECF0 | RW_TEXTURE | 1 | rgba8 | 原b7 |

### 其余 pass 私有 79-97
| 79 | SET2_BINDING_TAA_CURRENT_COLOR | TEXTURE | 1 | — | taa.comp b32 |
| 80 | SET2_BINDING_TAA_HISTORY_COLOR | TEXTURE | 1 | — | taa.comp b33 |
| 81 | SET2_BINDING_TAA_REPROJECTION_RESULT | TEXTURE | 1 | — | taa.comp b34 |
| 82 | SET2_BINDING_TAA_OUT_COLOR | RW_TEXTURE | 1 | rgba16f | taa.comp b35 |
| 83 | SET2_BINDING_TAA_HISTORY_OUT | RW_TEXTURE | 1 | rgba16f | taa.comp b36 |
| 84 | SET2_BINDING_REPROJECTION_CURRENT_NORMAL | TEXTURE | 1 | — | reprojection.comp b32 |
| 85 | SET2_BINDING_REPROJECTION_HISTORY_NORMAL | TEXTURE | 1 | — | reprojection.comp b33 |
| 86 | SET2_BINDING_REPROJECTION_RESULT_OUT | RW_TEXTURE | 1 | rgba32f | reprojection.comp b34 |
| 87 | SET2_BINDING_FOG_VOXEL_CURRENT | RW_TEXTURE | 1 | rgba16f,3D | fog b32 |
| 88 | SET2_BINDING_FOG_VOXEL_HISTORY | RW_TEXTURE | 1 | rgba16f,3D | fog b33 |
| 89 | SET2_BINDING_FOG_VOXEL_INTEGRATED | RW_TEXTURE | 1 | rgba16f,3D | fog b34 |
| 90 | SET2_BINDING_FOG_VOXEL_OUT | RW_TEXTURE | 1 | rgba16f,2D | fog b35 |
| 91 | SET2_BINDING_CLIPMAP_A | RW_TEXTURE | 1 | rgba16f,3D | vxgi b32 |
| 92 | SET2_BINDING_CLIPMAP_B | RW_BUFFER | 1 | ClipmapInfo | vxgi b33 |
| 93 | SET2_BINDING_CLIPMAP_C | RW_TEXTURE | 1 | rgba16f,2D | visualize b34 |
| 94 | SET2_BINDING_RTB_A | RW_TEXTURE | 1 | rgba8 | RTBase b32 |
| 95 | SET2_BINDING_PT_A | RW_TEXTURE | 1 | rgba16f | PathTracing b32 |
| 96 | SET2_BINDING_PT_B | RW_TEXTURE | 1 | rgba32f | PathTracing b33 |
| 97 | SET2_BINDING_IBL_A | RW_TEXTURE | 1 | rgba16f | IBL b32 |

## 待定项（7b-3 施工时现场裁决）

- Clipmap b33 的边语义（矩阵无注释）——读边声明补注
- PointShadow x6 数组与普通 x1 的 count 统一策略验证
- DI 的 G-Buffer 读法 imageLoad→texelFetch 改造范围（ReSTIR DI 四张+NRD common 四张）
- legacy_set0.glsl 的 cluster/裸声明收敛去向

## 施工顺序（终态）

1. **7b-4**：set2.glsl胖头——98声明+访问器+契约表（本表即契约源）
2. **7b-5**：校验器转型——注册表+边校验phase+ReadIfValid留痕；先于迁移落地，现有旧槽号pass应全绿
3. **7b-3**：逐pass迁移——C++三行拼装+边宏+shader换胖头；边校验器实时把关
4. **刀6**：收伞删common.glsl，每shader最小include集

验收：每阶段全量重编+校验双绿+对照旧日志零回归
