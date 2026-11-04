#version 460
#extension GL_GOOGLE_include_directive : enable

// GBufferThinPass专用（原deferred.frag 2026-11-03拆分）：薄透明路径，虹彩烘焙无条件——
// 薄透明材质自带薄膜参数(ior=η2/index2=η3/metallic=κ3/dinc=膜厚)，无需逐材质分支；
// specF恒1（光照端F=烘焙F0跳过Schlick）。当前无薄透明网格在册，此路为Phase2落地形态。
#include "include/common.glsl"

layout(location = 0) in vec4 IN_POSITION;
layout(location = 1) in vec4 IN_PREV_POSITION;
layout(location = 2) in vec3 VIS_IN_COLOR;
layout(location = 3) in vec2 IN_TEXCOORD;
layout(location = 4) in vec3 IN_NORMAL;
layout(location = 5) in vec4 IN_TANGENT;
layout(location = 6) in flat uint IN_ID;

layout (location = 0) out vec4 OUT_GBUFFER_DIFFUSE;
layout (location = 1) out vec4 OUT_GBUFFER_NORMAL;
layout (location = 2) out vec4 OUT_GBUFFER_EMISSION;
layout (location = 3) out vec2 OUT_GBUFFER_VELOCITY;
layout (location = 4) out uint OUT_GBUFFER_OBJECT_ID;
layout (location = 5) out vec4 OUT_GBUFFER_GEOMETRIC_NORMAL_AO;
layout (location = 6) out vec4 OUT_GBUFFER_SPECULAR_F0;
layout (location = 7) out float OUT_GBUFFER_IOR;

void main()
{
    // 采集（GetSurfaceInfo，bakeIridescence=true：按当时视角烘焙薄膜F0并置specF；非虹彩薄材质走正常Schlick）
    SurfaceInfo surface = GetSurfaceInfo(IN_ID, IN_TEXCOORD, IN_POSITION.xyz, IN_NORMAL, IN_TANGENT, VIS_IN_COLOR, true);
    vec4 color = vec4(VIS_IN_COLOR, 1.0f);

    if(FetchMaterial(IN_ID).alphaClip >= 1.0f - surface.transparent)
        discard;    // 透明度测试

    // 打包（Encode2GBuffer薄透明形；velocity/objectId是pass机械量不走材质打包）
    vec4 gbufferDiffuse, gbufferNormal, gbufferEmission, gbufferGNormalAO, gbufferF0Roughness;
    float gbufferIorR8;
    Encode2GBuffer(surface.diffuse, surface.transparent, surface.roughness, surface.F0, surface.normal, surface.emission,
                   surface.gnormal, surface.ao, surface.ior,
                   gbufferDiffuse, gbufferNormal, gbufferEmission, gbufferGNormalAO, gbufferF0Roughness, gbufferIorR8,
                   surface.specF);

    if(GLOBAL_SETTING.clusterInspectMode == 0) OUT_GBUFFER_DIFFUSE = gbufferDiffuse;
    else                                       OUT_GBUFFER_DIFFUSE = color;

    OUT_GBUFFER_NORMAL                 = gbufferNormal;
    OUT_GBUFFER_EMISSION               = gbufferEmission;
    OUT_GBUFFER_VELOCITY               = CalculateVelocity(IN_POSITION, IN_PREV_POSITION);
    OUT_GBUFFER_OBJECT_ID              = IN_ID;
    OUT_GBUFFER_GEOMETRIC_NORMAL_AO    = gbufferGNormalAO;
    OUT_GBUFFER_SPECULAR_F0            = gbufferF0Roughness;
    OUT_GBUFFER_IOR                    = gbufferIorR8;
}
