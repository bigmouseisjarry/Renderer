#version 460
#extension GL_GOOGLE_include_directive : enable

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

void main()
{
    // 采集（GetSurfaceInfo，bakeIridescence=false：不透明路无薄膜，拆分定案）；alphaClip是pass机械量留调用侧
    SurfaceInfo surface = GetSurfaceInfo(IN_ID, IN_TEXCOORD, IN_POSITION.xyz, IN_NORMAL, IN_TANGENT, VIS_IN_COLOR, false);
    vec4 color = vec4(VIS_IN_COLOR, 1.0f);

    if(FetchMaterial(IN_ID).alphaClip >= 1.0f - surface.transparent)
        discard;    // 透明度测试

    vec4 gbufferDiffuse, gbufferNormal, gbufferEmission, gbufferGNormalAO, gbufferF0Roughness;
    Encode2GBuffer(surface.diffuse, surface.transparent, surface.roughness, surface.F0, surface.normal, surface.emission,
                   surface.gnormal, surface.ao,
                   gbufferDiffuse, gbufferNormal, gbufferEmission, gbufferGNormalAO, gbufferF0Roughness,
                   surface.specF);

    if(GLOBAL_SETTING.clusterInspectMode == 0) OUT_GBUFFER_DIFFUSE = gbufferDiffuse;
    else                                       OUT_GBUFFER_DIFFUSE = color;

    OUT_GBUFFER_NORMAL                 = gbufferNormal;
    OUT_GBUFFER_EMISSION               = gbufferEmission;
    OUT_GBUFFER_VELOCITY               = CalculateVelocity(IN_POSITION, IN_PREV_POSITION);
    OUT_GBUFFER_OBJECT_ID              = IN_ID;
    OUT_GBUFFER_GEOMETRIC_NORMAL_AO    = gbufferGNormalAO;
    OUT_GBUFFER_SPECULAR_F0            = gbufferF0Roughness;
}
